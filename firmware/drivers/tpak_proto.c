#include "tpak_proto.h"
#include <string.h>

#define POWER_ON  0x84
#define POWER_OFF 0xFE

enum { REG_POWER, REG_BANK, REG_STATUS, REG_ZERO, REG_WINDOW };

// $0000-$3FFF echoes $8000-$BFFF, or the Controller Pak probe reads back its pattern and calls us one.
static int region_of(uint16_t addr) {
    if (addr < 0x2000U) { return REG_POWER; }
    if (addr < 0x3000U) { return REG_BANK; }
    if (addr < 0x4000U) { return REG_STATUS; }
    if (addr < 0x8000U) { return REG_ZERO; }
    if (addr < 0xA000U) { return REG_POWER; }
    if (addr < 0xB000U) { return REG_BANK; }
    if (addr < 0xC000U) { return REG_STATUS; }
    return REG_WINDOW;
}

void tpak_init(tpak_t *t, gbcart_t *cart) {
    memset(t, 0, sizeof *t);
    t->cart = cart;
}

void tpak_set_feed(tpak_t *t, tpak_feed_fn fn, void *ctx) {
    t->feed = fn;
    t->feed_ctx = ctx;
}

void tpak_set_window(tpak_t *t, tpak_win_fn fn, void *ctx) {
    t->win = fn;
    t->win_ctx = ctx;
}

// RUNNING rises 2-4 ms after access on, with the window reading junk until then; not modeled.
static uint8_t status_byte(const tpak_t *t) {
    uint8_t st = 0;
    // An empty pak clears GBCART_ON, or osGbpakReadId reads 0x40 as a cart change and fails.
    if (t->powered && t->cart) { st |= TPAK_ST_POWERED; }
    if (!t->cart) { st |= TPAK_ST_NO_CART; }
    if (t->access) { st |= TPAK_ST_ACCESS | TPAK_ST_RUNNING; }
    if (t->was_reset) { st |= TPAK_ST_WAS_RESET; }
    return st;
}

static void bank_moved(tpak_t *t, uint16_t was0, uint16_t was1) {
    if (gbcart_want_bank(t->cart, 0) != was0 || gbcart_want_bank(t->cart, 1) != was1) {
        t->refill_req++;
    }
}

static pak_result_t tpak_read(uint16_t addr, uint8_t out[N64_BLOCK], void *ctx) {
    tpak_t *t = (tpak_t *)ctx;

    if (!t->powered) { memset(out, 0, N64_BLOCK); return PAK_OK; }

    // The feed window's read half, the only path that answers with no cartridge staged.
    if (t->win && addr >= 0xC000U && addr < 0xE000U && t->bank == 2U &&
        t->win(t->win_ctx, (uint16_t)(addr - 0xC000U), out)) {
        return PAK_OK;
    }

    switch (region_of(addr)) {
    case REG_POWER:  memset(out, POWER_ON, N64_BLOCK); return PAK_OK;
    case REG_BANK:   memset(out, t->bank,  N64_BLOCK); return PAK_OK;
    case REG_STATUS: {
        uint8_t st = status_byte(t);
        memset(out, st, N64_BLOCK);
        // Only the $B000 decode clears the latch, and only after the byte that reported it.
        if (addr & 0x8000U) {
            memset(out + 1, st & (uint8_t)~TPAK_ST_WAS_RESET, N64_BLOCK - 1);
            t->was_reset = false;
        }
        return PAK_OK;
    }
    case REG_ZERO:   memset(out, 0, N64_BLOCK); return PAK_OK;
    default: break;
    }

    if (!t->access || !t->cart) { memset(out, 0, N64_BLOCK); return PAK_OK; }

    uint16_t gb = (uint16_t)(t->bank * GB_ROM_WINDOW + (addr - 0xC000U));
    if (!gbcart_read_block(t->cart, gb, out, N64_BLOCK)) {
        memset(out, 0, N64_BLOCK);
        t->faults++;
        return PAK_RETRY;                   // core0 has not staged this bank yet
    }
    return PAK_OK;
}

static pak_result_t tpak_write(uint16_t addr, const uint8_t in[N64_BLOCK], void *ctx) {
    tpak_t *t = (tpak_t *)ctx;
    uint8_t v = in[N64_BLOCK - 1];          // the pak writes all 32 bytes in turn; the last one stays

    // GB VRAM, which no cart decodes; Stadium writes it unpowered, so that counts as bank 2 too.
    if (t->feed && addr >= 0xC000U && addr < 0xE000U && (t->bank == 2U || !t->powered)) {
        t->feed(t->feed_ctx, (uint16_t)(addr - 0xC000U), in);
        return PAK_OK;
    }

    switch (region_of(addr)) {
    case REG_POWER:
        // Only 0x84 powers on; every other probe value (rumble, snap, off, 0x00) must read back 0x00.
        t->powered = (v == POWER_ON);
        return PAK_OK;
    case REG_BANK:
        if (!t->powered) { return PAK_OK; }
        t->bank = v & 3U;
        return PAK_OK;
    case REG_STATUS: {
        if (!t->powered) { return PAK_OK; }
        bool want = (v & TPAK_ST_ACCESS) != 0;
        if (want == t->access) { return PAK_OK; }
        t->access = want;
        // Access off holds the cart in /RST, which is what resets its MBC.
        if (!want) {
            t->was_reset = true;
            if (t->cart) {
                uint16_t was0 = gbcart_want_bank(t->cart, 0);
                uint16_t was1 = gbcart_want_bank(t->cart, 1);
                gbcart_mbc_reset(t->cart);
                bank_moved(t, was0, was1);
            }
        }
        return PAK_OK;
    }
    case REG_ZERO:
        return PAK_OK;
    default: break;
    }

    if (!t->powered || !t->access || !t->cart) { return PAK_OK; }

    uint16_t gb = (uint16_t)(t->bank * GB_ROM_WINDOW + (addr - 0xC000U));
    uint16_t was0 = gbcart_want_bank(t->cart, 0);
    uint16_t was1 = gbcart_want_bank(t->cart, 1);
    gbcart_write_block(t->cart, gb, in, N64_BLOCK);
    bank_moved(t, was0, was1);
    return PAK_OK;
}

void tpak_bind(tpak_t *t, n64_pak_t *out) {
    out->read  = tpak_read;
    out->write = tpak_write;
    out->ctx   = t;
}
