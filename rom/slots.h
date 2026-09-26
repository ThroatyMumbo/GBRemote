// slots.h — reads the Game Boy ROM slot page the firmware publishes at $0400.
//
// Same contract as catalog.h: seeded once at boot before the console leaves reset, never
// rewritten, so it cannot tear. Still read through volatile and still validated, because a
// firmware too old to publish it leaves the page as $FF filler.
//
// Only POPULATED slots are emitted, so entry i is not slot i — slot_index(i) is the value to
// write to CFG_DRV + CFG_TPAK_SLOT. Which slot is selected lives in the config mirror, not here.
//
// Mirrors firmware/romstore.h.
#ifndef SLOTS_H
#define SLOTS_H

#include <stdint.h>

#define SLOT_BASE     0x0400
#define SLOT_PAGE_LEN 0x100
#define SLOT_NAME_MAX 18

// firmware/test/ points the parser at a host array with -DSLOT_PAGE_PTR=<name>; declare it here so
// slots.c needs no test-only include. The GB build never defines this and is unchanged.
#ifdef SLOT_PAGE_PTR
extern volatile uint8_t SLOT_PAGE_PTR[SLOT_PAGE_LEN];   // the test writes it; `pg` adds the const
#endif

#define SLOTF_PRESENT 0x01
#define SLOTF_RTC     0x02
#define SLOTF_CGB     0x04
#define SLOTF_SAVE    0x08

uint8_t slot_ok(void);          // magic and version recognised
uint8_t slot_count(void);       // 0 if absent; clamped to what the page can physically hold

uint8_t  slot_index(uint8_t i); // 1-8, the CFG_DRV byte value
uint8_t  slot_flags(uint8_t i);
uint8_t  slot_mbc(uint8_t i);
uint8_t  slot_ram_kb(uint8_t i);
uint16_t slot_banks(uint8_t i);
void     slot_name(uint8_t i, char *out);   // out[SLOT_NAME_MAX], NUL-terminated, sanitised

const char *slot_mbc_name(uint8_t mbc);

#endif
