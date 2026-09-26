// vis.c — the Tandy VIS controller over IR, Solo mode: the held set every 24 ms, one release frame
// on let-go. A changed set waits for the next slot; only the first press out of idle goes at once.
#include "pad_driver.h"
#include "config_map.h"
#include "pinmap.h"
#include "vis_proto.h"
#include "irdev.h"
#include "pico/time.h"

static bool     g_held;         // the last frame sent was a held set, so a release is owed
static uint32_t g_next_us;      // when the next frame is due

static pad_err_t vis_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d; (void)cfg;
    g_held = false;
    return irdev_init(PIN_IR_TX, VIS_CARRIER_HZ) ? PAD_OK : PAD_ERR_HW;
}

static void vis_deinit(const pad_driver_t *d) { (void)d; irdev_deinit(); }

static pad_err_t vis_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d; (void)cfg;
    return PAD_OK;
}

// There is no return path: a transmitter that is resident is as linked as it can be.
static bool vis_link_up(const pad_driver_t *d) { (void)d; return true; }

static void __not_in_flash_func(vis_service)(const pad_driver_t *d, const pad_state_t *s,
                                             uint32_t idle_us) {
    (void)d;
    uint16_t held = vis_held_from_pad(s);
    uint32_t now = time_us_32();
    bool due = (int32_t)(now - g_next_us) >= 0;

    if (held && (!g_held || due)) {
        irdev_send(vis_halves(vis_encode(held)), 2 * VIS_FRAME_BITS, VIS_HALF_US);
        g_next_us = now + VIS_SOLO_PERIOD_US;
        g_held = true;
    } else if (!held && g_held && due) {
        irdev_send(vis_halves(vis_encode(0)), 2 * VIS_FRAME_BITS, VIS_HALF_US);
        g_held = false;
    } else {
        uint32_t wait = idle_us;
        if (g_held && !due && g_next_us - now < wait) { wait = g_next_us - now; }
        busy_wait_us_32(wait);
    }
}

static const pad_target_t k_targets[] = {
    { ACT_PAD(PAD_A), "A" }, { ACT_PAD(PAD_B), "B" },
    { ACT_PAD(PAD_X), "1" }, { ACT_PAD(PAD_Y), "2" },
    { ACT_PAD(PAD_START), "3" }, { ACT_PAD(PAD_SELECT), "4" },
    PT_DPAD,
};

const pad_driver_t drv_vis = {
    .name = "vis",
    .label = "Tandy VIS",
    .proto = PROTO_VIS,
    PAD_TARGETS(k_targets),
    .res = { .ctrl_mask = 0 },
    .init = vis_init, .deinit = vis_deinit, .service = vis_service,
    .reconfig = vis_reconfig, .link_up = vis_link_up,
};
