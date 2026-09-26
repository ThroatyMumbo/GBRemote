// psram.h — APS6404 PSRAM (U3, CS on GP47), mapped as ordinary RAM at PSRAM_XIP_BASE once up.
#ifndef PSRAM_H
#define PSRAM_H

#include <stddef.h>
#include "pico/types.h"

#define PSRAM_CS_PIN   47u          // U3 pin 1, cuttable trace (hardware/gen/build_sch.py)
#define PSRAM_XIP_BASE 0x11000000u  // QMI CS1 memory-mapped window (M1)

// Size in bytes, or 0 if no APS6404 answered. Call with clk_sys final: M1 timing derives from it.
size_t psram_init(uint cs_pin);

#endif // PSRAM_H
