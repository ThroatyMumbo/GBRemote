// pad.h — the canonical pad state every protocol driver consumes, published core0->core1 by seqlock.
// Bits 0-7 are GBDK's J_* order, so the default map is the identity; bits 8+ cover N64/PS2-class pads.
#ifndef PAD_H
#define PAD_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "seqlock.h"

enum {
    PAD_RIGHT = 1U << 0,
    PAD_LEFT = 1U << 1,
    PAD_UP = 1U << 2,
    PAD_DOWN = 1U << 3,
    PAD_A = 1U << 4,
    PAD_B = 1U << 5,
    PAD_SELECT = 1U << 6,
    PAD_START = 1U << 7,
    PAD_X = 1U << 8,
    PAD_Y = 1U << 9,
    PAD_L = 1U << 10,
    PAD_R = 1U << 11,
    PAD_ZL = 1U << 12,
    PAD_ZR = 1U << 13,
    PAD_C_UP = 1U << 14,
    PAD_C_DOWN = 1U << 15,
    PAD_C_LEFT = 1U << 16,
    PAD_C_RIGHT = 1U << 17,
    PAD_L3 = 1U << 18,
    PAD_R3 = 1U << 19,
    PAD_MODE = 1U << 20,
    PAD_HOME = 1U << 21,
};

enum { AX_LX = 0, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT, PAD_AXES };

enum { PADF_FROM_GB = 1U << 0, PADF_RELEASED = 1U << 1 }; // RELEASED = synthesized, not polled

// int8 axes fit the N64 stick natively; a 0..255 target adds 128 in its own projection.
typedef struct {
    uint32_t buttons;
    int8_t   axis[PAD_AXES];
    uint8_t  flags;
    uint8_t  _rsv;
    uint32_t t_ms;                  // publisher's to_ms_since_boot(), inside the seqlock
} pad_state_t;

typedef struct {
    volatile uint32_t seq;          // odd while the writer is inside
    pad_state_t s;
} pad_pub_t;

static inline void pad_zero(pad_state_t *s) {
    memset(s, 0, sizeof *s);
    s->flags = PADF_RELEASED;
}

#define PAD_DIRS (PAD_RIGHT | PAD_LEFT | PAD_UP | PAD_DOWN)

// -1, 0 or +1 along one d-pad axis.
static inline int pad_dir(uint32_t b, uint32_t neg, uint32_t pos) {
    return ((b & pos) ? 1 : 0) - ((b & neg) ? 1 : 0);
}

// The d-pad as PAD_DIRS bits. dz >= 0 also folds in the left stick past +/-dz. `cancel` drops
// opposite pairs: a remap can press both, a real rocker cannot, and some games walk into a wall.
static inline uint32_t pad_dpad(const pad_state_t *p, int dz, bool cancel) {
    uint32_t d = p->buttons & PAD_DIRS;
    if (dz >= 0) {
        if (dz > 127) {
            dz = 127; // axis is int8_t; 128+ would never trip
        }
        if (p->axis[AX_LX] >= dz) {
            d |= PAD_RIGHT;
        } else if (p->axis[AX_LX] <= -dz) {
            d |= PAD_LEFT;
        }
        if (p->axis[AX_LY] >= dz) {
            d |= PAD_UP;
        } else if (p->axis[AX_LY] <= -dz) {
            d |= PAD_DOWN;
        }
    }
    if (cancel) {
        if ((d & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) {
            d &= ~(uint32_t)(PAD_UP | PAD_DOWN);
        }
        if ((d & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) {
            d &= ~(uint32_t)(PAD_LEFT | PAD_RIGHT);
        }
    }
    return d;
}

static inline void pad_publish(pad_pub_t *p, const pad_state_t *s) {
    seq_write(&p->seq, &p->s, s, sizeof *s);
}

// A torn read must never reach the console; core0 publishes once per GB poll, so four tries suffice.
static inline bool pad_read(const pad_pub_t *p, pad_state_t *out) {
    return seq_read(&p->seq, out, &p->s, sizeof *out, NULL);
}

#endif
