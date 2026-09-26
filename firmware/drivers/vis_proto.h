// vis_proto.h — the Tandy VIS IR frame: 16-bit biphase, 300 us half-bit, RC5 sense, MSB first,
// ~56 kHz carrier, the whole held set per frame (learned with demos/irlearn).
#ifndef VIS_PROTO_H
#define VIS_PROTO_H

#include <stdint.h>
#include "pad.h"

enum {
    VIS_BTN_DOWN = 1U << 3,
    VIS_BTN_1 = 1U << 4,
    VIS_BTN_A = 1U << 5,
    VIS_BTN_2 = 1U << 6,
    VIS_BTN_4 = 1U << 7,
    VIS_BTN_B = 1U << 8,
    VIS_BTN_3 = 1U << 9,
    VIS_BTN_LEFT = 1U << 10,
    VIS_BTN_UP = 1U << 11,
    VIS_BTN_RIGHT = 1U << 12,
    VIS_PLAYER2 = 1U << 13, // Player 2 mode's flag, encoded as one more button
    VIS_HELD_MASK = 0x3ff8U,
};

#define VIS_HALF_US        300
#define VIS_FRAME_BITS     16
#define VIS_CARRIER_HZ     56000    // the bench receiver is a 56 kHz part and hears it at range
#define VIS_SOLO_PERIOD_US 24000    // Solo: one frame per 24 ms while anything is held

// The full 16-bit frame for a held set (VIS_BTN_* | VIS_PLAYER2). vis_encode(0) is Solo's release.
uint16_t vis_encode(uint16_t held);

// SELECT/START are 4/3 as on the controller, X/Y are 1/2; Down+Up cannot be pressed, so neither is sent.
uint16_t vis_held_from_pad(const pad_state_t *p);

// The frame as 32 half-bit levels, first half in bit 31, 1 = carrier on.
uint32_t vis_halves(uint16_t code);

#endif
