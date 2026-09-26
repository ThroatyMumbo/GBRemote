// cart_mbc.c — cart_bigrom.c's bank_sel arm without the stall bank; the table is built once.
#include "hardware/sync.h"

#include "cartserve.h"
#include "cart_mbc.h"
#include "gbcart_emu.h"

#define MBC_MAX_BANKS 16

// Indexed by bank_sel's `data << 3`, up to 0x7F8: 2 KB alignment makes the SET-OR an add.
static uint32_t __attribute__((aligned(2048))) g_tab[256][2];
static uint32_t g_tab_base;
static const uint8_t *g_mbc_banks[MBC_MAX_BANKS];
static const uint8_t *g_mbc_prev[MBC_MAX_BANKS];    // what a remap replaced, until the next select
static unsigned g_nbanks;

void cs_mbc_start(uint8_t *const *banks, unsigned n) {
    gbcart_t t;
    gbcart_reset(&t);
    t.mbc = MBC5;
    t.rom_banks = (uint16_t)n;
    g_nbanks = n < MBC_MAX_BANKS ? n : MBC_MAX_BANKS;
    for (unsigned b = 0; b < g_nbanks; b++) { g_mbc_banks[b] = banks[b]; }
    for (unsigned v = 0; v < 256; v++) {
        gbcart_write(&t, 0x2000, (uint8_t)v);
        g_tab[v][0] = (uint32_t)banks[gbcart_want_bank(&t, 0)];
        g_tab[v][1] = (uint32_t)banks[gbcart_want_bank(&t, 1)];
    }
    g_tab_base = (uint32_t)g_tab;

    volatile uint32_t *base = cs_rom_base();
    base[0] = (uint32_t)banks[0];
    base[1] = (uint32_t)banks[1];
    __dmb();

    cs_bank_sel_arm(&g_tab_base, NULL, NULL);
}

void cs_mbc_reset(void) {
    cs_rom_base()[1] = (uint32_t)g_mbc_banks[1];
    __dmb();
}

int cs_mbc_bank(void) {
    uint32_t hi = cs_rom_base()[1];
    for (unsigned b = 0; b < g_nbanks; b++) {
        if (hi == (uint32_t)g_mbc_banks[b] || hi == (uint32_t)g_mbc_prev[b]) { return (int)b; }
    }
    return -1;
}

// Word stores, so the DMA reads either the old entry or the new one, never half of each.
void cs_mbc_remap(unsigned bank, const uint8_t *buf) {
    if (bank >= g_nbanks || buf == g_mbc_banks[bank]) { return; }
    uint32_t was = (uint32_t)g_mbc_banks[bank];
    for (unsigned v = 0; v < 256; v++) {
        for (unsigned h = 0; h < 2; h++) {
            if (g_tab[v][h] == was) { g_tab[v][h] = (uint32_t)buf; }
        }
    }
    g_mbc_prev[bank]  = g_mbc_banks[bank];
    g_mbc_banks[bank] = buf;
    __dmb();
}
