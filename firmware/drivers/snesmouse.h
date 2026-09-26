// snesmouse.h — device-side SNES Mouse wire layer: frame, speed strobe and echo, meaning-agnostic.
// SDK-only deps, so demos/ link it. core1, interrupts masked.
#ifndef SNESMOUSE_H
#define SNESMOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"

#define SNESMOUSE_STROBE 0xffffffffu    // RX word for a speed strobe; every other word is an echo

typedef struct { uint32_t latches, frames, strobes, drops, restarts; } snesmouse_stats_t;

// Claims 1 SM and 1 program. pio_set_gpio_base(pio, 16) must already have run.
bool snesmouse_init(PIO pio, uint pin_data, uint pin_latch, uint pin_clk);
void snesmouse_deinit(void);

void snesmouse_put(uint32_t report);            // drops if the SM is mid-frame, as snesdev does
bool snesmouse_rx(uint32_t *w);                 // next echo or SNESMOUSE_STROBE, in wire order
bool snesmouse_wait_latch(uint32_t timeout_us); // true on a LATCH seen by the SM
void snesmouse_restart(void);                   // a frame cut short: back to top with X cleared

const snesmouse_stats_t *snesmouse_stats(void);

#endif
