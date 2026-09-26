#include "snes_proto.h"

// Bits at or above the pad's width stay zero; snesdev.pio drives past-the-end bits itself.
static uint32_t dpad(const pad_state_t *p, uint8_t flags, uint8_t deadzone,
                     unsigned b_up, unsigned b_down, unsigned b_left, unsigned b_right) {
    uint32_t d = pad_dpad(p, (flags & SNF_DPAD_FROM_AXIS) ? deadzone : -1,
                          (flags & SNF_CANCEL_OPPOSITE) != 0);
    return ((uint32_t)!!(d & PAD_UP)   << b_up)   | ((uint32_t)!!(d & PAD_DOWN)  << b_down)
         | ((uint32_t)!!(d & PAD_LEFT) << b_left) | ((uint32_t)!!(d & PAD_RIGHT) << b_right);
}

uint32_t snes_from_pad(const pad_state_t *p, uint8_t flags, uint8_t deadzone) {
    uint32_t b = p->buttons;
    uint32_t w = 0;

    if (b & PAD_B) { w |= 1U << SNES_B; }
    if (b & PAD_Y) { w |= 1U << SNES_Y; }
    if (b & PAD_SELECT) { w |= 1U << SNES_SELECT; }
    if (b & PAD_START) { w |= 1U << SNES_START; }
    if (b & PAD_A) { w |= 1U << SNES_A; }
    if (b & PAD_X) { w |= 1U << SNES_X; }
    if (b & PAD_L) { w |= 1U << SNES_L; }
    if (b & PAD_R) { w |= 1U << SNES_R; }
    // ZL/ZR fold onto the shoulders, for transports richer than the GB byte.
    if (b & PAD_ZL) { w |= 1U << SNES_L; }
    if (b & PAD_ZR) { w |= 1U << SNES_R; }

    return w | dpad(p, flags, deadzone, SNES_UP, SNES_DOWN, SNES_LEFT, SNES_RIGHT);
}

uint32_t nes_from_pad(const pad_state_t *p, uint8_t flags, uint8_t deadzone) {
    uint32_t b = p->buttons;
    uint32_t w = 0;

    if (b & PAD_A) { w |= 1U << NES_A; }
    if (b & PAD_B) { w |= 1U << NES_B; }
    if (b & PAD_SELECT) { w |= 1U << NES_SELECT; }
    if (b & PAD_START) { w |= 1U << NES_START; }
    // An NES pad has no X/Y, and a GB byte remapped onto them would otherwise vanish.
    if (b & PAD_X) { w |= 1U << NES_A; }
    if (b & PAD_Y) { w |= 1U << NES_B; }

    return w | dpad(p, flags, deadzone, NES_UP, NES_DOWN, NES_LEFT, NES_RIGHT);
}
