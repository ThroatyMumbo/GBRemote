#include "snesmouse_proto.h"

#define STROBE_WORD 0xffffffffu

// A tap moves exactly one pixel; holding past HOLD_US ramps from V0 to the top speed over RAMP_US.
#define HOLD_US   180000u
#define RAMP_US   700000u
#define V0_PXS    60u
#define VMAX_PXS  360u
#define ACC_CAP   (SMOUSE_MAX << 8)     // with no console polling, motion must not bank up

// SELECT cycles the top speed; 57 px/s is under 1 px a frame, which Mario Paint's pencil needs.
static const uint16_t k_top_pxs[SMOUSE_SPEEDS] = {VMAX_PXS, 150U, 57U};

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

void smouse_reset(smouse_t *m) { *m = (smouse_t){ 0 }; }

// Whole pixels still owed, the fraction dropped: two axes that start in phase cross pixels together.
static int32_t whole_part(int32_t acc) { return acc >= 0 ? acc & ~0xff : -((-acc) & ~0xff); }

void smouse_tick(smouse_t *m, uint32_t b, uint32_t dt_us) {
    uint32_t dirs = b & PAD_DIRS;
    int sx = pad_dir(b, PAD_LEFT, PAD_RIGHT);
    int sy = pad_dir(b, PAD_UP, PAD_DOWN);

    bool sel = (b & PAD_SELECT) != 0;
    if (sel && !m->select && !(b & PAD_START)) { // START: the exit combo, not a cycle
        m->top = (uint8_t)((m->top + 1U) % SMOUSE_SPEEDS);
    }
    m->select = sel;
    uint32_t top = k_top_pxs[m->top % SMOUSE_SPEEDS];

    if (dirs != m->dirs) {
        uint32_t pressed = dirs & ~m->dirs;
        m->acc_x = whole_part(m->acc_x);            // else a diagonal staircases on old fractions
        m->acc_y = whole_part(m->acc_y);
        m->acc_x += 256 * pad_dir(pressed, PAD_LEFT, PAD_RIGHT);
        m->acc_y += 256 * pad_dir(pressed, PAD_UP, PAD_DOWN);
        if (!m->dirs || !dirs) {
            m->held_us = 0; // a turn keeps the ramp it earned
        }
        m->dirs = dirs;
    } else if (dirs) {
        uint32_t t = m->held_us;
        m->held_us = t + dt_us > t ? t + dt_us : t;
        if (m->held_us > HOLD_US) {
            uint32_t r = m->held_us - HOLD_US;
            uint32_t v = r >= RAMP_US ? VMAX_PXS : V0_PXS + (VMAX_PXS - V0_PXS) * r / RAMP_US;
            if (v > top) { v = top; }
            int32_t step = (int32_t)((uint64_t)v * dt_us * 256U / 1000000U);
            m->acc_x += sx * step;
            m->acc_y += sy * step;
        }
    }
    m->acc_x = clamp(m->acc_x, -ACC_CAP, ACC_CAP);
    m->acc_y = clamp(m->acc_y, -ACC_CAP, ACC_CAP);
    m->left  = (b & PAD_A) != 0;
    m->right = (b & PAD_B) != 0;
}

// Whole pixels only, toward zero: the fraction stays banked for the next frame.
static int whole(int32_t acc) { return acc >= 0 ? (int)(acc >> 8) : -(int)((-acc) >> 8); }

uint32_t smouse_report(const smouse_t *m) {
    return smouse_pack(whole(m->acc_x), whole(m->acc_y), m->left, m->right, m->speed);
}

void smouse_rx(smouse_t *m, uint32_t w) {
    if (w == STROBE_WORD) {
        m->speed = (uint8_t)((m->speed + 1U) % 3U);
        return;
    }
    int dx;
    int dy;
    bool l, r;
    unsigned s;
    smouse_unpack(w, &dx, &dy, &l, &r, &s);
    m->acc_x -= dx * 256;
    m->acc_y -= dy * 256;
}

static uint32_t rev7(uint32_t v) {
    uint32_t r = 0;
    for (int i = 0; i < 7; i++) {
        if (v & (1U << i)) { r |= 1U << (6 - i); }
    }
    return r;
}

// Sign-magnitude, sign first then MSB first: the console ROLs each bit in at the bottom.
static uint32_t pack_axis(int d) {
    int mag = d < 0 ? -d : d;
    if (mag > SMOUSE_MAX) { mag = SMOUSE_MAX; }
    return (d < 0 ? 1U : 0U) | (rev7((uint32_t)mag) << 1);
}

static int unpack_axis(uint32_t f) {
    int mag = (int)rev7((f >> 1) & 0x7f);
    return (f & 1U) ? -mag : mag;
}

uint32_t smouse_pack(int dx, int dy, bool left, bool right, unsigned speed) {
    return (right ? 1U << SMB_RIGHT : 0) | (left ? 1U << SMB_LEFT : 0) |
           (((speed >> 1) & 1U) << SMB_SPEED_HI) | ((speed & 1U) << SMB_SPEED_LO) |
           (1U << SMB_SIG) | (pack_axis(dy) << SMB_Y) | (pack_axis(dx) << SMB_X);
}

void smouse_unpack(uint32_t w, int *dx, int *dy, bool *left, bool *right, unsigned *speed) {
    *dy = unpack_axis((w >> SMB_Y) & 0xff);
    *dx = unpack_axis((w >> SMB_X) & 0xff);
    *left = (w >> SMB_LEFT) & 1U;
    *right = (w >> SMB_RIGHT) & 1U;
    *speed = (((w >> SMB_SPEED_HI) & 1U) << 1) | ((w >> SMB_SPEED_LO) & 1U);
}
