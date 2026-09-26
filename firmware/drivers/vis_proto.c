// vis_proto.c — see vis_proto.h. The check field was solved from 41 captures; demos/irlearn verifies it.
#include "vis_proto.h"

#define VIS_RELEASE   0x0002u
#define VIS_HIGH_BYTE 0x3f00u

// Each button's frame with nothing else held, captured.
static const struct { uint16_t bit, alone; } k_alone[] = {
    {VIS_BTN_DOWN, 0x400d}, {VIS_BTN_1, 0x4016},  {VIS_BTN_A, 0x4025},    {VIS_BTN_2, 0x4045},
    {VIS_BTN_4, 0x4081},    {VIS_BTN_B, 0x4105},  {VIS_BTN_3, 0x4201},    {VIS_BTN_LEFT, 0x4401},
    {VIS_BTN_UP, 0x0801},   {VIS_BTN_RIGHT, 0x5001}, {VIS_PLAYER2, 0x6001},
};

// Every bit but 2 is release XOR each held button's (alone XOR release). Bit 2 also flips when an
// odd number of low-byte buttons is held with any high-byte one.
uint16_t vis_encode(uint16_t held) {
    uint16_t code = VIS_RELEASE;
    int lo = 0;
    int hi = 0;
    for (unsigned i = 0; i < sizeof k_alone / sizeof k_alone[0]; i++) {
        if (!(held & k_alone[i].bit)) { continue; }
        code ^= k_alone[i].alone ^ VIS_RELEASE;
        if (k_alone[i].bit & VIS_HIGH_BYTE) {
            hi++;
        } else {
            lo++;
        }
    }
    if ((lo & 1) && hi) { code ^= 1U << 2; }
    return code;
}

uint16_t vis_held_from_pad(const pad_state_t *p) {
    static const struct { uint32_t pad; uint16_t vis; } k_map[] = {
        {PAD_UP, VIS_BTN_UP},   {PAD_DOWN, VIS_BTN_DOWN}, {PAD_LEFT, VIS_BTN_LEFT},
        {PAD_RIGHT, VIS_BTN_RIGHT}, {PAD_A, VIS_BTN_A},   {PAD_B, VIS_BTN_B},
        {PAD_START, VIS_BTN_3}, {PAD_SELECT, VIS_BTN_4},  {PAD_X, VIS_BTN_1}, {PAD_Y, VIS_BTN_2},
    };
    uint16_t held = 0;
    for (unsigned i = 0; i < sizeof k_map / sizeof k_map[0]; i++) {
        if (p->buttons & k_map[i].pad) { held |= k_map[i].vis; }
    }
    if ((held & (VIS_BTN_UP | VIS_BTN_DOWN)) == (VIS_BTN_UP | VIS_BTN_DOWN)) {
        held &= ~(VIS_BTN_UP | VIS_BTN_DOWN);
    }
    return held;
}

uint32_t vis_halves(uint16_t code) {
    uint32_t h = 0;
    for (int i = VIS_FRAME_BITS - 1; i >= 0; i--) {
        uint32_t b = (code >> i) & 1U;
        h = (h << 2) | (b ? 0x1U : 0x2U);
    }
    return h;
}
