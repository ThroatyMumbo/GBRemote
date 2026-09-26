// gen_proto.h — the Genesis / Mega Drive pad report as pure host-testable logic over the 8-cycle
// table. Both words are a pressed mask; gendev.c converts to wire levels.
#ifndef GEN_PROTO_H
#define GEN_PROTO_H

#include <stdint.h>
#include "pad.h"

// Bit position within a nibble, and within the TL/TR pair. Wire order, LSB first.
enum { GEN_D0 = 0, GEN_D1, GEN_D2, GEN_D3 };
enum { GEN_TL = 0, GEN_TR };

#define GEN_CYCLES 8            // nibbles in the dpad word

typedef struct {
    uint32_t dpad;              // 8 nibbles, cycle 1 in the low nibble -> D0..D3
    uint32_t tltr;              // bits 0-1 = TH high (B, C); bits 2-3 = TH low (A, START)
} gen_report_t;

// CFG_DRV block.
enum { GNC_FLAGS = 0, GNC_DEADZONE = 1, GNC_RESET_100US = 2 };
enum {
    GNF_THREE_BUTTON = 1U << 0,    // cycles 6-8 become ordinary ones: a 3-button pad
    GNF_CANCEL_OPPOSITE = 1U << 1, // suppress U+D and L+R, which some games mishandle
    GNF_DPAD_FROM_AXIS = 1U << 2,  // project AX_LX/AX_LY onto the d-pad
    GNF_PULLUP = 1U << 3,          // add the RP2350's own pull-up to D0-D5, if a console's is weak
    GNF_OPEN_DRAIN = 1U << 4,      // never source a high, for a game that drives DB9 6/9 itself
};

#define GEN_DEADZONE_DEFAULT  48
#define GEN_RESET_US_DEFAULT  1500      // a real 6-button pad's counter timeout

// MODE takes PAD_SELECT too, since PAD_MODE is out of a GB byte's reach. MODE held at power-up
// forces a real 6-button pad into 3-button mode.
void gen_from_pad(const pad_state_t *p, uint8_t flags, uint8_t deadzone, gen_report_t *out);

#endif
