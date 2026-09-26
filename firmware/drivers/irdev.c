// irdev.c — keys by function select, which takes effect at once; a PWM level change waits for the wrap.
#include "irdev.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

static unsigned g_pin;
static unsigned g_slice;
static bool     g_up;

bool irdev_init(unsigned pin, uint32_t carrier_hz) {
    uint32_t wrap = clock_get_hz(clk_sys) / carrier_hz;
    if (wrap < 3 || wrap > 65536) { return false; }
    g_pin = pin;
    g_slice = pwm_gpio_to_slice_num(pin);

    gpio_init(pin);                 // SIO, output register 0: dark until the first mark
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&c, 1);
    pwm_config_set_wrap(&c, wrap - 1);
    pwm_init(g_slice, &c, false);
    pwm_set_gpio_level(pin, wrap / 3);   // 33% duty, the usual for a remote's LED
    pwm_set_enabled(g_slice, true);
    g_up = true;
    return true;
}

void irdev_deinit(void) {
    if (!g_up) { return; }
    gpio_set_function(g_pin, GPIO_FUNC_SIO);
    pwm_set_enabled(g_slice, false);
    gpio_init(g_pin);               // input again; R27 holds the FET off
    g_up = false;
}

void __not_in_flash_func(irdev_send)(uint32_t halves, unsigned n, uint32_t half_us) {
    uint32_t t = time_us_32();
    for (unsigned i = 0; i < n; i++) {
        gpio_set_function(g_pin, (halves >> (31 - i)) & 1U ? GPIO_FUNC_PWM : GPIO_FUNC_SIO);
        t += half_us;
        while ((int32_t)(time_us_32() - t) < 0) { ; }
    }
    gpio_set_function(g_pin, GPIO_FUNC_SIO);
}

void __not_in_flash_func(irdev_send_durations)(const uint16_t *d, unsigned n) {
    uint32_t t = time_us_32();
    for (unsigned i = 0; i < n; i++) {
        gpio_set_function(g_pin, i & 1U ? GPIO_FUNC_SIO : GPIO_FUNC_PWM);
        t += d[i];
        while ((int32_t)(time_us_32() - t) < 0) { ; }
    }
    gpio_set_function(g_pin, GPIO_FUNC_SIO);
}
