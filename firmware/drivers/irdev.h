// irdev.h — the IR LED on GP46: a free-running PWM carrier, keyed by the pin's function select.
#ifndef IRDEV_H
#define IRDEV_H

#include <stdint.h>
#include <stdbool.h>

bool irdev_init(unsigned pin, uint32_t carrier_hz);
void irdev_deinit(void);

// Blocking: n half-bits of half_us each from `halves`, first in bit 31, 1 = carrier on. Ends dark.
void irdev_send(uint32_t halves, unsigned n, uint32_t half_us);

// Blocking: d[] alternates mark, space, mark... in us, starting with a mark. Ends dark.
void irdev_send_durations(const uint16_t *d, unsigned n);

#endif
