// gen_model.h — both gendev SMs against a console-side TH waveform; the modeled 68000 read loop is
// the oracle. When gendev_service() rewinds is out of scope: gen_model_resync() places it by hand.
#ifndef GEN_MODEL_H
#define GEN_MODEL_H

#include <stdint.h>
#include "pio_core.h"

#define GEN_MODEL_MAX_CYCLES 32

typedef struct {
    uint8_t  line[GEN_MODEL_MAX_CYCLES];    // bit n = line n driven LOW; D0..D3, then TL, TR
    uint8_t  th[GEN_MODEL_MAX_CYCLES];      // the TH level the console held for that read
    unsigned n;
} gen_trace_t;

typedef struct gen_model gen_model_t;

typedef struct {
    pio_core_sm_t sm;
    uint16_t prog[32];                      // gendev.c patches a copy for open drain; so do we
    unsigned irqs;
    uint32_t fifo[4];                       // the real TX FIFO depth
    unsigned rd, wr;
    gen_model_t *m;                         // the pins and TH both SMs share
} gen_sm_t;

struct gen_model {
    gen_sm_t dpad, tltr;                    // one shares TH as IN pin 0 and JMP pin with the other
    uint8_t  th;
    int      open_drain;                    // gendev's two drive modes, same program either way
    uint64_t out, dirs;                     // absolute GPIO bits, both OUT windows
    uint64_t now;                           // PIO cycles run so far
};

// Mirrors gen_dpad_program_init()/gen_tltr_program_init(): X and pins primed released, PCs at the
// wrap target, TH high.
void gen_model_init(gen_model_t *m, int open_drain,
                    const uint16_t *dpad, unsigned dpad_len, unsigned dpad_wt, unsigned dpad_wrap,
                    const uint16_t *tltr, unsigned tltr_len, unsigned tltr_wt, unsigned tltr_wrap);

// Takes a pressed mask and inverts for push-pull, exactly as gendev_put() does. Drops the word if
// that SM's FIFO is full, also like gendev_put().
void gen_model_put(gen_model_t *m, uint32_t dpad_word, uint32_t tltr_word);

// TH held at its current level for us microseconds.
void gen_model_idle(gen_model_t *m, unsigned us);

// The rewind alone: both SMs back to their idle entry, FIFOs cleared, X untouched — which is all
// pio_sm_restart() plus the explicit jmp actually do.
void gen_model_rewind(gen_model_t *m);

// gendev_service()'s counter reset: rewind, then re-prime each FIFO, or the SM serves its stale X.
void gen_model_resync(gen_model_t *m, uint32_t dpad_word, uint32_t tltr_word);

// The six lines as the console would read them right now: bit n = line n driven LOW.
uint8_t gen_model_sample(const gen_model_t *m);

// One console read loop of ncycles TH cycles from TH high, sampling 2 us after each TH write.
// Appends to out.
void gen_model_read(gen_model_t *m, unsigned ncycles, gen_trace_t *out);

#endif
