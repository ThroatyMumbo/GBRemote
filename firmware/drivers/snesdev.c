#include "snesdev.h"
#include "snesdev.pio.h"
#include "pio_run.h"
#include "devcommon.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

_Static_assert(PIO_WORDS(snesdev) <= PIO2_SHARED_WORDS,
               "snesdev outgrew PIO2's budget with the CYW43 gSPI resident");

static pio_res_t g_res;
static PIO      g_pio;
static uint     g_sm, g_off;
static snesdev_stats_t g_st;

bool snesdev_init(PIO pio, uint pin_data, uint pin_latch, uint pin_clk, unsigned nbits) {
    static const struct pio_program *const progs[] = { &snesdev_program };

    g_pio = NULL;
    g_st = (snesdev_stats_t){ 0 };

    if (pin_clk != pin_latch + 1U) {
        return false; // one IN base must reach both
    }
    if (nbits == 0 || nbits > 32) {
        return false; // nbits-1 must fit `set`'s 5-bit immediate
    }
    if (!pio_res_claim(&g_res, pio, progs, 1, 1)) { return false; }

    g_pio = pio;
    g_off = g_res.off[0];
    g_sm  = g_res.sm[0];

    snesdev_program_init(pio, g_sm, g_off, pin_data, pin_latch, nbits);
    pio_interrupt_clear(pio, snesdev_IRQ_LATCH);
    pio_sm_run(pio, g_sm, true);
    return true;
}

void snesdev_deinit(void) {
    if (!g_pio) { return; }
    pio_sm_run(g_pio, g_sm, false);
    pio_res_release(&g_res);
    pio_interrupt_clear(g_pio, snesdev_IRQ_LATCH);
    g_pio = NULL;
}

void __not_in_flash_func(snesdev_put_report)(uint32_t pressed) {
    if (!g_pio) { return; }
    // A full FIFO means the SM is mid-frame holding its report; drop, the next call lands in the gap.
    if (pio_sm_is_tx_fifo_full(g_pio, g_sm)) { g_st.drops++; return; }
    pio_sm_put(g_pio, g_sm, pressed);
}

bool __not_in_flash_func(snesdev_wait_latch)(uint32_t timeout_us) {
    if (!g_pio) { return false; }
    uint32_t t0 = timer_hw->timerawl;
    for (;;) {
        if (pio_interrupt_get(g_pio, snesdev_IRQ_LATCH)) {
            pio_interrupt_clear(g_pio, snesdev_IRQ_LATCH);
            g_st.latches++;
            return true;
        }
        if (timer_hw->timerawl - t0 > timeout_us) { return false; }
    }
}

const snesdev_stats_t *snesdev_stats(void) { return &g_st; }
