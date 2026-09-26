// flash_burn.h — the one erase-and-program sequence. `erase_len` 0 skips the erase, `len` 0 the program.
#pragma once
#include <stdint.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

static inline void flash_burn(uint32_t off, uint32_t erase_len, const uint8_t *buf, uint32_t len) {
    uint32_t ints = save_and_disable_interrupts();
    if (erase_len) { flash_range_erase(off, erase_len); }
    if (len) { flash_range_program(off, buf, len); }
    restore_interrupts(ints);
}
