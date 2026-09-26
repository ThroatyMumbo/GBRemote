// capout_model.h — cycle model of capstream plus a rising-edge receiver: each nibble is stable a
// cycle before its edge, and nibbles leave low-first. Not a test of capenc.c.
#ifndef CAPOUT_MODEL_H
#define CAPOUT_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  clk;
    uint8_t  pins;      // 4 data lines
} capout_cycle_t;

// Run the assembled capstream words over `words` of TX FIFO data. Returns cycles written.
unsigned capout_sim(const uint16_t *insns, unsigned n_insns,
                    const uint32_t *words, unsigned n_words,
                    capout_cycle_t *out, unsigned cap);

// Sample on every rising clock edge, low nibble first. Returns bytes recovered.
unsigned capout_receive(const capout_cycle_t *cyc, unsigned n, uint8_t *out, unsigned cap);

// True if every rising edge had its data stable for at least the preceding cycle.
bool capout_setup_ok(const capout_cycle_t *cyc, unsigned n, unsigned *first_bad);

#endif
