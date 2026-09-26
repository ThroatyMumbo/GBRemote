// mouse_model.h — snesmouse.pio on pio_core, read by Mario Paint's own decode, transcribed from
// CODE_01DA0D, CODE_01DA73 and CODE_01DAF9.
#ifndef MOUSE_MODEL_H
#define MOUSE_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "pio_core.h"

typedef struct mm mm_t;
typedef void (*mm_device_fn)(mm_t *m, void *u);     // core1's service pass, called between edges

struct mm {
    pio_core_sm_t sm;
    uint16_t prog[32];
    uint64_t pins;              // console-driven levels (LATCH, CLK)
    bool     data_dir;          // the SM's pindir on DATA1: 1 pulls the line low
    uint8_t  irq;
    uint32_t tx[4], rx[4];
    unsigned tx_rd, tx_wr, rx_rd, rx_wr;
    uint64_t ns;                // console time
    uint64_t next_service_ns;
    mm_device_fn dev;
    void    *dev_u;

    unsigned manual_low_ns;     // CLK low time of a manual $4016 read: the unmeasured number
    uint8_t  want_speed;        // Mario Paint's $04C4
    uint8_t  r4218, r4219;      // this frame's auto-read
    int      dx, dy;            // this frame's decoded displacement
    bool     present, left, right;
    unsigned speed;             // speed bits as the game read them
    unsigned strobes;           // strobes the game issued, total
};

void mm_init(mm_t *m, const uint16_t *insns, unsigned len, unsigned wrap_target, unsigned wrap,
             mm_device_fn dev, void *u);
bool mm_push(mm_t *m, uint32_t w);      // core1 -> TX FIFO; false when full
bool mm_pop(mm_t *m, uint32_t *w);      // RX FIFO -> core1
bool mm_irq_take(mm_t *m, unsigned n);
void mm_wait_ns(mm_t *m, uint64_t ns);  // console holds its lines; PIO and core1 run
void mm_frame(mm_t *m);                 // one Mario Paint mouse read on port 1, strobes included

#endif
