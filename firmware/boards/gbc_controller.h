// Also included by the assembler: preprocessor directives only.

// gbc_controller — rev A: RP2350B (U1), W25Q128JVSIQ flash (U2), APS6404L PSRAM (U3). Never include a
// module header: its default-peripheral pins (LED GP25, I2C, SPI, UART0) all land on the cart bus.

#ifndef _BOARDS_GBC_CONTROLLER_H
#define _BOARDS_GBC_CONTROLLER_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)

#define PICO_RP2350A 0                  // QFN-80: GP0-47

// UART1 on the J4 debug header: TX GP40, RX GP5, both funcsel F2.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 1
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 40
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 5
#endif

// Deliberately undefined: PICO_DEFAULT_LED_PIN, PICO_DEFAULT_I2C*, PICO_DEFAULT_SPI*,
// PICO_SMPS_MODE_PIN, PICO_VBUS_PIN, PICO_VSYS_PIN.

// RM2 (U7) as on pico2_w.h: DI/DO/nIRQ share GP45 via R34/R35, WL_ON and BT_ON share GP42.
// Never define CYW43_WL_GPIO_LED/SMPS/VBUS_PIN: on RM2 those GPIOs go to TP16-TP18 and are ours.
#define PICO_CYW43_SUPPORTED 1
#define CYW43_PIN_WL_DYNAMIC 0
#define CYW43_WL_GPIO_COUNT 3
#define CYW43_DEFAULT_PIN_WL_REG_ON    42u
#define CYW43_DEFAULT_PIN_WL_CS        43u
#define CYW43_DEFAULT_PIN_WL_CLOCK     44u
#define CYW43_DEFAULT_PIN_WL_DATA_OUT  45u
#define CYW43_DEFAULT_PIN_WL_DATA_IN   45u
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 45u

// U2 flash, 16 MB; config_save() uses the last sector.
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
