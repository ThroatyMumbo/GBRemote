// genesis.c — the Genesis / Mega Drive driver: a 6-button pad (HSS-0155 class), or 3-button with
// GNF_THREE_BUTTON.
#include "pad_driver.h"
#include "config_map.h"
#include "pinmap.h"
#include "gendev.h"
#include "gendev.pio.h"
#include "devcommon.h"
#include "gen_proto.h"
#include "pico/time.h"

static uint8_t g_flags;
static uint8_t g_deadzone = GEN_DEADZONE_DEFAULT;
static uint32_t g_last_poll_ms;

#ifdef BENCH_PROTO
// Bench calibration: raw wire words bypassing the mapping, so the console reports the bit it sees.
static volatile uint32_t g_force_d, g_force_t;
static volatile bool     g_force_on;
void gen_bench_force(uint32_t dpad, uint32_t tltr, bool on) {
    g_force_d = dpad; g_force_t = tltr; g_force_on = on;
}

// TH edge rate tells the read loop apart: 3-button is ~120 edges/s, the 6-button walk ~960/s.
void gen_bench_stats(uint32_t *polls, uint32_t *resyncs, uint32_t *drops) {
    const gendev_stats_t *s = gendev_stats();
    *polls = s->polls; *resyncs = s->resyncs; *drops = s->drops;
}
#define GEN_APPLY_FORCE(r)  do { if (g_force_on) { (r).dpad = g_force_d; (r).tltr = g_force_t; } } while (0)
#else
#define GEN_APPLY_FORCE(r)  ((void)0)
#endif

static void gen_load_cfg(const uint8_t *cfg) {
    g_flags    = cfg_get(cfg, GNC_FLAGS, 0);
    g_deadzone = cfg_get_nz(cfg, GNC_DEADZONE, GEN_DEADZONE_DEFAULT);
    gendev_set_reset_us((uint32_t)cfg_get(cfg, GNC_RESET_100US, 0) * 100U); // 0: gendev's default
    gendev_set_pullup((g_flags & GNF_PULLUP) != 0);
}

static pad_err_t gen_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    g_last_poll_ms = 0;
    // Before init, not in gen_load_cfg(): the drive mode picks the program and the pin priming.
    gendev_set_open_drain((cfg_get(cfg, GNC_FLAGS, 0) & GNF_OPEN_DRAIN) != 0);
    if (!gendev_init(pio2, PIN_GEN_TL, PIN_GEN_TH, PIN_GEN_D0)) { return PAD_ERR_NO_RESOURCE; }
    gen_load_cfg(cfg);      // after init: the reset window and the pulls are gendev's to hold
    return PAD_OK;
}

static void gen_deinit(const pad_driver_t *d) { (void)d; gendev_deinit(); }

static pad_err_t gen_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    if ((cfg_get(cfg, GNC_FLAGS, 0) ^ g_flags) & GNF_OPEN_DRAIN) {
        gen_deinit(d);
        return gen_init(d, cfg);
    }
    gen_load_cfg(cfg);
    return PAD_OK;
}

static bool gen_link_up(const pad_driver_t *d) {
    (void)d;
    return pad_link_recent(g_last_poll_ms, to_ms_since_boot(get_absolute_time()));
}

// Push on every pass, never only when empty: it keeps the served report microseconds fresh.
static void __not_in_flash_func(gen_service)(const pad_driver_t *d, const pad_state_t *s,
                                             uint32_t idle_us) {
    (void)d;
    gen_report_t r;
    gen_from_pad(s, g_flags, g_deadzone, &r);
    GEN_APPLY_FORCE(r);
    gendev_put(r.dpad, r.tltr);
    if (gendev_service(idle_us)) { g_last_poll_ms = to_ms_since_boot(get_absolute_time()); }
}

#define GEN_CTRL_MASK 0x7fu                             // CTRL0-6
#define GEN_CTRL_OUT  (GEN_CTRL_MASK & ~(1u << 2))      // everything but TH

// gen_proto.c's projection: TL low = PAD_Y, TL high = PAD_B, TR high = PAD_A, X/Y/Z = L/X/R.
static const pad_target_t k_targets[] = {
    { ACT_PAD(PAD_Y), "A" }, { ACT_PAD(PAD_B), "B" }, { ACT_PAD(PAD_A), "C" },
    { ACT_PAD(PAD_L), "X" }, { ACT_PAD(PAD_X), "Y" }, { ACT_PAD(PAD_R), "Z" },
    { ACT_PAD(PAD_START), "Start" }, { ACT_PAD(PAD_SELECT), "Mode" },
    PT_DPAD,
};

const pad_driver_t drv_genesis = {
    .name = "genesis",
    .label = "Genesis",
    .proto = PROTO_GENESIS,
    PAD_TARGETS(k_targets),
    .res = {
        .ctrl_mask = GEN_CTRL_MASK,
        .ctrl_out_mask = GEN_CTRL_OUT,  // push-pull unless GNF_OPEN_DRAIN
        .pio_sms = 2,
        .pio_words = PIO_WORDS(gen_dpad) + PIO_WORDS(gen_tltr),
        .dma_chans = 0,
        .logic_5v = 1,                  // TH arrives at 5 V and the data lines pull up to it
        .needs_5v = 1,
    },
    .init = gen_init, .deinit = gen_deinit, .service = gen_service,
    .reconfig = gen_reconfig, .link_up = gen_link_up,
};
