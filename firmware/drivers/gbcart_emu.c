#include "gbcart_emu.h"
#include <string.h>

void gbcart_mbc_reset(gbcart_t *c) {
    c->bank_lo         = 1;
    c->bank_hi         = 0;
    c->bank_b8         = 0;
    c->mode            = 0;
    c->ram_enabled     = false;
    c->rtc_sel         = 0;
    c->rtc_latch_prev  = 0xff;
}

void gbcart_reset(gbcart_t *c) {
    gbcart_mbc_reset(c);
    c->win_bank[0]     = 0xffff;
    c->win_bank[1]     = 0xffff;
}

// A zero low field reads as the bank above (MBC1 cannot reach $20/$40/$60); only MBC5 selects bank 0.
static uint16_t high_bank(const gbcart_t *c) {
    uint16_t b;
    switch (c->mbc) {
    case MBC1: {
        uint8_t lo = c->bank_lo & 0x1fU;
        if (lo == 0) { lo = 1; }
        b = (uint16_t)(((c->bank_hi & 3U) << 5) | lo);
        break;
    }
    case MBC2:
        b = c->bank_lo & 0x0fU;
        if (!b) { b = 1; }
        break;
    case MBC3:
        b = c->bank_lo & 0x7fU;
        if (!b) { b = 1; }
        break;
    case MBC5: b = (uint16_t)(((c->bank_b8 & 1U) << 8) | c->bank_lo); break;
    default:   b = 1; break;
    }
    return c->rom_banks ? (uint16_t)(b % c->rom_banks) : 0;
}

// Bank 0 except in MBC1 mode 1, where the RAM-bank bits move this window too.
static uint16_t low_bank(const gbcart_t *c) {
    if (c->mbc != MBC1 || !c->mode) { return 0; }
    uint16_t b = (uint16_t)((c->bank_hi & 3U) << 5);
    return c->rom_banks ? (uint16_t)(b % c->rom_banks) : 0;
}

uint16_t gbcart_want_bank(const gbcart_t *c, unsigned which) {
    return which ? high_bank(c) : low_bank(c);
}

// Shared with the serve path's RAM window, so hardware and emulation cannot disagree.
uint32_t gbcart_ram_bank(const gbcart_t *c) {
    if (c->mbc == MBC1) { return c->mode ? (c->bank_hi & 3U) : 0U; }
    if (c->mbc == MBC3 || c->mbc == MBC5) { return c->bank_hi & 0x0fU; }
    return 0;
}

// Flat offset into the cart's RAM for a GB $A000-$BFFF address, or -1 if nothing is mapped there.
static int32_t sram_offset(const gbcart_t *c, uint16_t addr) {
    if (!c->ram_enabled || !c->ram_len || !c->sram) { return -1; }
    if (c->mbc == MBC2) { // 512 nibbles, mirrored through the 8 KB
        return (int32_t)((addr - 0xA000U) % MBC2_RAM_LEN);
    }

    uint32_t off = gbcart_ram_bank(c) * 0x2000U + (uint32_t)(addr - 0xA000U);
    return off < c->ram_len ? (int32_t)off : -1;
}

bool gbcart_rtc_mapped(const gbcart_t *c) {
    return c->has_rtc && c->mbc == MBC3 && c->rtc_sel >= 0x08 && c->rtc_sel <= 0x0c;
}

uint8_t gbcart_read(const gbcart_t *c, uint16_t addr, bool *ready) {
    *ready = true;

    if (addr < 0x8000U) {
        unsigned w = addr >> 14;                    // 0 or 1
        uint16_t want = gbcart_want_bank(c, w);
        if (!c->win[w] || c->win_bank[w] != want) { *ready = false; return 0xff; }
        return c->win[w][addr & 0x3fffU];
    }

    if (addr < 0xA000U) {
        return 0xff; // VRAM never reaches the cart edge
    }

    if (addr < 0xC000U) {
        if (gbcart_rtc_mapped(c)) {
            return c->ram_enabled ? c->rtc_latched[c->rtc_sel - 0x08] : 0xff;
        }
        int32_t off = sram_offset(c, addr);
        if (off < 0) { return 0xff; }
        uint8_t v = c->sram[off];
        return c->mbc == MBC2 ? (uint8_t)(v | 0xf0U) : v;
    }

    return 0xff;                                // WRAM, OAM, I/O: internal to the CPU
}

bool gbcart_read_block(const gbcart_t *c, uint16_t addr, uint8_t *out, unsigned n) {
    if (addr < 0x8000U) {
        unsigned w = addr >> 14;
        if (!c->win[w] || c->win_bank[w] != gbcart_want_bank(c, w)) { return false; }
        memcpy(out, c->win[w] + (addr & 0x3fffU), n);
        return true;
    }

    if (addr >= 0xA000U && addr < 0xC000U && !gbcart_rtc_mapped(c)) {
        int32_t off = sram_offset(c, addr);
        if (off >= 0 && (uint32_t)off + n <= c->ram_len) {
            memcpy(out, c->sram + off, n);
            if (c->mbc == MBC2) {
                for (unsigned i = 0; i < n; i++) { out[i] |= 0xf0U; }
            }
            return true;
        }
    }

    // VRAM, WRAM, a latched RTC register, disabled RAM: all byte-uniform, none hot.
    bool ready;
    for (unsigned i = 0; i < n; i++) { out[i] = gbcart_read(c, (uint16_t)(addr + i), &ready); }
    return true;
}

// Below $8000 the console writes one value 32 times to one register, so only the last matters.
void gbcart_write_block(gbcart_t *c, uint16_t addr, const uint8_t *in, unsigned n) {
    if (!n) { return; }

    if (addr < 0x8000U) {
        gbcart_write(c, (uint16_t)(addr + n - 1), in[n - 1]);
        return;
    }

    if (addr >= 0xA000U && addr < 0xC000U && !gbcart_rtc_mapped(c)) {
        int32_t off = sram_offset(c, addr);
        if (off >= 0 && (uint32_t)off + n <= c->ram_len) {
            if (c->mbc == MBC2) {
                for (unsigned i = 0; i < n; i++) { c->sram[off + i] = in[i] & 0x0fU; }
            } else {
                memcpy(c->sram + off, in, n);
            }
            c->save_dirty++;
        }
        return;
    }

    for (unsigned i = 0; i < n; i++) { gbcart_write(c, (uint16_t)(addr + i), in[i]); }
}

void gbcart_write(gbcart_t *c, uint16_t addr, uint8_t v) {
    if (addr < 0x2000U) {
        // MBC2 shares $0000-$3FFF with its bank register; A8 picks which one is being written.
        if (c->mbc == MBC2 && (addr & 0x0100U)) { return; }
        c->ram_enabled = (v & 0x0fU) == 0x0a;
        return;
    }

    if (addr < 0x4000U) {
        switch (c->mbc) {
        case MBC_NONE:
        default: break;
        case MBC2:
            if (addr & 0x0100U) { c->bank_lo = v & 0x0fU; }
            break;
        case MBC1: c->bank_lo = v & 0x1fU; break;
        case MBC3: c->bank_lo = v & 0x7fU; break;
        case MBC5:
            if (addr < 0x3000U) {
                c->bank_lo = v;
            } else {
                c->bank_b8 = v & 1U;
            }
            break;
        }
        return;
    }

    if (addr < 0x6000U) {
        if (c->mbc == MBC1 || c->mbc == MBC5) {
            c->bank_hi = v & 0x0fU;
        } else if (c->mbc == MBC3) {
            // 04-07 select no RAM at all: a real MBC3 reads $FF there (rom-tpaktest survey).
            if (v <= 0x07)                   { c->bank_hi = v; c->rtc_sel = 0;
            } else if (v >= 0x08 && v <= 0x0c) {
                c->rtc_sel = v;
            }
        }
        return;
    }

    if (addr < 0x8000U) {
        if (c->mbc == MBC1) {
            c->mode = v & 1U;
        } else if (c->mbc == MBC3 && c->has_rtc) {
            if (c->rtc_latch_prev == 0 && v == 1) {
                for (int i = 0; i < 5; i++) { c->rtc_latched[i] = c->rtc_live[i]; }
            }
            c->rtc_latch_prev = v;
        }
        return;
    }

    if (addr >= 0xA000U && addr < 0xC000U) {
        if (gbcart_rtc_mapped(c)) {
            if (c->ram_enabled) { c->rtc_live[c->rtc_sel - 0x08] = v; }
            return;
        }
        int32_t off = sram_offset(c, addr);
        if (off >= 0) {
            c->sram[off] = (c->mbc == MBC2) ? (uint8_t)(v & 0x0fU) : v;
            c->save_dirty++;
        }
    }
}
