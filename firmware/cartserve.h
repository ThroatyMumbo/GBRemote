// cartserve.h — the cart-bus serve path: PIO0 a15_detect -> rom_low/rom_high -> serve_data fed by
// two DMA rings, PIO1 write_capture, and the /RST hold. Shared by the product and demos/.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hardware/pio.h"

#define CS_SM_WCAP 0                    // write_capture's SM on pio1
#define CS_SM_BSEL 1                    // bank_sel's SM on pio1, cart_mbc.c only

extern const uint8_t cs_wire_pos[14];   // GB address bit i -> GPIO offset from PIN_A0

uint16_t cs_permute14(uint16_t a);

// Boot order: rsthold_assert, ctrl_init_safe, set_operating_point, arm_failsafe, bus_priority,
// stdio, load/seed, start, release.
void cs_rsthold_assert(void);
void cs_rsthold_arm_failsafe(void);
void cs_rsthold_release(void);
void cs_set_operating_point(void);
void cs_bus_priority(void);

uint8_t *cs_bank(int high);                                     // the two static 16 KB banks
void cs_load(uint8_t *dst, const uint8_t *img, uint32_t len);   // permuted, $FF past len
void cs_seed(uint16_t addr, uint8_t v);                         // low bank, before cs_start() only
void cs_start(void);                                            // serve + capture running; /RST still held

void cs_poke(uint16_t addr, uint8_t v);                         // low bank, under the running console
void cs_poke_bank(uint8_t *base, uint16_t addr, uint8_t v);     // ... or any served bank

// Bulk into a bank nothing is serving: no CLK gate, and the permute is two table lookups rather
// than cs_permute14()'s bit loop. Valid once cs_start() has built the tables.
void cs_write_span(uint8_t *bank, uint16_t addr, const uint8_t *src, uint32_t len);
void cs_mount_high(const uint8_t *bank16k);                     // 16 KB-aligned, already permuted
volatile uint32_t *cs_rom_base(void);                           // {low, high} serve bases, 8-aligned

// bank_sel + a 3-channel DMA chain copying the 8-byte entry at *tab_base + (byte << 3) into both
// serve bases on every $2000 write. *tab_base is read on every select, so it must outlive the chain.
void cs_bank_sel_arm(const uint32_t *tab_base, int *copy_ch, int *trig_ch);

// wcap_ring.c, demos only: a lossless DMA drain instead of pio_sm_get. Start before release.
void cs_wcap_ring_start(void);
bool cs_wcap_ring_next(uint32_t *w);
uint32_t cs_wcap_ring_maxdepth(void);

// Decodes a write_capture word: false for A15 writes, else the un-permuted address and data.
bool cs_wcap_decode(uint32_t w, uint16_t *addr, uint8_t *data);
