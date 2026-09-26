// pio_run.h — one SM's enable bit, atomically. CTRL holds every SM's, and pio_sm_set_enabled() is a
// read-modify-write that races the CYW43 gSPI flipping its own bit from the other core.
#pragma once
#include "hardware/pio.h"
#include "hardware/clocks.h"

// The SM clock divider for a program written at `hz`, from whatever clk_sys this build runs.
static inline float pio_clkdiv_hz(uint32_t hz) {
    return (float)clock_get_hz(clk_sys) / (float)hz;
}

#ifndef PIO_SM_RUN_SHIM
static inline void pio_sm_run(PIO pio, uint sm, bool on) {
    if (on) {
        hw_set_bits(&pio->ctrl, 1U << sm);
    } else {
        hw_clear_bits(&pio->ctrl, 1U << sm);
    }
}
#endif
