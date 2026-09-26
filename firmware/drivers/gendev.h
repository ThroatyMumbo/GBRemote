// gendev.h — device-side Genesis / Mega Drive port: an 8-cycle nibble table on four lines and a
// TH-level-keyed pair on two more, meaning-agnostic. core1, interrupts masked.
#ifndef GENDEV_H
#define GENDEV_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"

typedef struct { uint32_t polls, resyncs, drops; } gendev_stats_t;

// Claims 2 SMs and 2 programs; needs pio_set_gpio_base(pio, 16). pin_d0 heads four lines, pin_tl
// two; pin_th lies outside both and stays a plain input.
bool gendev_init(PIO pio, uint pin_tl, uint pin_th, uint pin_d0);
void gendev_deinit(void);

// Bit set = pressed = line low. Call often: the console reads whatever the last call put.
void gendev_put(uint32_t dpad_word, uint32_t tltr_word);

// Spins until a TH edge or timeout_us; it also enforces the counter reset, so call it in a loop.
bool gendev_service(uint32_t timeout_us);

// A real 6-button pad resets its counter after TH rests ~1.5 ms. 0 restores the default.
void gendev_set_reset_us(uint32_t us);

// Push-pull by default, as a real pad's 74HC157 drives. Call before gendev_init().
void gendev_set_open_drain(bool on);

// Adds our pull-ups to the console's, for a port too weak to reach VIH through the 100R. Normally off.
void gendev_set_pullup(bool on);

const gendev_stats_t *gendev_stats(void);

#endif
