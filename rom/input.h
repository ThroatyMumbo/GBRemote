// input.h — joypad edge/repeat for the menus, and the real-time hold timer for the exit combo.
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

// Fills *keys with the raw pad byte and returns the newly-pressed bits. Directions auto-repeat;
// A/B/START/SELECT never do, so a held button can never fire twice.
uint8_t in_poll(uint8_t *keys);

// Makes whatever is currently held read as not-pressed. ui.c calls this on every screen change,
// so the A that entered a screen is never seen again by the screen underneath.
void in_reset(void);

// rDIV ticks at 16384 Hz in SINGLE speed, so HOLD_TICKS is half a second. Accumulating DIV deltas
// makes the threshold real time rather than a loop-iteration count. The 8-bit delta is only safe
// because the poll loop runs far faster than DIV's 15.6 ms wrap — never call hold_tick() from
// anything that can stall for a whole wrap.
#ifndef HOLD_TICKS                  // a double-speed ROM passes 16384u: DIV runs twice as fast there
#define HOLD_TICKS 8192u
#endif

typedef struct { uint16_t acc; uint8_t last; } hold_t;

void    hold_reset(hold_t *h);
uint8_t hold_tick(hold_t *h);   // 1 once the accumulated hold passes HOLD_TICKS

#endif
