// jb_model.h — both assembled jbdev.pio programs on one wire shared with a console-side master.
// RX clocks at 8 MHz and TX at 4, so the bus steps at 8 and TX takes every other tick.
#ifndef JB_MODEL_H
#define JB_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#define JB_HZ          8000000
#define JB_TICKS_US    (JB_HZ / 1000000)
#define JB_CELL_US     4                        // one bit cell
#define JB_SAMPLE_US   2                        // where both ends read it
#define JB_QUIET_US    8                        // jbdev_rx's idle countdown
#define JB_LOW_US(one) ((one) ? 1u : 3u)        // '1' = 1 us low + 3 high, '0' = 3 + 1

#define JB_MASTER_MAX  40

typedef struct {
    const uint16_t *prog;
    unsigned len, wrap_target, wrap;
    unsigned pc, delay;
    uint32_t x, y, isr, osr;
    unsigned isr_count, osr_count;
    unsigned push_thresh, pull_thresh;
    unsigned sideset_bits;                      // whole side-set field, the opt enable included
    bool     sideset_opt;
    bool     enabled;
    uint8_t  pindir;                            // 1 = driving low; the output register is 0
    uint8_t  irq;                               // bit n set by `irq set n`
    bool   (*pop)(void *ctx, uint32_t *v);      // TX FIFO, read on the cycle autopull needs it
    bool   (*push)(void *ctx, uint32_t v);      // RX FIFO, false = full, which stalls the SM
    void    *ctx;
    unsigned stalls;
} jb_sm_t;

typedef struct {
    jb_sm_t  rx, tx;
    uint8_t  master_low;                        // the console holding the line down
    unsigned tick;
} jb_bus_t;

void jb_rx_init(jb_sm_t *s, const uint16_t *prog, unsigned len, unsigned wt, unsigned wrap,
                unsigned pc, bool (*push)(void *, uint32_t), void *ctx);
void jb_tx_init(jb_sm_t *s, const uint16_t *prog, unsigned len, unsigned wt, unsigned wrap,
                unsigned pc, bool (*pop)(void *, uint32_t *), void *ctx);

// Mirrors jbdev_resync(): shift state and PC back to frame_end, the FIFO is the caller's.
void jb_rx_resync(jb_sm_t *s, unsigned pc);

uint8_t jb_line(const jb_bus_t *b);             // 1 = released/high
void    jb_bus_step(jb_bus_t *b);               // one 8 MHz tick

// The console's receiver: sample 2 us past each falling edge, MSB first, end on quiet. The stop
// bit is one extra bit, dropped with the partial byte.
typedef struct {
    uint8_t  prev, state;
    unsigned t, high_run;
    uint32_t sr;
    unsigned nbits;
    uint8_t  bytes[JB_MASTER_MAX];
    unsigned n;
} jb_master_t;

void jb_master_reset(jb_master_t *m);
void jb_master_feed(jb_master_t *m, uint8_t level);
bool jb_master_done(const jb_master_t *m);      // quiet again, with at least one bit behind it

#endif
