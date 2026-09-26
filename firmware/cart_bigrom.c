// cart_bigrom.c — bigrom on cartserve: cart_mbc.c's arm, with a table that sends a non-resident
// bank to the trap bank.
#include <string.h>

#include "hardware/dma.h"
#include "hardware/sync.h"

#include "pinmap.h"
#include "cartserve.h"
#include "cart_bigrom.h"
#include "gbcart_emu.h"

#define CBR_MAX_BANKS 512               // MBC5's own ceiling; bank_sel ignores A12 so $3000 is out

// Indexed by bank_sel's `data << 3`, up to 0x7F8: 2 KB alignment makes the SET-OR an add.
static uint32_t __attribute__((aligned(2048))) g_tab[256][2];
static uint32_t g_tab_base;
static bigrom_t g_br;
static int      g_e2, g_e3;
static const uint8_t *g_img;
static uint16_t g_nbanks;
static bool     g_identity;             // A0_13_WIRE_POS: a fill is a memcpy, or a per-byte loop
static void   (*g_pump)(void);

// Select decode, cached on every register but bank_lo: rebuilding it per fill cost 163 us of a 540 us freeze.
static uint16_t g_sel[256][2];
static uint32_t g_bankptr[CBR_MAX_BANKS];
static uint32_t g_sel_key = 0xFFFFFFFFu;

static uint16_t br_perm(uint16_t off) { return cs_permute14(off); }

static void br_poke(uint8_t *base, uint16_t off, uint8_t v) {
    if (g_pump) g_pump();               // a CLK period each, and a release is a dozen of them
    cs_poke_bank(base, off, v);
}

static void br_fill(uint8_t *dst, uint16_t bank) {
    const uint8_t *src = g_img + (uint32_t)bank * 0x4000u;
    if (g_identity) memcpy(dst, src, 0x4000u);
    else            cs_load(dst, src, 0x4000u);
}

// A whole-bank DMA holds the fabric 360 us and the serve DMA misses its 113 ns; 4 KB pieces cost nothing.
#define CBR_CHUNK 4096u
static uint32_t g_fc_left;
static uint8_t *g_fc_dst;
static const uint8_t *g_fc_src;

static void br_fill_start(uint8_t *dst, uint16_t bank) {
    if (!g_identity) { br_fill(dst, bank); return; }    // the permuting form stays synchronous
    g_fc_dst  = dst;
    g_fc_src  = g_img + (uint32_t)bank * 0x4000u;
    g_fc_left = 0x4000u;
}

static bool br_fill_step(void) {
    if (!g_fc_left) return false;
    uint32_t n = g_fc_left < CBR_CHUNK ? g_fc_left : CBR_CHUNK;
    memcpy(g_fc_dst, g_fc_src, n);
    g_fc_dst  += n;
    g_fc_src  += n;
    g_fc_left -= n;
    return true;
}

static void br_mount(const uint8_t *low, const uint8_t *high) {
    volatile uint32_t *base = cs_rom_base();
    base[0] = (uint32_t)low;
    base[1] = (uint32_t)high;
    __dmb();
}

// Replay a select through the arm's chain. Enter at e3: e2 chains back to e1, so a forced e1
// re-fires forever. Only with the console parked.
static void br_remount(uint8_t byte) {
    dma_hw->ch[g_e2].read_addr = (uint32_t)byte << 3;
    dma_channel_start(g_e3);
    while (dma_channel_is_busy(g_e2) || dma_channel_is_busy(g_e3)) tight_loop_contents();
}

static inline uint32_t br_sel_state(const gbcart_t *c) {
    return (uint32_t)c->mbc | ((uint32_t)c->bank_hi << 8) | ((uint32_t)c->bank_b8 << 16)
         | ((uint32_t)c->mode << 24);
}

// The arm DMA's table, built from gbcart_emu so the hardware and core1 agree about the MBC.
static void br_remap(void) {
    uint32_t key = br_sel_state(&g_br.cart);
    if (key != g_sel_key) {
        gbcart_t t = g_br.cart;         // a $2000 write only overwrites bank_lo: one copy, not 256
        for (unsigned v = 0; v < 256; v++) {
            gbcart_write(&t, 0x2000, (uint8_t)v);
            g_sel[v][0] = gbcart_want_bank(&t, 0);
            g_sel[v][1] = gbcart_want_bank(&t, 1);
        }
        g_sel_key = key;
    }
    uint32_t trap = (uint32_t)g_br.cfg.trap;
    for (unsigned b = 0; b < g_nbanks; b++) g_bankptr[b] = trap;
    for (unsigned s = 0; s < g_br.cfg.nslots; s++) {
        int32_t b = g_br.slot_bank[s];
        if (b >= 0 && b < (int32_t)g_nbanks) g_bankptr[b] = (uint32_t)bigrom_slot_ptr(&g_br, (int)s);
    }
    for (unsigned v = 0; v < 256; v++) {
        uint16_t b0 = g_sel[v][0], b1 = g_sel[v][1];
        uint32_t p0 = b0 < g_nbanks ? g_bankptr[b0] : trap;
        uint32_t p1 = b1 < g_nbanks ? g_bankptr[b1] : trap;
        // Either half missing traps both: the park is in the low region.
        bool miss = p0 == trap || p1 == trap;
        g_tab[v][0] = miss ? trap : p0;
        g_tab[v][1] = miss ? trap : p1;
    }
}

void cs_bigrom_start(const uint8_t *img, uint16_t banks, uint8_t mbc, bool has_rtc,
                     uint8_t *const *slots, uint8_t nslots, uint8_t *trap,
                     int (*late_vector)(void), void (*pump)(void)) {
    g_img    = img;
    g_nbanks = banks > CBR_MAX_BANKS ? CBR_MAX_BANKS : banks;
    g_pump   = pump;
    g_identity = true;
    for (int i = 0; i < 14; i++) if (cs_wire_pos[i] != i) g_identity = false;

    bigrom_cfg_t cfg = {
        .img = img, .banks = g_nbanks, .mbc = mbc, .has_rtc = has_rtc,
        .slot = slots, .nslots = nslots, .trap = trap, .fill_us = 0,
        .perm = br_perm, .poke = br_poke, .fill = br_fill,
        .fill_start = br_fill_start, .fill_step = br_fill_step,
        .remap = br_remap, .mount = br_mount, .remount = br_remount,
        .late_vector = late_vector,
    };
    bigrom_init(&g_br, &cfg);           // loads banks 0 and 1, fills the trap
    br_remap();
    br_mount(g_br.base[0], g_br.base[1]);

    g_tab_base = (uint32_t)g_tab;
    cs_bank_sel_arm(&g_tab_base, &g_e2, &g_e3);
}

// A DMA mount survives /RST and a real MBC does not: it comes up with bank 1 high.
void cs_bigrom_reset(void) {
    g_sel_key = 0xFFFFFFFFu;            // mode and the upper bank bits moved: rebuild the decode
    bigrom_reset(&g_br);                // keeps the trap and the cache; bigrom_reset() says why
    br_mount(g_br.base[0], g_br.base[1]);
}

// NULL on a permuted wire order, where no slot holds plain image bytes.
const uint8_t *cs_bigrom_bank_ptr(void *user, uint16_t bank) {
    (void)user;
    // Called per ROM byte, so one index rather than a slot scan.
    if (!g_identity || bank >= g_nbanks) return NULL;
    uint32_t p = g_bankptr[bank];
    return p == (uint32_t)g_br.cfg.trap ? NULL : (const uint8_t *)p;
}

void cs_bigrom_write(uint16_t addr, uint8_t v, uint32_t now_us) {
    bigrom_write(&g_br, addr, v, now_us);
}
void cs_bigrom_read(uint16_t addr, uint32_t now_us) { bigrom_read(&g_br, addr, now_us); }
void cs_bigrom_tick(uint32_t now_us) { bigrom_tick(&g_br, now_us); }
void cs_bigrom_lagging(bool behind) { g_br.lagging = behind; }
bool cs_bigrom_frozen(void) { return bigrom_frozen(&g_br); }
const bigrom_t *cs_bigrom_stats(void) { return &g_br; }
