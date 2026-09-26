#include "driver_core.h"
#include "ctrl.h"
#include "cable_id.h"
#include "pad_arena.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include <string.h>

enum { DRV_RQ_PROTO = 1U << 0, DRV_RQ_DRVCFG = 1U << 1, DRV_RQ_CABLE = 1U << 2 };

// core0-written counters, not a shared bitmask: a cross-core read-modify-write drops requests.
static volatile uint32_t g_seq_proto, g_seq_drv, g_seq_cable;

static pad_pub_t g_pad;
static pad_map_t g_map;

// core1 reads this snapshot, never g_config[] itself.
typedef struct { volatile uint32_t seq; uint8_t b[CFG_LEN]; } cfg_pub_t;
static cfg_pub_t g_cfg_pub;

static volatile uint32_t g_rq_ms;
static volatile bool     g_link_up;
static volatile int8_t   g_err;
static volatile uint32_t g_services, g_applies;
static const pad_driver_t *volatile g_owner;

static void cfg_publish(const uint8_t cfg[CFG_LEN]) {
    seq_write(&g_cfg_pub.seq, g_cfg_pub.b, cfg, CFG_LEN);
}

static bool cfg_snapshot(uint8_t out[CFG_LEN]) {
    return seq_read(&g_cfg_pub.seq, out, g_cfg_pub.b, CFG_LEN, NULL);
}

void drv_init_core0(const uint8_t cfg[CFG_LEN]) {
    map_compile(cfg, &g_map);
    cfg_publish(cfg);
    pad_zero(&g_pad.s);
    g_seq_proto++;                      // core1's first pass brings a driver up
}

void drv_pad_from_gb(uint8_t gb, uint32_t now_ms) {
    pad_state_t s;
    map_apply(&g_map, gb, &s);
    s.t_ms = now_ms;
    pad_publish(&g_pad, &s);
}

void drv_pad_publish(const pad_state_t *s) {
    pad_publish(&g_pad, s);
}

// core0 reads back its own write, so no seqlock; the $4600 controller picture lights from it.
uint32_t drv_pad_live(void) { return g_pad.s.buttons; }

void drv_pad_release(uint32_t now_ms) {
    pad_state_t s;
    pad_zero(&s);
    s.t_ms = now_ms;
    pad_publish(&g_pad, &s);
}

void drv_config_changed(const uint8_t cfg[CFG_LEN], uint8_t cls, uint32_t now_ms) {
    if (cls == CFG_CLASS_MAP) { map_compile(cfg, &g_map); return; }   // core0 only
    if (cls == CFG_CLASS_PROTO) {
        map_compile(cfg, &g_map); // each proto has its own map
    }
    cfg_publish(cfg);
    if (cls == CFG_CLASS_PROTO) {
        g_seq_proto++;
    } else if (cls == CFG_CLASS_DRV) {
        g_seq_drv++;
    } else {
        return;
    }
    g_rq_ms = now_ms;
}

void drv_cable_changed(uint32_t now_ms) {
    g_seq_cable++;
    g_rq_ms = now_ms;
}

bool drv_cable_ok(const uint8_t cfg[CFG_LEN]) {
#ifdef BENCH_PROTO
    (void)cfg;
    return true;    // never via g_config: config_save() would persist it into normal builds
#else
    return cable_id_slot() != ADAPT_NONE || (cfg_get(cfg, CFG_FLAGS, 0) & CFGF_ANY_CABLE);
#endif
}

bool        drv_link_up(void)       { return g_link_up; }
int8_t      drv_error(void)         { return g_err; }
uint32_t    drv_service_count(void) { return g_services; }
uint32_t    drv_apply_count(void)   { return g_applies; }
const char *drv_name(void) {
    const pad_driver_t *d = g_owner;
    return d ? d->name : "none";
}
uint8_t drv_resident_proto(void) {
    const pad_driver_t *d = g_owner;
    return d ? d->proto : PROTO_NONE;
}

// Everything below runs on core1.
static pad_err_t drv_check(const pad_driver_t *d) {
    return pad_res_check(&d->res);
}

static void drv_teardown(void) {
    const pad_driver_t *d = g_owner;
    if (!d) { return; }
    d->deinit(d);
    ctrl_release(d->res.ctrl_mask);     // a driver that forgets cannot leave a pin driven
    pad_arena_reset();
    g_owner = NULL;
    g_link_up = false;
}

static void drv_apply(uint32_t rq, const uint8_t cfg[CFG_LEN]) {
    const pad_driver_t *d = g_owner;

    if (rq == DRV_RQ_DRVCFG && d) {
        g_err = (int8_t)d->reconfig(d, cfg_drv_block(cfg, d->proto));
        return;
    }

    drv_teardown();

    uint8_t proto = cfg_proto_sel(cfg);
#ifndef BENCH_PROTO
    proto = cable_id_proto(cable_id_slot(), proto);     // core0 has not caught up: never start the wrong one
#endif
    d = pad_driver_for(proto);
    if (!d || (d->proto != PROTO_NONE && !d->res.ctrl_mask != proto_pinless(d->proto))) {
        g_err = PAD_ERR_CONFIG;
        return;
    }
    if (d->res.ctrl_mask && !drv_cable_ok(cfg)) { g_err = PAD_OK; return; }

    pad_err_t e = drv_check(d);
    if (e != PAD_OK) { g_err = (int8_t)e; return; }
    if (d->res.ctrl_mask && !ctrl_claim(d->res.ctrl_mask)) { g_err = PAD_ERR_UNSAFE; return; }

    e = d->init(d, cfg_drv_block(cfg, d->proto));
    if (e != PAD_OK) { ctrl_release(d->res.ctrl_mask); g_err = (int8_t)e; return; }

    g_owner = d;
    g_err = PAD_OK;
}

static void __not_in_flash_func(drv_core1_main)(void) {
    // Nothing may land inside a protocol frame; core1's NVIC only, and it rules out multicore_lockout.
    irq_set_mask_enabled(~0U, false);

    pio_set_gpio_base(pio2, 16);        // must precede any pio_add_program on pio2
    ctrl_init_safe();

    static uint8_t cfg[CFG_LEN];
    pad_state_t last;
    pad_zero(&last);
    uint32_t seen_proto = 0;
    uint32_t seen_drv = 0;
    uint32_t seen_cable = 0;

    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        uint32_t sp = g_seq_proto;
        uint32_t sd = g_seq_drv;
        uint32_t sc = g_seq_cable;
        uint32_t rq = (sp != seen_proto ? DRV_RQ_PROTO  : 0)
                    | (sd != seen_drv   ? DRV_RQ_DRVCFG : 0)
                    | (sc != seen_cable ? DRV_RQ_CABLE  : 0);
        // Coalesce: the UI writes config a byte at a time.
        if (rq && (int32_t)(now - g_rq_ms) > DRV_SETTLE_MS) {
            seen_proto = sp; seen_drv = sd; seen_cable = sc;
            // Bumped here rather than inside drv_apply() so every early-return path counts.
            if (cfg_snapshot(cfg)) { drv_apply(rq, cfg); g_applies++; }
        }

        pad_state_t s;
        if (pad_read(&g_pad, &s)) { last = s; }
        if ((int32_t)(now - last.t_ms) > PAD_STALE_MS && !(last.flags & PADF_RELEASED)) {
            pad_zero(&last); // fires continuously in config mode, which is correct
        }

        const pad_driver_t *d = g_owner;
        if (d) {
            d->service(d, &last, 1000);
            g_link_up = d->link_up(d);
            g_services++;
        } else {
            busy_wait_us_32(1000);
            g_link_up = false;
        }
    }
}

void drv_start(void) { multicore_launch_core1(drv_core1_main); }
