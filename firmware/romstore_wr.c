// romstore_wr.c — filling a slot from a Game Pak read through the N64 (demos/stadium/n64bk). Core0 only.
// No romstore_flash_ok() gate: the session is the writer, and the N64 waits on the ack (bk_proto.h).
#include "romstore.h"
#include <string.h>
#include "flash_burn.h"

typedef struct {
    bool     active;
    uint8_t  slot;
    uint32_t rom_off;           // absolute flash offset of the slot's ROM
    uint32_t rom_len;
    uint32_t ram_len;
    uint32_t rom_at;            // bytes accepted so far, per region
    uint32_t save_at;
    uint32_t rescue_at;
} wr_t;

static wr_t g_wr;

static bool region_put(uint32_t base, uint32_t limit, uint32_t *at,
                       uint32_t off, const uint8_t *buf, uint32_t len) {
    if (!g_wr.active || off != *at || !len) return false;
    if (off + len > limit) return false;
    if (len & (FLASH_PAGE_SIZE - 1)) return false;          // flash_range_program's granularity
    if (off & (FLASH_PAGE_SIZE - 1)) return false;

    uint32_t done = 0;
    while (done < len) {
        uint32_t abs    = base + off + done;
        uint32_t into   = abs & (FLASH_SECTOR_SIZE - 1);    // how far into its sector this lands
        uint32_t room   = FLASH_SECTOR_SIZE - into;
        uint32_t n      = len - done < room ? len - done : room;
        flash_burn(abs, into ? 0 : FLASH_SECTOR_SIZE, buf + done, n);
        done += n;
    }
    *at = off + len;
    return true;
}

bool romstore_wr_begin(uint8_t slot, uint32_t rom_len, uint32_t ram_len) {
    memset(&g_wr, 0, sizeof g_wr);

    if (slot < 1 || slot > GB_SLOTS) return false;
    if (!rom_len || (rom_len & (GB_ROM_WINDOW - 1))) return false;
    if (rom_len > GB_SLOT_STRIDE) return false;             // mkslot.py's stride is the ceiling
    if (ram_len > GB_SRAM_MAX) return false;

    uint32_t off = GB_ROM_BASE + (uint32_t)(slot - 1) * GB_SLOT_STRIDE;
    if (off + rom_len > GB_ROM_LIMIT) return false;         // slot 8 always fails, as in mkslot.py
    g_wr.active  = true;
    g_wr.slot    = slot;
    g_wr.rom_off = off;
    g_wr.rom_len = rom_len;
    g_wr.ram_len = ram_len;
    return true;
}

bool romstore_wr_rom(uint32_t off, const uint8_t *buf, uint32_t len) {
    return region_put(g_wr.rom_off, g_wr.rom_len, &g_wr.rom_at, off, buf, len);
}

bool romstore_wr_save(uint32_t off, const uint8_t *buf, uint32_t len) {
    if (!g_wr.active) return false;
    return region_put(save_off(g_wr.slot), GB_SAVE_SLOT, &g_wr.save_at, off, buf, len);
}

// Not the slot's save, so a restore can undo itself after that save is overwritten.
bool romstore_wr_rescue(uint32_t off, const uint8_t *buf, uint32_t len) {
    if (!g_wr.active) return false;
    return region_put(GB_RESCUE_OFF, GB_SAVE_SLOT, &g_wr.rescue_at, off, buf, len);
}

bool romstore_wr_commit(uint8_t slot, uint8_t mbc, bool has_rtc, uint8_t cgb_flag,
                        const char *title) {
    static uint8_t dirbuf[GB_SLOTS * sizeof(gb_slot_hdr_t)];
    gb_slot_hdr_t h;

    if (!g_wr.active || slot != g_wr.slot) return false;
    if (g_wr.rom_at != g_wr.rom_len) return false;          // a short dump must not be published

    memset(&h, 0, sizeof h);
    h.magic     = GB_SLOT_MAGIC;
    h.rom_off   = g_wr.rom_off;
    h.rom_len   = g_wr.rom_len;
    h.ram_len   = g_wr.ram_len;
    h.rom_banks = (uint16_t)(g_wr.rom_len / GB_ROM_WINDOW);
    h.mbc       = mbc;
    h.has_rtc   = has_rtc ? 1 : 0;
    h.cgb_flag  = cgb_flag;
    for (unsigned i = 0; i < sizeof h.title - 1 && title[i]; i++) h.title[i] = title[i];

    // Only the headers are held: the rest of the directory sector is erased.
    memcpy(dirbuf, (const uint8_t *)(XIP_BASE + GB_DIR_OFF), sizeof dirbuf);
    memcpy(dirbuf + (uint32_t)(slot - 1) * sizeof(gb_slot_hdr_t), &h, sizeof h);
    flash_burn(GB_DIR_OFF, FLASH_SECTOR_SIZE, dirbuf, sizeof dirbuf);

    g_wr.active = false;
    return true;
}

void romstore_wr_abort(void) { g_wr.active = false; }
