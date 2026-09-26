// link_model.h — sneslink.pio on pio_core, clocked by a 5A22 running the link loops of
// demos/mariopaint/snes/link.asm at their master-cycle timings. The device half is the caller's.
#ifndef LINK_MODEL_H
#define LINK_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "pio_core.h"

#define LM_MC_PS 46561u                 // one master cycle at 21.477 MHz, in ps

typedef struct lm lm_t;
typedef void (*lm_device_fn)(lm_t *m, void *u);     // core1's pass, called between console steps

struct lm {
    pio_core_sm_t sm;
    uint16_t prog[32];
    uint8_t  start;
    bool     enabled;
    uint64_t pins;              // console-driven: LATCH, CLK, IOBIT
    uint8_t  out, dir;          // the SM's D0/D1 levels and pindirs, bit 0 = D0
    uint32_t tx[4], rx[4];
    unsigned tx_rd, tx_wr, rx_rd, rx_wr;
    uint64_t ps;                // console time
    uint64_t service_ps, next_service_ps;
    lm_device_fn dev;
    void    *dev_u;
    unsigned read_low_ns;       // CLK low time of a $4016 read: 12 master cycles nominal
    unsigned reads;             // $4016 reads the console made
};

void lm_init(lm_t *m, const uint16_t *insns, unsigned len, unsigned wrap_target, unsigned wrap,
             unsigned start, lm_device_fn dev, void *u);

// The device's side, as sneslink.c does it.
void lm_arm(lm_t *m);
void lm_go(lm_t *m);
bool lm_tx(lm_t *m, uint32_t w);
bool lm_rx(lm_t *m, uint32_t *w);

// The console's side.
void     lm_wait_mc(lm_t *m, unsigned mc);
unsigned lm_read(lm_t *m);      // one `lda $4016`, fetch included: bits 1:0 as the CPU sees them
void     lm_iobit(lm_t *m, bool high);

#endif
