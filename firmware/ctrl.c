#include "ctrl.h"
#include "hardware/gpio.h"

static uint8_t g_owned;

static void ctrl_pin_safe(uint p) {
    gpio_init(p);
    gpio_set_dir(p, GPIO_IN);
    gpio_disable_pulls(p);                                  // never fight a console's own pull
    // 4 mA into the 100R series resistor is enough for an open-drain low and caps output-vs-output
    // contention if a protocol is ever wrong; 12 mA would defeat the series resistors.
    gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(p, GPIO_SLEW_RATE_SLOW);
}

void ctrl_init_safe(void) {
    for (uint i = 0; i < PIN_CTRL_N; i++) { ctrl_pin_safe(PIN_CTRL(i)); }
    g_owned = 0;
}

bool ctrl_claim(uint8_t mask) {
    if (mask & g_owned) { return false; }
    g_owned |= mask;
    return true;
}

void ctrl_release(uint8_t mask) {
    for (uint i = 0; i < PIN_CTRL_N; i++) {
        if (mask & (1U << i)) { ctrl_pin_safe(PIN_CTRL(i)); }
    }
    g_owned &= (uint8_t)~mask;
}

uint8_t ctrl_owned(void) { return g_owned; }
