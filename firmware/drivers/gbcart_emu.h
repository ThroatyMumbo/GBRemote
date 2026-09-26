// gbcart_emu.h — an emulated Game Boy cartridge as pure host-testable logic. ROM arrives as two
// owner-staged 16 KB windows, which is what keeps core1 off flash.
#ifndef GBCART_EMU_H
#define GBCART_EMU_H

#include <stdint.h>
#include <stdbool.h>

enum { MBC_NONE = 0, MBC1, MBC2, MBC3, MBC5 };

#define GB_ROM_WINDOW 0x4000u
#define GB_SRAM_MAX   0x8000u    // 32 KB: MBC1/MBC3 ceiling, and every Pokemon generation
#define MBC2_RAM_LEN  0x0200u    // 512 x 4 bits, inside the MBC

typedef struct {
    uint8_t  mbc;
    bool     has_rtc;
    uint16_t rom_banks;          // total 16 KB banks in the image
    uint32_t ram_len;            // 0, MBC2_RAM_LEN, or a multiple of 8 KB up to GB_SRAM_MAX

    // GB $0000-$3FFF and $4000-$7FFF; MBC1 mode 1 moves the low one too.
    const uint8_t *win[2];
    uint16_t       win_bank[2];  // which ROM bank each window actually holds
    uint8_t       *sram;         // ram_len bytes, writable

    // The MBC registers as written, not as interpreted (MBC1's $4000 means two things by `mode`).
    uint8_t bank_lo;             // $2000-$3FFF
    uint8_t bank_hi;             // $4000-$5FFF
    uint8_t bank_b8;             // MBC5 only, $3000-$3FFF bit 8
    uint8_t mode;                // MBC1 banking mode: 0 = ROM, 1 = RAM
    bool    ram_enabled;

    uint32_t save_dirty;         // ++ per write into sram; the owner flushes when it stops moving

    uint8_t rtc_sel;             // $08-$0C selects a clock register instead of RAM
    uint8_t rtc_latched[5];      // S M H DL DH, frozen by the $6000 0->1 sequence
    uint8_t rtc_live[5];
    uint8_t rtc_latch_prev;
} gbcart_t;

void gbcart_reset(gbcart_t *c);
void gbcart_mbc_reset(gbcart_t *c);         // /RST: the registers only, the staged windows stay

// The ROM bank the owner must have resident in win[which] before the next read through it.
uint16_t gbcart_want_bank(const gbcart_t *c, unsigned which);

// What $A000-$BFFF maps to; a hardware serve path must call these, never re-derive them.
uint32_t gbcart_ram_bank(const gbcart_t *c);
bool     gbcart_rtc_mapped(const gbcart_t *c);

// One byte; `ready` is false when win[] does not hold the selected bank, so never serve it stale.
uint8_t gbcart_read (const gbcart_t *c, uint16_t addr, bool *ready);
void    gbcart_write(gbcart_t *c, uint16_t addr, uint8_t v);

// The Transfer Pak's forms: `addr` 32-byte aligned and `n` <= 32 never straddle a region, so they
// decode once and memcpy inside the Joybus turnaround.
bool gbcart_read_block (const gbcart_t *c, uint16_t addr, uint8_t *out, unsigned n);
void gbcart_write_block(gbcart_t *c, uint16_t addr, const uint8_t *in, unsigned n);

#endif
