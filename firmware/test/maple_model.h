// maple_model.h — enough PIO to run maple_tx, so the 26-word TX can be diffed edge for edge
// against Charlie Cole's 29-word original on the same payload.
#ifndef MAPLE_MODEL_H
#define MAPLE_MODEL_H

#include <stdint.h>

// Sized for the longest frame on the bus, a 525-byte block-read reply; a controller reply is ~2800.
#define MAPLE_MAX_TRACE 16384

// Bus state as the decoder sees it: bit0 = SDCKA, bit1 = SDCKB. A released line reads high.
typedef struct {
    uint8_t  state[MAPLE_MAX_TRACE];
    uint32_t hold[MAPLE_MAX_TRACE];     // cycles the state was held, for timing comparison
    unsigned n;
    unsigned cycles;
    int      overflow;
} maple_trace_t;

// Run one SM for a single frame, stopping when it wraps or stalls on an empty FIFO.
void maple_tx_sim(const uint16_t *prog, unsigned len, unsigned wrap_target, unsigned wrap,
                  const uint32_t *words, unsigned nwords, maple_trace_t *out);

// Charlie Cole's maple_tx as MaplePad and dc_dvd ship it, frozen: drivers/mapledev.pio must
// reproduce its waveform state for state and cycle for cycle.
extern const uint16_t maple_tx_reference[29];
#define MAPLE_TX_REF_WRAP_TARGET 0u
#define MAPLE_TX_REF_WRAP        28u

#endif
