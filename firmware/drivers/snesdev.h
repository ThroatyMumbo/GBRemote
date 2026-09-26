// snesdev.h — device-side SNES / NES port: an N-bit active-low report on a latch/clock port,
// meaning-agnostic. core1, interrupts masked.
#ifndef SNESDEV_H
#define SNESDEV_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"

typedef struct { uint32_t latches, drops; } snesdev_stats_t;

// Claims 1 SM and 1 program; needs pio_set_gpio_base(pio, 16) and pin_clk == pin_latch + 1.
bool snesdev_init(PIO pio, uint pin_data, uint pin_latch, uint pin_clk, unsigned nbits);
void snesdev_deinit(void);

// Bit n set = pressed = line low; bits at or above nbits are ignored. Call often: the console
// latches whatever the last call put.
void snesdev_put_report(uint32_t pressed);

// Spins until a latch or timeout_us; a latch is the only evidence the console is reading us.
bool snesdev_wait_latch(uint32_t timeout_us);

const snesdev_stats_t *snesdev_stats(void);

#endif
