// pio_core.h — a PIO state machine interpreter. Full ISA; the caller owns pins, IRQs and FIFOs.
// Pin indices are absolute in a 64-bit word: folding PIO2's gpio_base into the bases is the caller's.
#ifndef PIO_CORE_H
#define PIO_CORE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t (*pins_get)(void *u);
    void     (*pins_set)(void *u, uint64_t val, uint64_t mask);
    void     (*dirs_set)(void *u, uint64_t val, uint64_t mask);
    bool     (*irq_get)(void *u, unsigned n);
    void     (*irq_set)(void *u, unsigned n, bool v);
    bool     (*rx_push)(void *u, uint32_t w);       // false = full, stall at the IN
    bool     (*tx_pop)(void *u, uint32_t *w);       // false = empty, stall at the PULL
    void     *u;
} pio_core_bus_t;

typedef struct {
    uint8_t in_base, out_base, out_count, set_base, set_count, jmp_pin;
    uint8_t sideset_base, sideset_bits;             // bits includes the opt enable bit
    bool    sideset_opt, sideset_pindirs;
    uint8_t wrap_target, wrap;
    bool    in_shift_right, in_autopush;
    uint8_t in_push_threshold;                      // 1..32; 0 reads as 32
    bool    out_shift_right, out_autopull;
    uint8_t out_pull_threshold;
} pio_core_cfg_t;

typedef struct {
    const uint16_t *prog;                           // 32 words, JMP targets already relocated
    pio_core_cfg_t  cfg;
    uint8_t  pc;
    uint32_t x, y, isr, osr;
    uint8_t  isr_count, osr_count;                  // bits in the ISR, bits shifted out of the OSR
    bool     push_pending;                          // autopush retrying into a full FIFO
    bool     stalled;                               // last step could not retire
    uint64_t cycle;                                 // next SM cycle this may execute at
    uint32_t insns, stalls, push_stalls;            // push_stalls: autopush held off by a full FIFO
} pio_core_sm_t;

void pio_core_init(pio_core_sm_t *s, const uint16_t *prog, const pio_core_cfg_t *c, uint8_t pc);
void pio_core_restart(pio_core_sm_t *s);            // ISR empty, OSR empty (count 32)

// Retire at most one instruction. On success s->cycle advances by 1 + the delay field; on a stall
// it does not move, and deciding when to retry is the caller's.
bool pio_core_step(pio_core_sm_t *s, const pio_core_bus_t *bus);

// Tick every SM to `until` in array order, sms[i] against buses[i]. A lower-indexed SM's IRQ is
// visible to a higher one in the same tick, a cycle earlier than hardware.
void pio_core_run(pio_core_sm_t *const *sms, const pio_core_bus_t *buses, unsigned n,
                  uint64_t until);

#endif
