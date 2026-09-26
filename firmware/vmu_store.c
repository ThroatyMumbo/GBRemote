#include "vmu_store.h"
#include "vmu_store_fmt.h"
#include "vmu_stage.h"
#include "romstore.h"
#include "drivers/maple_proto.h"
#include "flash_burn.h"
#include "hardware/xip_cache.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

#define VMU_IDLE_MS       2000u     // quiet time after the last write before a flush starts
#define VMU_MIN_FLUSH_MS 10000u     // wear floor between flushes; a detach ignores it

enum { S_IDLE, S_SECTORS, S_HEADER };

static uint8_t  g_state;
static uint8_t  g_slot;             // the slot being written, or the one loaded from
static uint8_t  g_stage_buf[VMU_SECTOR];
static unsigned g_sect;
static uint32_t g_crc, g_seq, g_writes_at_start, g_flush_at_start, g_last_flush_ms;
static uint32_t g_burned, g_erased;

static uint32_t slot_off(unsigned n)     { return VMU_STORE_OFF + n * VMU_SLOT_STRIDE; }
static uint32_t slot_img_off(unsigned n) { return slot_off(n) + VMU_SLOT_IMG; }

static const uint8_t *xip(uint32_t off) { return (const uint8_t *)(XIP_BASE + off); }

// The erase stalls write_capture's drain for tens of ms, so skip it whenever programming alone gets
// there: NOR only clears bits.
static void burn(uint32_t off, const uint8_t *buf, uint32_t len) {
    const uint8_t *f = xip(off);
    bool erase = false;

    for (uint32_t i = 0; i < len && !erase; i++) { erase = (f[i] & buf[i]) != buf[i]; }

    flash_burn(off, erase ? FLASH_SECTOR_SIZE : 0, buf, len);
    g_burned++;
    g_erased += erase;
}

static void read_hdr(unsigned n, vmu_hdr_t *h) { memcpy(h, xip(slot_off(n)), sizeof *h); }

void vmu_store_init(void) {
    vmu_stage_t *s = vmu_stage();
    vmu_hdr_t h[VMU_SLOTS];

    // A card written over SWD lands behind the XIP cache, which a reset alone does not clear.
    xip_cache_invalidate_all();

    for (unsigned i = 0; i < VMU_SLOTS; i++) { read_hdr(i, &h[i]); }

    // Newest first, then the other one: the older slot is a whole generation behind, but a
    // generation behind beats a formatted card. Two slots exist for exactly this.
    int pick = vmu_hdr_pick(&h[0], &h[1]);
    for (unsigned k = 0; pick >= 0 && k < VMU_SLOTS; k++) {
        unsigned n = ((unsigned)pick + k) % VMU_SLOTS;
        const uint8_t *img = xip(slot_img_off(n));
        if (!vmu_hdr_ok(&h[n])) { continue; }
        if (vmu_crc32(0, img, VMU_CARD_BYTES) != h[n].crc) {
            printf("vmu: slot %u fails its crc\n", n);
            continue;
        }
        memcpy(vmu_card(), img, VMU_CARD_BYTES);
        s->boot_secs = h[n].wall_secs;
        g_seq  = h[n].seq;
        g_slot = (uint8_t)n;
        printf("vmu: card from slot %u seq %lu, clock %lu\n",
               n, (unsigned long)g_seq, (unsigned long)s->boot_secs);
        return;
    }

    // A zeroed card enumerates and then reads as unformatted, which is not what a VMU looks like.
    maple_vmu_format(vmu_card());
    g_seq  = 0;
    g_slot = VMU_SLOTS - 1U; // so the first flush lands in slot 0
}

bool vmu_store_busy(void) { return g_state != S_IDLE; }

static void start_flush(void) {
    const vmu_stage_t *s = vmu_stage();
    g_state           = S_SECTORS;
    g_sect            = 0;
    g_crc             = 0;
    g_burned          = 0;
    g_erased          = 0;
    g_writes_at_start = s->writes;
    g_flush_at_start  = s->flush_req;
    g_slot = (uint8_t)((g_slot + 1U) % VMU_SLOTS);
    g_seq++;
}

// Staged first, because core1 keeps writing the card and the crc must describe what was programmed.
// The crc covers the staged copy, so skipping an identical sector changes nothing.
static void do_sector(void) {
    uint32_t off = slot_img_off(g_slot) + g_sect * VMU_SECTOR;

    memcpy(g_stage_buf, vmu_card() + g_sect * VMU_SECTOR, VMU_SECTOR);
    g_crc = vmu_crc32(g_crc, g_stage_buf, VMU_SECTOR);
    if (memcmp(xip(off), g_stage_buf, VMU_SECTOR) != 0) { burn(off, g_stage_buf, VMU_SECTOR); }

    if (++g_sect == VMU_SECTORS) { g_state = S_HEADER; }
}

// Written last and alone in its sector: until this lands the slot still reads as whatever it was,
// which is what makes an interrupted writeback cost nothing.
static void do_header(uint32_t now_ms) {
    vmu_stage_t *s = vmu_stage();
    static uint8_t page[FLASH_PAGE_SIZE];
    vmu_hdr_t h = { .magic = VMU_HDR_MAGIC, .seq = g_seq, .len = VMU_CARD_BYTES, .crc = g_crc,
                    .wall_secs = vmu_clock_now(now_ms), .writes = g_writes_at_start };

    memset(page, 0xff, sizeof page);
    memcpy(page, &h, sizeof h);
    burn(slot_off(g_slot), page, sizeof page);

    s->saved_writes = g_writes_at_start;
    s->flush_ack    = g_flush_at_start;
    s->saves++;
    g_last_flush_ms = now_ms;
    g_state = S_IDLE;
    printf("vmu: saved slot %u seq %lu, %lu sector(s), %lu erased, crc %08lx\n",
           g_slot, (unsigned long)g_seq, (unsigned long)g_burned,
           (unsigned long)g_erased, (unsigned long)g_crc);
}

void vmu_store_poll(uint32_t now_ms) {
    const vmu_stage_t *s = vmu_stage();

    if (g_state == S_SECTORS) {
        if (romstore_flash_ok(now_ms)) { do_sector(); }
        return;
    }
    if (g_state == S_HEADER)  {
        if (romstore_flash_ok(now_ms)) { do_header(now_ms); }
        return;
    }

    // A detach with nothing new to save still gets its ack, or the counters read as a writeback
    // that never finished.
    if (s->writes == s->saved_writes) { vmu_stage()->flush_ack = s->flush_req; return; }
    if (!romstore_flash_ok(now_ms)) { return; }

    // A detach does not wait for the idle timer or the wear floor: the console is gone and the
    // only copy of the save is in SRAM.
    if (s->flush_req != s->flush_ack) { start_flush(); return; }
    if ((uint32_t)(now_ms - s->last_write_ms) < VMU_IDLE_MS) { return; }
    if (g_last_flush_ms && (now_ms - g_last_flush_ms) < VMU_MIN_FLUSH_MS) { return; }
    start_flush();
}

void vmu_store_force(void) {
    if (g_state == S_IDLE) { start_flush(); }
}

void vmu_store_report(void) {
    const vmu_stage_t *s = vmu_stage();
    uint32_t live = vmu_crc32(0, vmu_card(), VMU_CARD_BYTES);

    printf("  vmu: writes=%lu saved=%lu saves=%lu flush=%lu/%lu live crc=%08lx frames=%lu\n",
           (unsigned long)s->writes, (unsigned long)s->saved_writes, (unsigned long)s->saves,
           (unsigned long)s->flush_req, (unsigned long)s->flush_ack, (unsigned long)live,
           (unsigned long)s->lcd_frames);
    for (unsigned i = 0; i < VMU_SLOTS; i++) {
        vmu_hdr_t h;
        read_hdr(i, &h);
        if (!vmu_hdr_ok(&h)) { printf("  vmu slot %u: empty\n", i); continue; }
        uint32_t crc = vmu_crc32(0, xip(slot_img_off(i)), VMU_CARD_BYTES);
        printf("  vmu slot %u: seq=%lu crc=%08lx %s clock=%lu writes=%lu%s\n", i,
               (unsigned long)h.seq, (unsigned long)h.crc,
               crc == h.crc ? "ok" : "CORRUPT", (unsigned long)h.wall_secs,
               (unsigned long)h.writes, crc == live ? " == live" : "");
    }
}
