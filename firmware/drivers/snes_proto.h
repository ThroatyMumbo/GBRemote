// snes_proto.h — the SNES and NES pad reports as pure host-testable logic. Both are a pressed mask
// that snesdev.pio shifts into PINDIRS; never invert them here.
#ifndef SNES_PROTO_H
#define SNES_PROTO_H

#include <stdint.h>
#include "pad.h"

// Wire order, LSB first — the order the console clocks them out.
enum {
    SNES_B = 0, SNES_Y, SNES_SELECT, SNES_START, SNES_UP, SNES_DOWN, SNES_LEFT, SNES_RIGHT,
    SNES_A,     SNES_X, SNES_L,      SNES_R,
    SNES_BITS = 16,             // 12 buttons then 4 released bits: a standard pad's ID nibble
};

enum {
    NES_A = 0, NES_B, NES_SELECT, NES_START, NES_UP, NES_DOWN, NES_LEFT, NES_RIGHT,
    NES_BITS = 8,
};

// CFG_DRV block, shared layout, owned by whichever of the two is resident.
enum { SNC_FLAGS = 0, SNC_DEADZONE = 1 };
enum {
    SNF_CANCEL_OPPOSITE = 1U << 0, // suppress U+D and L+R, which some games mishandle
    SNF_DPAD_FROM_AXIS = 1U << 1,  // project AX_LX/AX_LY onto the d-pad
};

#define SNES_DEADZONE_DEFAULT 48

uint32_t snes_from_pad(const pad_state_t *p, uint8_t flags, uint8_t deadzone);
uint32_t nes_from_pad (const pad_state_t *p, uint8_t flags, uint8_t deadzone);

#endif
