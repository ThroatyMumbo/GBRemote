// cartserve.c — the serve path, shared by firmware/ and demos/.
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/address_mapped.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/powman.h"
#include <string.h>

#include "cartserve.h"
#include "pinmap.h"
#include "clock_cfg.h"
#include "serve.pio.h"

#define VREG_FROM_MV(mv) ((enum vreg_voltage)(VREG_VOLTAGE_1_00 + ((mv) - 1000) / 50))
_Static_assert(BOD_MV + 50 <= VREG_MV, "the brown-out threshold must sit below the rail");

#define SM_A15      0
#define SM_ROM_LOW  1
#define SM_ROM_HIGH 2           // shim_dma.c finds the high ring by this SM
#define SM_DATA     3

// Two static 16 KB banks, both permuted to the wired bit order at load.
static uint8_t __attribute__((aligned(0x4000))) low_bank[0x4000];    // $0000-$3FFF
static uint8_t __attribute__((aligned(0x4000))) high_bank[0x4000];   // $4000-$7FFF
// One aligned pair: cart_mbc.c's arm DMA writes both in a single 2-word transfer.
static volatile uint32_t __attribute__((aligned(8))) g_rom_base[2];
#define g_rom_base_low  g_rom_base[0]
#define g_rom_base_high g_rom_base[1]

volatile uint32_t *cs_rom_base(void) { return g_rom_base; }

void cs_bank_sel_arm(const uint32_t *tab_base, int *copy_ch, int *trig_ch) {
    uint off = pio_add_program(pio1, &bank_sel_program);
    bank_sel_program_init(pio1, CS_SM_BSEL, off);

    int e1 = dma_claim_unused_channel(true);
    int e2 = dma_claim_unused_channel(true);
    int e3 = dma_claim_unused_channel(true);
    dma_channel_config c;

    c = dma_channel_get_default_config(e2);          // tab[byte] -> both bases; the ring rewinds
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 3);
    channel_config_set_high_priority(&c, true);
    channel_config_set_chain_to(&c, e1);
    dma_channel_set_trans_count(e2, 2, false);
    dma_channel_set_write_addr(e2, g_rom_base, false);
    dma_channel_set_config(e2, &c, false);

    c = dma_channel_get_default_config(e1);          // the byte, pre-shifted by bank_sel
    channel_config_set_read_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio1, CS_SM_BSEL, false));
    channel_config_set_high_priority(&c, true);
    channel_config_set_chain_to(&c, e3);
    dma_channel_set_trans_count(e1, 1, false);
    dma_channel_set_read_addr(e1, &pio1->rxf[CS_SM_BSEL], false);
    dma_channel_set_write_addr(e1, &dma_hw->ch[e2].read_addr, false);
    dma_channel_set_config(e1, &c, true);

    c = dma_channel_get_default_config(e3);          // OR in the table base, and go
    channel_config_set_read_increment(&c, false);
    channel_config_set_high_priority(&c, true);
    dma_channel_set_trans_count(e3, 1, false);
    dma_channel_set_read_addr(e3, tab_base, false);
    dma_channel_set_write_addr(e3, hw_set_alias_untyped(&dma_hw->ch[e2].al3_read_addr_trig), false);
    dma_channel_set_config(e3, &c, false);

    if (copy_ch) { *copy_ch = e2; }
    if (trig_ch) { *trig_ch = e3; }
    pio_sm_set_enabled(pio1, CS_SM_BSEL, true);
}

const uint8_t cs_wire_pos[14] = A0_13_WIRE_POS;

uint16_t cs_permute14(uint16_t a) {
    uint16_t w = 0;
    for (int i = 0; i < 14; i++) {
        if (a & (1U << i)) { w |= (1U << cs_wire_pos[i]); }
    }
    return w;
}

uint8_t *cs_bank(int high) { return high ? high_bank : low_bank; }

// cs_permute14() costs ~0.5 us a byte; a bit permutation separates, perm(a) = lo[a & 0xff] | hi[a >> 8].
static uint16_t g_perm_lo[256], g_perm_hi[64];      // an address is 14 bits: the high byte is 6

static void cs_perm_init(void) {
    for (uint32_t i = 0; i < 256; i++) { g_perm_lo[i] = cs_permute14((uint16_t)i); }
    for (uint32_t i = 0; i < 64; i++) { g_perm_hi[i] = cs_permute14((uint16_t)(i << 8)); }
}

// No CLK gate: the caller owns a bank nothing is serving. Wraps at the 16 KB boundary, as the
// bank does.
void cs_write_span(uint8_t *bank, uint16_t addr, const uint8_t *src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint16_t a = (uint16_t)((addr + i) & 0x3fffU);
        bank[g_perm_lo[a & 0xffU] | g_perm_hi[a >> 8]] = src[i];
    }
}

void cs_load(uint8_t *dst, const uint8_t *img, uint32_t len) {
    memset(dst, 0xff, 0x4000);
    for (uint32_t a = 0; a < 0x4000 && a < len; a++) { dst[cs_permute14((uint16_t)a)] = img[a]; }
}

// Nothing reads the bank yet: the ring is not built and the console is held.
void cs_seed(uint16_t addr, uint8_t v) { low_bank[cs_permute14(addr)] = v; }

// Croco's setup_read_dma_method2: d1 moves the address into d2.read_addr, d3 SET-ORs the bank base
// and triggers d2, d2 sends the byte to the data SM and re-arms d1.
static void setup_ring(PIO pio, uint sm_addr, PIO dpio, uint sm_data,
                       volatile uint32_t *base) {
    int d1 = dma_claim_unused_channel(true);
    int d2 = dma_claim_unused_channel(true);
    int d3 = dma_claim_unused_channel(true);
    dma_channel_config c;

    c = dma_channel_get_default_config(d2);
    channel_config_set_read_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_high_priority(&c, true);
    channel_config_set_chain_to(&c, d1);
    dma_channel_set_trans_count(d2, 1, false);
    dma_channel_set_write_addr(d2, &dpio->txf[sm_data], false);
    dma_channel_set_config(d2, &c, false);

    c = dma_channel_get_default_config(d1);
    channel_config_set_read_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_addr, false));
    channel_config_set_high_priority(&c, true);
    channel_config_set_chain_to(&c, d3);
    dma_channel_set_trans_count(d1, 1, false);
    dma_channel_set_read_addr(d1, &pio->rxf[sm_addr], false);
    dma_channel_set_write_addr(d1, &dma_hw->ch[d2].read_addr, false);
    dma_channel_set_config(d1, &c, true);          // arm (waits on DREQ)

    c = dma_channel_get_default_config(d3);
    channel_config_set_read_increment(&c, false);
    channel_config_set_high_priority(&c, true);
    dma_channel_set_trans_count(d3, 1, false);
    dma_channel_set_read_addr(d3, base, false);
    dma_channel_set_write_addr(
        d3, hw_set_alias_untyped(&dma_hw->ch[d2].al3_read_addr_trig), false);
    dma_channel_set_config(d3, &c, false);
}

// An HDMA read never strobes A15, so a15_detect fires it on a delay from the CLK rise: past the
// 127 ns where the DMA's address settles, far short of a double-speed read's ~345 ns latch.
#define HDMA_NS   150u
#define HDMA_LEAD 10      // cycles ahead of the delay: 2 sync, the rise, 5 instructions, jmp pin, irq

static const struct pio_program *a15_detect_timed(void) {
    static uint16_t ins[32];
    static struct pio_program p;
    int d = (int)(clock_get_hz(clk_sys) / 1000000U * HDMA_NS / 1000U) - HDMA_LEAD;
    if (d < 0) { d = 0; }
    if (d > 31) { d = 31; }
    for (uint i = 0; i < a15_detect_program.length; i++) {
        ins[i] = a15_detect_program.instructions[i];
    }
    ins[a15_detect_offset_hdma] =
        (uint16_t)((ins[a15_detect_offset_hdma] & ~0x1f00U) | pio_encode_delay((uint)d));
    p = a15_detect_program;
    p.instructions = ins;
    return &p;
}

// BOD first, since out of reset it trips at 0.946 V; a lower rail waits for the lower clock.
void cs_set_operating_point(void) {
    powman_hw->bod = POWMAN_PASSWORD_BITS | (uint32_t)((BOD_MV - 473 + 21) / 43) << POWMAN_BOD_VSEL_LSB
                   | POWMAN_BOD_EN_BITS;
    enum vreg_voltage v = VREG_FROM_MV(VREG_MV);
    if (v > VREG_VOLTAGE_MAX) { v = VREG_VOLTAGE_MAX; }
    bool lower = v < vreg_get_voltage();
    if (!lower) { vreg_set_voltage(v); sleep_ms(2); }
    // required=false: an unachievable clock falls back rather than panics; SWD reaches it either way.
    if (!set_sys_clock_khz(SYSCLK_KHZ, false)) { set_sys_clock_khz(150000, false); }
    if (lower) { vreg_set_voltage(v); sleep_ms(2); }
}

void cs_bus_priority(void) {
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS
                          | BUSCTRL_BUS_PRIORITY_PROC0_BITS;
}

// HIGH on the FET gate holds the console in reset. The gate's 10k pull-up already holds it from
// cold power-up; asserting makes that independent of the pad reset state.
#define RSTHOLD_FAILSAFE_MS 500

void cs_rsthold_assert(void) {
    gpio_init(PIN_RSTHOLD);
    gpio_put(PIN_RSTHOLD, 1);
    gpio_set_dir(PIN_RSTHOLD, GPIO_OUT);
}
void cs_rsthold_release(void) { gpio_put(PIN_RSTHOLD, 0); }

// Unconditional: a hang in init degrades to a dead bus, never a console that looks broken.
static int64_t rsthold_failsafe(alarm_id_t id, void *user) {
    (void)id; (void)user;
    cs_rsthold_release();
    return 0;
}
void cs_rsthold_arm_failsafe(void) { add_alarm_in_ms(RSTHOLD_FAILSAFE_MS, rsthold_failsafe, NULL, true); }

void cs_start(void) {
    cs_perm_init();
    g_rom_base_low  = (uint32_t)low_bank;
    g_rom_base_high = (uint32_t)high_bank;

    const uint8_t in_pins[] = { PIN_CLK, PIN_NWR, PIN_NRD, PIN_NCS,
                                PIN_A14, PIN_A15, PIN_RST };
    for (uint i = 0; i < sizeof(in_pins); i++) {
        gpio_init(in_pins[i]); gpio_set_dir(in_pins[i], GPIO_IN);
    }
    for (uint p = PIN_A0; p < PIN_A0 + 14; p++) { gpio_init(p); gpio_set_dir(p, GPIO_IN); }

    uint off_region = pio_add_program(pio0, a15_detect_timed());
    uint off_low    = pio_add_program(pio0, &rom_low_program);
    uint off_high   = pio_add_program(pio0, &rom_high_program);
    uint off_data   = pio_add_program(pio0, &serve_data_program);
    a15_detect_program_init   (pio0, SM_A15,   off_region);
    rom_low_program_init      (pio0, SM_ROM_LOW,  off_low);
    rom_high_program_init     (pio0, SM_ROM_HIGH, off_high);
    serve_data_program_init   (pio0, SM_DATA,     off_data);

    uint off_wcap = pio_add_program(pio1, &write_capture_program);
    write_capture_program_init(pio1, CS_SM_WCAP, off_wcap);

    setup_ring(pio0, SM_ROM_LOW,  pio0, SM_DATA, &g_rom_base_low);
    setup_ring(pio0, SM_ROM_HIGH, pio0, SM_DATA, &g_rom_base_high);

    pio_sm_set_enabled(pio0, SM_A15,   true);
    pio_sm_set_enabled(pio0, SM_ROM_LOW,  true);
    pio_sm_set_enabled(pio0, SM_ROM_HIGH, true);
    pio_sm_set_enabled(pio0, SM_DATA,     true);
    pio_sm_set_enabled(pio1, CS_SM_WCAP,  true);
}

// The gate is harmless, not needed (hw-test/dspeed: 11-60M ungated writes, zero errors). It waits for
// a rising edge, not the level, so back-to-back pokes cannot walk into the other phase.
void cs_poke_bank(uint8_t *base, uint16_t addr, uint8_t v) {
    uint32_t spin = 0;
    while ((sio_hw->gpio_in & (1U << PIN_CLK)) && ++spin < 20000U) { tight_loop_contents(); }
    while (!(sio_hw->gpio_in & (1U << PIN_CLK)) && ++spin < 20000U) { tight_loop_contents(); }
    base[cs_permute14(addr)] = v;
}

void cs_poke(uint16_t addr, uint8_t v) { cs_poke_bank(low_bank, addr, v); }

// d3 re-reads the base on every served byte, so one store moves the whole bank.
void cs_mount_high(const uint8_t *bank16k) {
    g_rom_base_high = (uint32_t)bank16k;
    __dmb();
}

// One latched word per write cycle: GP6->bit0 .. GP31->bit25 (serve.pio).
bool cs_wcap_decode(uint32_t w, uint16_t *addr, uint8_t *data) {
    if (w & (1U << 1)) {
        return false; // A15: internal space
    }
    uint32_t cap = (w >> 12) & 0x3FFF;                 // A0-A13 = GP18-31 = bits12-25
    uint16_t a = (uint16_t)((w & 1U) << 14);           // A14 = GP6 = bit0
    for (int i = 0; i < 14; i++) { a |= (uint16_t)(((cap >> cs_wire_pos[i]) & 1U) << i); }
    *addr = a;
    *data = (uint8_t)((w >> 2) & 0xFF);                // D0-D7 = GP8-15 = bits2-9
    return true;
}
