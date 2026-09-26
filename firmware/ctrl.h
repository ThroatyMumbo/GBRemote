// ctrl.h — ownership of the controller-port pins CTRL0-7 = GP32-39. An unclaimed pin is always an
// input with pulls off: unknown cables get plugged in here.
#ifndef CTRL_H
#define CTRL_H

#include <stdint.h>
#include <stdbool.h>
#include "pinmap.h"

// All eight pins to a known-safe state. Runs before the clock change, so nothing that can fail
// happens first — same fail-closed reasoning as the /RST hold.
void ctrl_init_safe(void);

// Hand pins to a driver. Refuses (returns false, claims nothing) if any of them is already owned.
bool ctrl_claim(uint8_t mask);

// Back to the ctrl_init_safe() state. driver_core calls this after every deinit(), so a driver
// that forgets still cannot leave a pin driven.
void ctrl_release(uint8_t mask);

uint8_t ctrl_owned(void);

#endif
