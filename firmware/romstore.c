#include "romstore.h"
#include "pad_driver.h"
#include "config_map.h"
#include <string.h>
#include "flash_burn.h"
#include "pico/flash.h"

#define CHUNK       512u        // per romstore_poll() call; see the FIFO note in romstore.h
#define QUIET_MS    250u        // no 0x02/0x03 for this long before flash is allowed
#define FLUSH_MS    2000u       // debounce before a save writeback

static const gb_slot_hdr_t *dir(void) {
    return (const gb_slot_hdr_t *)(XIP_BASE + GB_DIR_OFF);
}

static gb_slot_hdr_t          g_builtin;          // magic 0 until romstore_set_builtin()
static const uint8_t *const  *g_builtin_bank;

static const gb_slot_hdr_t *slot_hdr(uint8_t slot) {
    if (slot == GB_SLOT_BUILTIN) { return g_builtin.magic ? &g_builtin : NULL; }
    if (slot == 0 || slot > GB_SLOTS) { return NULL; }
    const gb_slot_hdr_t *h = &dir()[slot - 1];
    if (h->magic != GB_SLOT_MAGIC) { return NULL; }
    if (h->rom_off < GB_ROM_BASE || h->rom_len == 0) { return NULL; }
    if (h->rom_len > GB_ROM_LIMIT - h->rom_off) { return NULL; }
    if (h->rom_len & (GB_ROM_WINDOW - 1)) { return NULL; }
    if (h->ram_len > GB_SRAM_MAX) { return NULL; }
    return h;
}

static uint8_t  g_slot;
static uint8_t  g_live_slot;        // what the live session was staged for; g_slot is a request
static uint8_t  g_req_slot = 0xfe;      // never a legal request, so the first call always publishes
static uint32_t g_seen_epoch;
static uint32_t g_stage_off;        // progress through the current copy
static int      g_stage_win;        // -1 idle, 2 == the save load
static uint16_t g_stage_bank;
static uint32_t g_flush_at;
static bool     g_flush_due;

void romstore_init(void) {
    g_stage_win = -1;
    gb_stage()->slot = 0;
}

const char *romstore_slot_title(uint8_t slot) {
    const gb_slot_hdr_t *h = slot_hdr(slot);
    return h ? h->title : "";
}

void romstore_set_builtin(const uint8_t *const *banks, uint16_t n) {
    memset(&g_builtin, 0, sizeof g_builtin);
    g_builtin.magic     = GB_SLOT_MAGIC;
    g_builtin.rom_len   = (uint32_t)n * GB_ROM_WINDOW;
    g_builtin.rom_banks = n;
    g_builtin.mbc       = MBC_NONE;
    for (unsigned i = 0; i < 15 && banks[0][0x134 + i] >= 0x20 && banks[0][0x134 + i] < 0x7f; i++) {
        g_builtin.title[i] = (char)banks[0][0x134 + i]; // $0143 is the CGB flag, not text
    }
    g_builtin_bank = banks;
}

const uint8_t *romstore_rom(uint8_t slot, uint32_t *len) {
    const gb_slot_hdr_t *h = slot == GB_SLOT_BUILTIN ? NULL : slot_hdr(slot);
    *len = h ? h->rom_len : 0;
    return h ? (const uint8_t *)(XIP_BASE + h->rom_off) : NULL;
}

const uint8_t *romstore_save(uint8_t slot, uint32_t *len) {
    const gb_slot_hdr_t *h = slot_hdr(slot);
    if (!h || !h->ram_len) { *len = 0; return NULL; }
    *len = h->ram_len;
    return (const uint8_t *)(XIP_BASE + save_off(slot));
}

void romstore_build_page(uint8_t page[SLOT_PAGE_LEN]) {
    const gb_slot_hdr_t *hdrs[GB_SLOTS];
    for (uint8_t s = 1; s <= GB_SLOTS; s++) { hdrs[s - 1] = slot_hdr(s); }
    slot_page_build(page, hdrs);
}

void romstore_select(uint8_t slot) {
    const gb_slot_hdr_t *h = slot_hdr(slot);
    gb_slot_info_t info;

    memset(&info, 0, sizeof info);
    if (h) {
        info.valid     = true;
        info.mbc       = h->mbc;
        info.has_rtc   = h->has_rtc != 0;
        info.rom_banks = h->rom_banks;
        info.ram_len   = h->ram_len;
        memcpy(info.title, h->title, sizeof info.title - 1);
    }
    g_slot = h ? slot : 0;
    gb_stage()->slot = g_slot;
    gb_slot_info_publish(&info);
}

void romstore_config(const uint8_t *cfg) {
    uint8_t slot = 0;
    if (cfg && cfg[CFG_PROTO_SEL] == PROTO_N64 && cfg[CFG_DRV_OWNER] == PROTO_N64) {
        slot = cfg_get(cfg, CFG_DRV + GB_CFG_SLOT_OFF, 0);
    }
    if (slot != g_req_slot) { g_req_slot = slot; romstore_select(slot); }
}

bool romstore_flash_ok(uint32_t now_ms) {
    const gb_stage_t *s = gb_stage();
    if (!(s->epoch & 1U)) {
        return true; // no session: nothing can be mid-burst
    }
    return (uint32_t)(now_ms - s->busy_ms) > QUIET_MS;
}

// g_live_slot, never g_slot: by a detach's flush_req, romstore_config() has moved g_slot on.
static void save_flush(void) {
    gb_stage_t *s = gb_stage();
    const gb_slot_hdr_t *h = slot_hdr(g_live_slot);
    if (!h || !h->ram_len || !s->cart.sram) { s->save_flushed = s->cart.save_dirty; return; }

    uint32_t off = save_off(g_live_slot);
    uint32_t len = (h->ram_len + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    uint32_t seen = s->cart.save_dirty;

    flash_burn(off, GB_SAVE_SLOT, s->cart.sram, len);

    s->save_flushed = seen;                             // seen, not save_dirty: a write that
    g_flush_due = false;                                // landed mid-erase must flush again
}

void romstore_poll(uint32_t now_ms) {
    gb_stage_t *s = gb_stage();
    const gb_slot_hdr_t *h;

    // First, ungated: the last chance to get cart RAM out before the arena changes hands.
    if (s->flush_req != s->flush_ack) {
        if (s->cart.save_dirty != s->save_flushed) { save_flush(); }
        s->flush_ack = s->flush_req;
        return;
    }

    if (s->quiesce != s->quiesce_ack) {                 // core1 wants the arena back
        g_stage_win = -1;
        s->quiesce_ack = s->quiesce;
        return;
    }

    if (!(s->epoch & 1U)) {
        g_seen_epoch = s->epoch;
        g_stage_win = -1;
        return;
    }

    h = slot_hdr(g_slot);
    if (!h) { return; }

    if (g_seen_epoch != s->epoch) {                     // a new session: load the save first
        g_seen_epoch = s->epoch;
        g_live_slot  = g_slot;                          // latched here, read by save_flush()
        g_stage_win  = h->ram_len ? 2 : -1;
        g_stage_off  = 0;
        s->ready     = 0;
        if (g_stage_win < 0) { s->ready = 1; }
    }

    if (g_stage_win == 2) {
        uint32_t off = save_off(g_slot);
        uint32_t n   = h->ram_len - g_stage_off;
        if (n > CHUNK) { n = CHUNK; }
        memcpy(s->cart.sram + g_stage_off, (const uint8_t *)(XIP_BASE + off + g_stage_off), n);
        g_stage_off += n;
        if (g_stage_off >= h->ram_len) { g_stage_win = -1; s->ready = 1; }
        return;
    }

    if (g_stage_win < 0) {
        for (unsigned w = 0; w < 2; w++) {
            uint16_t want = gbcart_want_bank(&s->cart, w);
            if (s->cart.win_bank[w] == want) { continue; }
            g_stage_win  = (int)w;
            g_stage_bank = want;
            g_stage_off  = 0;
            s->cart.win_bank[w] = GB_BANK_NONE;
            __dmb();
            break;
        }
    }

    if (g_stage_win >= 0) {
        unsigned w = (unsigned)g_stage_win;
        uint16_t bank = g_stage_bank < h->rom_banks ? g_stage_bank : 0;
        const uint8_t *src = h == &g_builtin ? g_builtin_bank[bank] + g_stage_off
            : (const uint8_t *)(XIP_BASE + h->rom_off + (uint32_t)g_stage_bank * GB_ROM_WINDOW + g_stage_off);
        uint32_t n = GB_ROM_WINDOW - g_stage_off;
        if (n > CHUNK) { n = CHUNK; }
        // Const to the model but ours to fill; core1 reads it only where win_bank[] matches.
        memcpy((uint8_t *)s->cart.win[w] + g_stage_off, src, n);
        g_stage_off += n;
        if (g_stage_off >= GB_ROM_WINDOW) {
            __dmb();
            s->cart.win_bank[w] = g_stage_bank;
            g_stage_win = -1;
        }
        return;
    }

    if (s->cart.save_dirty != s->save_flushed) {
        if (!g_flush_due) { g_flush_due = true; g_flush_at = now_ms + FLUSH_MS;
        } else if ((int32_t)(now_ms - g_flush_at) >= 0 && romstore_flash_ok(now_ms)) {
            save_flush();
        }
    }
}
