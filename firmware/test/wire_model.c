#include "wire_model.h"
#include <string.h>

#define SAMPLE_DLY 13   // must track jbdev.pio
#define IDLE_TO    31

static unsigned put_cell(uint8_t *level, unsigned cap, unsigned t, bool one) {
    unsigned low = one ? 1 * WIRE_TICKS_US : 3 * WIRE_TICKS_US;
    for (unsigned i = 0; i < WIRE_BIT_TICKS && t + i < cap; i++)
        level[t + i] = (i < low) ? 0 : 1;
    return t + WIRE_BIT_TICKS;
}

unsigned wire_encode(uint8_t *level, unsigned cap, const uint8_t *bytes, unsigned n,
                     bool stop_bit, unsigned trunc_bits) {
    memset(level, 1, cap);                          // idle high
    unsigned t = 0, bit = 0;
    for (unsigned i = 0; i < n; i++) {
        for (int b = 7; b >= 0; b--) {
            if (trunc_bits && bit >= trunc_bits) return t;
            t = put_cell(level, cap, t, (bytes[i] >> b) & 1u);
            bit++;
        }
    }
    if (stop_bit) {                                 // 1 us low, then released
        for (unsigned i = 0; i < 1 * WIRE_TICKS_US && t + i < cap; i++) level[t + i] = 0;
        t += 1 * WIRE_TICKS_US;
    }
    return t;
}

void wire_rx_sim(const uint8_t *level, unsigned ticks, wire_rx_t *out) {
    memset(out, 0, sizeof *out);

    unsigned t = 0, isr = 0, cnt = 0, x = 0;
    enum { FRAME_END, SET_X, POLL, POLL_PIN, NOP_DLY, IN_PINS, WAIT_HIGH } pc = FRAME_END;

    while (t < ticks) {
        switch (pc) {
        case FRAME_END:                             // mov isr,null ; irq set 4
            if (cnt) out->dropped_bits += cnt;
            isr = 0; cnt = 0;
            out->idle_events++;
            t += 2;
            pc = SET_X;
            break;
        case SET_X:
            x = IDLE_TO;
            t += 1;
            pc = POLL;
            break;
        case POLL:                                  // jmp x-- poll_pin
            t += 1;
            if (x) { x--; pc = POLL_PIN; }
            else   { t += 1; pc = FRAME_END; }      // the fall-through jmp frame_end
            break;
        case POLL_PIN:                              // jmp pin poll
            t += 1;
            pc = level[t - 1] ? POLL : NOP_DLY;
            break;
        case NOP_DLY:
            t += 1 + SAMPLE_DLY;
            pc = IN_PINS;
            break;
        case IN_PINS:
            isr = (isr << 1) | (level[t] & 1u);
            t += 1;
            if (++cnt == 8) {                       // autopush
                if (out->n < sizeof out->bytes) out->bytes[out->n++] = (uint8_t)isr;
                isr = 0; cnt = 0;
            }
            pc = WAIT_HIGH;
            break;
        case WAIT_HIGH:                             // wait 1 pin 0
            while (t < ticks && !level[t]) t++;
            t += 1;
            pc = SET_X;
            break;
        }
    }
    if (cnt) out->dropped_bits += cnt;
}
