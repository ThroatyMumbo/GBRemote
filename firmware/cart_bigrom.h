// cart_bigrom.h — bigrom.c on cartserve: bank_sel, the table, the trap and the fill. The owner
// keeps the bus stream and feeds it here in order.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "bigrom.h"

// slots[0..1] must be cs_bank(0..1), trap one more 16 KB bank; late_vector and pump may be NULL.
// After cs_start(), before cs_rsthold_release().
void cs_bigrom_start(const uint8_t *img, uint16_t banks, uint8_t mbc, bool has_rtc,
                     uint8_t *const *slots, uint8_t nslots, uint8_t *trap,
                     int (*late_vector)(void), void (*pump)(void));

// Fed in bus order. The arm is the DMA's, ~200 ns after the write; cs_bigrom_write only mirrors it.
void cs_bigrom_write(uint16_t addr, uint8_t v, uint32_t now_us);
void cs_bigrom_read(uint16_t addr, uint32_t now_us);
void cs_bigrom_tick(uint32_t now_us);

// True while the owner feeds cycles older than the console's present; the release waits.
void cs_bigrom_lagging(bool behind);

// Either /RST edge: back to bank 1 high, as a real MBC comes up; the cache is kept.
void cs_bigrom_reset(void);

// True while the console is parked: every bus cycle is the stall's, not the game's.
bool cs_bigrom_frozen(void);

const bigrom_t *cs_bigrom_stats(void);

// The SRAM slot holding this bank, or NULL: lets a reader skip XIP for resident banks.
const uint8_t *cs_bigrom_bank_ptr(void *user, uint16_t bank);
