#include "gen_proto.h"

static uint32_t dpad_nibble(const pad_state_t *p, uint8_t flags, uint8_t deadzone) {
    uint32_t d = pad_dpad(p, (flags & GNF_DPAD_FROM_AXIS) ? deadzone : -1,
                          (flags & GNF_CANCEL_OPPOSITE) != 0);
    return ((uint32_t)!!(d & PAD_UP)   << GEN_D0) | ((uint32_t)!!(d & PAD_DOWN)  << GEN_D1)
         | ((uint32_t)!!(d & PAD_LEFT) << GEN_D2) | ((uint32_t)!!(d & PAD_RIGHT) << GEN_D3);
}

void gen_from_pad(const pad_state_t *p, uint8_t flags, uint8_t deadzone, gen_report_t *out) {
    uint32_t b = p->buttons;

    uint32_t hi = dpad_nibble(p, flags, deadzone);       // cycles 1/3/5: U D L R
    uint32_t lo = (hi & 0x3U) | 0xcU;                    // cycles 2/4: U D then two driven zeros

    // Cycle 7's top row, in the retail SNES-to-Genesis adapters' convention.
    uint32_t xyz = 0;
    if (b & PAD_R) {
        xyz |= 1U << GEN_D0; // Z
    }
    if (b & PAD_X) {
        xyz |= 1U << GEN_D1; // Y
    }
    if (b & PAD_L) {
        xyz |= 1U << GEN_D2; // X
    }
    if (b & (PAD_MODE | PAD_SELECT)) {
        xyz |= 1U << GEN_D3; // MODE
    }
    if (b & PAD_ZR) { xyz |= 1U << GEN_D0; }
    if (b & PAD_ZL) { xyz |= 1U << GEN_D2; }

    uint32_t w = hi | (lo << 4) | (hi << 8) | (lo << 12) | (hi << 16);
    if (flags & GNF_THREE_BUTTON) {
        // A 3-button pad is combinational: no ID nibble, no top row, no cycle counter to keep.
        w |= (lo << 20) | (hi << 24) | (lo << 28);
    } else {
        w |= (0xfU << 20)    // cycle 6: wire 0000 = all four driven low
             | (xyz << 24)   // cycle 7: Z Y X MODE
             | (0x0U << 28); // cycle 8: wire 1111 = all four released
    }
    out->dpad = w;

    uint32_t tl_hi = (b & PAD_B) ? 1U : 0U;     // B
    uint32_t tr_hi = (b & PAD_A) ? 1U : 0U;     // C
    uint32_t tl_lo = (b & PAD_Y) ? 1U : 0U;     // A
    uint32_t tr_lo = (b & PAD_START) ? 1U : 0U; // START
    out->tltr = (tl_hi << GEN_TL) | (tr_hi << GEN_TR)
              | ((tl_lo << GEN_TL) << 2) | ((tr_lo << GEN_TR) << 2);
}
