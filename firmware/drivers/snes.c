// snes.c — the SNES and NES pad drivers: one protocol at two widths. Only one is ever resident,
// so they share the statics below.
#include "pad_driver.h"
#include "config_map.h"
#include "pinmap.h"
#include "snesdev.h"
#include "snesdev.pio.h"
#include "devcommon.h"
#include "snes_proto.h"
#include "pico/time.h"

static uint8_t  g_flags;
static uint8_t  g_deadzone = SNES_DEADZONE_DEFAULT;
static uint32_t g_last_latch_ms;

#ifdef BENCH_PROTO
// Bench calibration: a raw wire word bypassing the mapping, so the console reports the bit it sees.
static volatile uint32_t g_force;
static volatile bool     g_force_on;
void snes_bench_force(uint32_t w, bool on) { g_force = w; g_force_on = on; }
#define SNES_APPLY_FORCE(w)  do { if (g_force_on) (w) = g_force; } while (0)
#else
#define SNES_APPLY_FORCE(w)  ((void)0)
#endif

static void snes_load_cfg(const uint8_t *cfg) {
    g_flags    = cfg_get(cfg, SNC_FLAGS, 0);
    g_deadzone = cfg_get_nz(cfg, SNC_DEADZONE, SNES_DEADZONE_DEFAULT);
}

static pad_err_t snes_start(const uint8_t *cfg, unsigned nbits) {
    snes_load_cfg(cfg);
    g_last_latch_ms = 0;
    return snesdev_init(pio2, PIN_SNES_DATA1, PIN_SNES_LATCH, PIN_SNES_CLK, nbits)
         ? PAD_OK : PAD_ERR_NO_RESOURCE;
}

static pad_err_t snes_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    return snes_start(cfg, SNES_BITS);
}

static pad_err_t nes_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    return snes_start(cfg, NES_BITS);
}

static void snes_deinit(const pad_driver_t *d) { (void)d; snesdev_deinit(); }

static pad_err_t snes_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    snes_load_cfg(cfg);
    return PAD_OK;
}

static bool snes_link_up(const pad_driver_t *d) {
    (void)d;
    return pad_link_recent(g_last_latch_ms, to_ms_since_boot(get_absolute_time()));
}

// Push on every pass, never only when empty: it keeps the latched report ~1 ms old, not a frame.
static void __not_in_flash_func(snes_service)(const pad_driver_t *d, const pad_state_t *s,
                                              uint32_t idle_us) {
    (void)d;
    uint32_t w = snes_from_pad(s, g_flags, g_deadzone);
    SNES_APPLY_FORCE(w);
    snesdev_put_report(w);
    if (snesdev_wait_latch(idle_us)) { g_last_latch_ms = to_ms_since_boot(get_absolute_time()); }
}

static void __not_in_flash_func(nes_service)(const pad_driver_t *d, const pad_state_t *s,
                                             uint32_t idle_us) {
    (void)d;
    snesdev_put_report(nes_from_pad(s, g_flags, g_deadzone));
    if (snesdev_wait_latch(idle_us)) { g_last_latch_ms = to_ms_since_boot(get_absolute_time()); }
}

// DATA2 (CTRL1) and IOBIT (CTRL4) stay unclaimed: the console's pull-up already reads DATA2 as an
// absent joypad 3, and a future multitap driver can claim them.
#define SNES_CTRL_MASK ((1u << 0) | (1u << 2) | (1u << 3))      // DATA1, LATCH, CLK

static const pad_target_t k_snes_targets[] = {
    { ACT_PAD(PAD_A), "A" },  { ACT_PAD(PAD_B), "B" },
    { ACT_PAD(PAD_X), "X" },  { ACT_PAD(PAD_Y), "Y" },
    { ACT_PAD(PAD_L), "L" },  { ACT_PAD(PAD_R), "R" },
    { ACT_PAD(PAD_SELECT), "Select" }, { ACT_PAD(PAD_START), "Start" },
    PT_DPAD,
};

static const pad_target_t k_nes_targets[] = {
    { ACT_PAD(PAD_A), "A" },  { ACT_PAD(PAD_B), "B" },
    { ACT_PAD(PAD_SELECT), "Select" }, { ACT_PAD(PAD_START), "Start" },
    PT_DPAD,
};

const pad_driver_t drv_snes = {
    .name = "snes",
    .label = "SNES",
    .proto = PROTO_SNES,
    PAD_TARGETS(k_snes_targets),
    .res =
        {
            .ctrl_mask = SNES_CTRL_MASK,
            .ctrl_out_mask = 1U << 0, // DATA1 only, and open-drain: we never source the high
            .pio_sms = 1,
            .pio_words = PIO_WORDS(snesdev),
            .dma_chans = 0,
            .logic_5v = 1, // the port's LATCH/CLK arrive at 5 V
            .needs_5v = 1,
        },
    .init = snes_init,
    .deinit = snes_deinit,
    .service = snes_service,
    .reconfig = snes_reconfig,
    .link_up = snes_link_up,
};

const pad_driver_t drv_nes = {
    .name = "nes",
    .label = "NES",
    .proto = PROTO_NES,
    PAD_TARGETS(k_nes_targets),
    .res =
        {
            .ctrl_mask = SNES_CTRL_MASK, // NES port is the same three lines: LATCH, CLK, D0
            .ctrl_out_mask = 1U << 0,
            .pio_sms = 1,
            .pio_words = PIO_WORDS(snesdev),
            .dma_chans = 0,
            .logic_5v = 1,
            .needs_5v = 1,
        },
    .init = nes_init,
    .deinit = snes_deinit,
    .service = nes_service,
    .reconfig = snes_reconfig,
    .link_up = snes_link_up,
};
