#include <stdio.h>
#include "cable_id.h"
#include "pinmap.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/uart.h"

#define REPORT_US 1000000u

static bool     g_up;
static uint32_t g_last_us;

void cable_id_report(void) {
    uint32_t now = time_us_32();
    if (g_up && now - g_last_us < REPORT_US) return;
    g_last_us = now;
    if (!g_up) {
        g_up = true;
        cable_id_init();
        if (!uart_is_enabled(uart_default)) {       // USB-stdio images never set J4 up
            uart_init(uart_default, 115200);
            gpio_set_function(PICO_DEFAULT_UART_TX_PIN,
                              UART_FUNCSEL_NUM(uart_default, PICO_DEFAULT_UART_TX_PIN));
        }
    }
    adc_select_input(CTRL_ID_ADC);                  // the image may use the ADC for something else
    cable_id_tick();
    char line[32];
    int n = snprintf(line, sizeof line, "ctrl_id=%umV slot=%u\r\n", cable_id_mv(), cable_id_slot());
    uart_write_blocking(uart_default, (const uint8_t *)line, (size_t)n);
}
