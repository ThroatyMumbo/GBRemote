#include "sneslink.h"
#include "sneslink.pio.h"
#include "pio_run.h"
#include "devcommon.h"

static pio_res_t g_res;
static PIO  g_pio;
static uint g_sm, g_off, g_pin_d0;

bool sneslink_init(PIO pio, uint pin_d0, uint pin_latch) {
    static const struct pio_program *const progs[] = { &sneslink_program };

    g_pio = NULL;
    if (!pio_res_claim(&g_res, pio, progs, 1, 1)) return false;
    g_pio = pio;
    g_off = g_res.off[0];
    g_sm  = g_res.sm[0];
    g_pin_d0 = pin_d0;
    sneslink_program_init(pio, g_sm, g_off, pin_d0, pin_latch);
    pio_sm_exec(pio, g_sm, pio_encode_set(pio_pins, 3));
    pio_sm_set_consecutive_pindirs(pio, g_sm, pin_d0, 2, true);
    for (uint p = pin_d0; p < pin_d0 + 2; p++) {
        pio_gpio_init(pio, p);
        gpio_set_slew_rate(p, GPIO_SLEW_RATE_SLOW);
    }
    return true;
}

void sneslink_deinit(void) {
    if (!g_pio) return;
    pio_sm_run(g_pio, g_sm, false);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, g_pin_d0, 2, false);
    pio_res_release(&g_res);
    g_pio = NULL;
}

void __not_in_flash_func(sneslink_arm)(void) {
    if (!g_pio) return;
    pio_sm_run(g_pio, g_sm, false);
    pio_sm_clear_fifos(g_pio, g_sm);
    pio_sm_restart(g_pio, g_sm);
    pio_sm_exec(g_pio, g_sm, pio_encode_set(pio_pins, 3));
    pio_sm_exec(g_pio, g_sm, pio_encode_jmp(g_off + sneslink_offset_start));
}

void __not_in_flash_func(sneslink_go)(void) {
    if (g_pio) pio_sm_run(g_pio, g_sm, true);
}

bool __not_in_flash_func(sneslink_tx)(uint32_t w) {
    if (!g_pio || pio_sm_is_tx_fifo_full(g_pio, g_sm)) return false;
    pio_sm_put(g_pio, g_sm, w);
    return true;
}

bool __not_in_flash_func(sneslink_rx)(uint32_t *w) {
    if (!g_pio || pio_sm_is_rx_fifo_empty(g_pio, g_sm)) return false;
    *w = pio_sm_get(g_pio, g_sm);
    return true;
}
