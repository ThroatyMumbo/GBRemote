// wire_model.h — jbdev_rx mirrored by hand against a synthesized Joybus line: the sample point lands
// inside both a '0' and a '1', and the idle countdown expires between frames, never inside one.
#ifndef WIRE_MODEL_H
#define WIRE_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#define WIRE_HZ        8000000              // the RX SM's clock: 8 ticks per microsecond
#define WIRE_TICKS_US  (WIRE_HZ / 1000000)
#define WIRE_BIT_TICKS (4 * WIRE_TICKS_US)  // 4 us bit cell

// Render bytes MSB-first onto a level buffer, 1 = released/high. Returns ticks written.
// trunc_bits > 0 stops after that many bits with no stop bit — a truncated frame.
unsigned wire_encode(uint8_t *level, unsigned cap, const uint8_t *bytes, unsigned n,
                     bool stop_bit, unsigned trunc_bits);

typedef struct {
    uint8_t  bytes[64];
    unsigned n;
    unsigned idle_events;       // times the SM reached frame_end (IRQ 4)
    unsigned dropped_bits;      // partial bits discarded by mov isr,null
} wire_rx_t;

// Faithful instruction-by-instruction simulation of jbdev_rx.
void wire_rx_sim(const uint8_t *level, unsigned ticks, wire_rx_t *out);

#endif
