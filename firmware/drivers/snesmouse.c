#include "snesmouse.h"
#include "snesmouse.pio.h"
#include "pio_run.h"
#include "devcommon.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

_Static_assert(PIO_WORDS(snesmouse) <= PIO2_SHARED_WORDS,
               "snesmouse outgrew PIO2's budget with the CYW43 gSPI resident");

#define MISSED_LATCHES 2    // LATCH pulses the SM never framed: it is parked mid-frame, so rewind

static pio_res_t g_res;
static PIO      g_pio;
static uint     g_sm, g_off, g_pin_latch;
static bool     g_lvl, g_rise;
static uint32_t g_missed;
static snesmouse_stats_t g_st;

bool snesmouse_init(PIO pio, uint pin_data, uint pin_latch, uint pin_clk) {
    static const struct pio_program *const progs[] = { &snesmouse_program };

    g_pio = NULL;
    g_st = (snesmouse_stats_t){ 0 };
    g_pin_latch = pin_latch;
    g_lvl = g_rise = false;
    g_missed = 0;
    if (pin_clk != pin_latch + 1U) { return false; }
    if (!pio_res_claim(&g_res, pio, progs, 1, 1)) { return false; }
    g_pio = pio;
    g_off = g_res.off[0];
    g_sm  = g_res.sm[0];
    snesmouse_program_init(pio, g_sm, g_off, pin_data, pin_latch);
    pio_interrupt_clear(pio, snesmouse_IRQ_LATCH);
    pio_sm_run(pio, g_sm, true);
    return true;
}

void snesmouse_deinit(void) {
    if (!g_pio) { return; }
    pio_sm_run(g_pio, g_sm, false);
    pio_res_release(&g_res);
    pio_interrupt_clear(g_pio, snesmouse_IRQ_LATCH);
    g_pio = NULL;
}

void __not_in_flash_func(snesmouse_put)(uint32_t report) {
    if (!g_pio) { return; }
    if (pio_sm_is_tx_fifo_full(g_pio, g_sm)) { g_st.drops++; return; }
    pio_sm_put(g_pio, g_sm, report);
}

bool __not_in_flash_func(snesmouse_rx)(uint32_t *w) {
    if (!g_pio || pio_sm_is_rx_fifo_empty(g_pio, g_sm)) { return false; }
    *w = pio_sm_get(g_pio, g_sm);
    if (*w == SNESMOUSE_STROBE) {
        g_st.strobes++;
    } else {
        g_st.frames++;
    }
    return true;
}

bool __not_in_flash_func(snesmouse_wait_latch)(uint32_t timeout_us) {
    if (!g_pio) { return false; }
    uint32_t t0 = timer_hw->timerawl;
    for (;;) {
        if (pio_interrupt_get(g_pio, snesmouse_IRQ_LATCH)) {
            pio_interrupt_clear(g_pio, snesmouse_IRQ_LATCH);
            g_st.latches++;
            g_rise = false;
            g_missed = 0;
            return true;
        }
        // The IRQ flag is sticky, so a pulse that rose and fell without one was never framed.
        bool lvl = gpio_get(g_pin_latch);
        if (lvl && !g_lvl) { g_rise = true; }
        if (!lvl && g_lvl && g_rise) {
            g_rise = false;
            if (++g_missed >= MISSED_LATCHES) { snesmouse_restart(); g_missed = 0; }
        }
        g_lvl = lvl;
        // An echo or strobe waiting is work for the caller; do not sit on it until the timeout.
        if (!pio_sm_is_rx_fifo_empty(g_pio, g_sm)) { return false; }
        if (timer_hw->timerawl - t0 > timeout_us) { return false; }
    }
}

// Same order as gendev's rewind: nothing staged before the restart may survive it.
void __not_in_flash_func(snesmouse_restart)(void) {
    if (!g_pio) { return; }
    pio_sm_run(g_pio, g_sm, false);
    pio_sm_clear_fifos(g_pio, g_sm);
    pio_sm_restart(g_pio, g_sm);
    pio_sm_exec(g_pio, g_sm, pio_encode_mov(pio_x, pio_null));
    pio_sm_exec(g_pio, g_sm, pio_encode_set(pio_pindirs, 0));
    pio_sm_exec(g_pio, g_sm, pio_encode_jmp(g_off + snesmouse_offset_top));
    pio_sm_run(g_pio, g_sm, true);
    g_st.restarts++;
}

const snesmouse_stats_t *snesmouse_stats(void) { return &g_st; }
