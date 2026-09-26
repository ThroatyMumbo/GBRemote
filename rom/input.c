#include <gb/gb.h>
#include <gb/hardware.h>
#include "input.h"

#define REP_DELAY 24    // frames before auto-repeat kicks in, ~400 ms
#define REP_RATE   5    // frames between repeats, ~12/s

static uint8_t prev, rep_key, rep_ct;

uint8_t in_poll(uint8_t *keys)
{
    uint8_t k   = joypad();
    uint8_t hit = (uint8_t)(k & (uint8_t)~prev);
    uint8_t dir = (uint8_t)(k & (J_UP | J_DOWN | J_LEFT | J_RIGHT));

    prev = k;

    // Directions only. Repeating A would double-fire a selection, and manufacturing a menu event
    // out of a held combination is how the exit combo would start selecting things.
    if (dir != rep_key) { rep_key = dir; rep_ct = REP_DELAY; }
    else if (dir && --rep_ct == 0) { rep_ct = REP_RATE; hit |= dir; }

    *keys = k;
    return hit;
}

void in_reset(void) { prev = 0xff; rep_key = 0; rep_ct = 0; }

void hold_reset(hold_t *h) { h->acc = 0; h->last = DIV_REG; }

uint8_t hold_tick(hold_t *h)
{
    uint8_t now = DIV_REG;
    uint8_t d   = (uint8_t)(now - h->last);     // 8-bit delta, wrap-correct

    h->last = now;
    if (h->acc >= HOLD_TICKS) return 1;         // saturate, so a long hold cannot wrap acc
    h->acc = (uint16_t)(h->acc + d);
    return h->acc >= HOLD_TICKS;
}
