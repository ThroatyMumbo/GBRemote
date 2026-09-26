// snes_model.h — snesdev.pio against a console-side latch/clock waveform: the bit the console
// samples during clock N's low phase must be report bit N.
#ifndef SNES_MODEL_H
#define SNES_MODEL_H

#include <stdint.h>
#include "pio_core.h"

#define SNES_MODEL_MAX_CLOCKS 64

typedef struct {
    uint8_t  bit[SNES_MODEL_MAX_CLOCKS];    // 1 = line driven low = pressed
    unsigned n;
} snes_trace_t;

typedef struct {
    pio_core_sm_t sm;
    uint16_t prog[32];
    uint64_t now;                           // PIO cycles run so far
    uint8_t  pindirs;                       // DATA1; its output level is held 0 (open drain)
    uint8_t  latch, clk, irq;
    uint32_t fifo[4];                       // the real TX FIFO depth
    unsigned rd, wr;
} snes_sm_t;

// Mirrors snesdev_program_init(): X and OSR zeroed, ISR = nbits-1, line released, PC at wrap.
void snes_sm_init(snes_sm_t *sm, const uint16_t *prog, unsigned len,
                  unsigned wrap_target, unsigned wrap, unsigned nbits);

// Drops the word if the FIFO is full, like snesdev_put_report().
void snes_sm_push(snes_sm_t *sm, uint32_t report);

// Console-side idle: LATCH and CLK held at their idle levels for us microseconds.
void snes_sm_idle(snes_sm_t *sm, unsigned us);

// One console poll: a 12 us LATCH pulse, then nclocks clock pulses at 6 us per phase. Samples the
// data line in the middle of each clock's low phase, where the console reads it.
void snes_sm_poll(snes_sm_t *sm, unsigned nclocks, snes_trace_t *out);

#endif
