// snes_mouse.c — drv_snes_mouse: the GB's D-pad drives an SNES Mouse pointer, A/B are its buttons.
#include "pad_driver.h"
#include "config_map.h"
#include "pinmap.h"
#include "snesmouse.h"
#include "snesmouse.pio.h"
#include "devcommon.h"
#include "snesmouse_proto.h"
#include "pico/time.h"
#include "hardware/timer.h"

static smouse_t g_m;
static uint32_t g_last_latch_ms, g_last_us;

static pad_err_t mouse_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d; (void)cfg;
    smouse_reset(&g_m);
    g_last_latch_ms = 0;
    g_last_us = timer_hw->timerawl;
    if (!snesmouse_init(pio2, PIN_SNES_DATA1, PIN_SNES_LATCH, PIN_SNES_CLK)) {
        return PAD_ERR_NO_RESOURCE;
    }
    snesmouse_put(smouse_report(&g_m));
    return PAD_OK;
}

static void mouse_deinit(const pad_driver_t *d) { (void)d; snesmouse_deinit(); }

static pad_err_t mouse_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d; (void)cfg;
    return PAD_OK;
}

static bool mouse_link_up(const pad_driver_t *d) {
    (void)d;
    return pad_link_recent(g_last_latch_ms, to_ms_since_boot(get_absolute_time()));
}

// Pushed on every pass like snes_service; the echo accounts for whichever word was served.
static void __not_in_flash_func(mouse_service)(const pad_driver_t *d, const pad_state_t *s,
                                               uint32_t idle_us) {
    (void)d;
    uint32_t now = timer_hw->timerawl;
    smouse_tick(&g_m, s->buttons, now - g_last_us);
    g_last_us = now;

    uint32_t w;
    while (snesmouse_rx(&w)) { smouse_rx(&g_m, w); }
    snesmouse_put(smouse_report(&g_m));

    if (snesmouse_wait_latch(idle_us)) { g_last_latch_ms = to_ms_since_boot(get_absolute_time()); }
}

static const pad_target_t k_targets[] = {
    { ACT_PAD(PAD_A), "L Click" }, { ACT_PAD(PAD_B), "R Click" }, { ACT_PAD(PAD_SELECT), "Speed" },
    PT_DPAD,
};

const pad_driver_t drv_snes_mouse = {
    .name = "snesmouse",
    .label = "SNES Mouse",
    .proto = PROTO_SNES_MOUSE,
    PAD_TARGETS(k_targets),
    .res =
        {
            .ctrl_mask = (1U << 0) | (1U << 2) | (1U << 3), // DATA1, LATCH, CLK: snes.c's mask
            .ctrl_out_mask = 1U << 0,
            .pio_sms = 1,
            .pio_words = PIO_WORDS(snesmouse),
            .dma_chans = 0,
            .logic_5v = 1,
            .needs_5v = 1,
        },
    .init = mouse_init,
    .deinit = mouse_deinit,
    .service = mouse_service,
    .reconfig = mouse_reconfig,
    .link_up = mouse_link_up,
};
