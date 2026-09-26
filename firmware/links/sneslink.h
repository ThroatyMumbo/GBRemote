// sneslink.h — the SNES link's wire layer: arms sneslink.pio and moves words. What the words mean
// is sneslink_proto.h. SDK-only deps, so demos/ link it. Runs on core1; __not_in_flash_func.
#ifndef SNESLINK_H
#define SNESLINK_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"

// Claims 1 SM and 1 program and leaves both lines released. pio_set_gpio_base(pio, 16) must already
// have run. D1 is pin_d0 + 1; CLK and IOBIT are pin_latch + 1 and + 2.
bool sneslink_init(PIO pio, uint pin_d0, uint pin_latch);
void sneslink_deinit(void);            // lines back to inputs

void sneslink_arm(void);               // stopped at `start`, FIFOs empty, lines released
void sneslink_go(void);                // after the first TX words are in
bool sneslink_tx(uint32_t w);
bool sneslink_rx(uint32_t *w);

#endif
