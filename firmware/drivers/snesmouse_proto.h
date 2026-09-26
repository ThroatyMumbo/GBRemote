// snesmouse_proto.h — the SNES Mouse report and a D-pad-driven pointer, as pure host-testable logic.
// A pressed mask like snes_proto.h (set = line low = console reads 1), in shift order, LSB first.
#ifndef SNESMOUSE_PROTO_H
#define SNESMOUSE_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "pad.h"

enum {
    SMB_RIGHT = 8, SMB_LEFT = 9,
    SMB_SPEED_HI = 10, SMB_SPEED_LO = 11,
    SMB_SIG = 15,                   // $4218 low nibble 0001: the mouse signature
    SMB_Y = 16, SMB_X = 24,         // sign bit (1 = up / left), then 7 magnitude bits MSB first
};

#define SMOUSE_MAX 127
#define SMOUSE_SPEEDS 3             // top speeds SELECT cycles through: 360, 150, 57 px/s

typedef struct {
    int32_t  acc_x, acc_y;          // motion not yet delivered, 1/256 px
    uint32_t held_us;               // how long the current direction set has been held
    uint32_t dirs;                  // last tick's PAD_ direction bits
    uint8_t  speed;                 // 0..2, cycled by the console's strobes
    uint8_t  top;                   // index of the top speed, cycled by SELECT
    bool     left, right, select;
} smouse_t;

void     smouse_reset(smouse_t *m);
void smouse_tick(smouse_t *m, uint32_t b, uint32_t dt_us);
uint32_t smouse_report(const smouse_t *m);
void     smouse_rx(smouse_t *m, uint32_t w);     // an echo subtracts what it carried; a strobe cycles speed

uint32_t smouse_pack(int dx, int dy, bool left, bool right, unsigned speed);
static inline uint32_t smouse_with_cmd(uint32_t report, uint8_t cmd) {   // cmd lands in $4219
    uint32_t r = 0;
    for (unsigned k = 0; k < 8; k++) { r |= (uint32_t)((cmd >> (7 - k)) & 1U) << k; }
    return (report & ~0xffU) | r;
}
void     smouse_unpack(uint32_t w, int *dx, int *dy, bool *left, bool *right, unsigned *speed);

#endif
