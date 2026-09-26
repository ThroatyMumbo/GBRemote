// romstore.h — the flash side of the Game Boy slot store. Core0 only: it dereferences XIP, which a
// config_save() erase takes away. Drivers include gb_stage.h instead (tools/run.sh greps for it).
#ifndef ROMSTORE_H
#define ROMSTORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gb_stage.h"

// 16 MB part. The firmware image is ~175 KB; config keeps the last sector (main.c).
//   0x000000  firmware                             512 KB reserved
//   0x080000  slot directory                       one 4 KB sector
//   0x100000  ROM arena                            13.75 MB, variable-size slots
//   0xEC0000  save areas, 8 x 32 KB fixed stride   256 KB
//   0xF00000  rescue save area                     32 KB
//   0xF10000  VMU card store, 2 slots              512 KB (vmu_store_fmt.h)
//   0xF90000  spare                                ~450 KB
//   0xFFF000  config                               4 KB
#define GB_DIR_OFF   0x080000u
#define GB_ROM_BASE  0x100000u
#define GB_ROM_LIMIT 0xEC0000u
#define GB_SAVE_BASE 0xEC0000u
#define GB_SAVE_SLOT 0x8000u
#define GB_RESCUE_OFF 0xF00000u     // a real cartridge's save as found, kept before a restore
#define GB_SLOT_STRIDE 0x200000u    // tools/mkslot.py's SLOT_STRIDE; the two allocators must agree
#define GB_SLOT_MAGIC 0x4D524247u   // 'GBRM' little-endian

static inline uint32_t save_off(uint8_t slot) {     // slots are 1-based
    return GB_SAVE_BASE + (uint32_t)(slot - 1) * GB_SAVE_SLOT;
}

typedef struct {
    uint32_t magic;
    uint32_t rom_off;       // absolute flash offset, 16 KB aligned
    uint32_t rom_len;
    uint32_t ram_len;       // 0..GB_SRAM_MAX
    uint16_t rom_banks;
    uint8_t  mbc;
    uint8_t  has_rtc;
    uint8_t  cgb_flag;
    char     title[17];
    uint8_t  pad[26];
} gb_slot_hdr_t;
// Pinned to tools/mkslot.py's explicit struct format.
_Static_assert(sizeof(gb_slot_hdr_t) == 64, "slot header must stay 64 bytes");
_Static_assert(offsetof(gb_slot_hdr_t, magic)     ==  0, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, rom_off)   ==  4, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, rom_len)   ==  8, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, ram_len)   == 12, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, rom_banks) == 16, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, mbc)       == 18, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, has_rtc)   == 19, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, cgb_flag)  == 20, "slot header layout");
_Static_assert(offsetof(gb_slot_hdr_t, title)     == 21, "slot header layout");

// The $0400 page the GB menu renders: seeded once at boot, never rewritten, so it cannot tear.
// The selected slot is in the config mirror, not here.
#define SLOT_PAGE_ADDR    0x0400
#define SLOT_PAGE_LEN     0x100
#define SLOT_HDR_LEN      16
#define SLOT_ENTRY_STRIDE 24
#define SLOT_NAME_OFF     6
#define SLOT_NAME_MAX     18

#define SLOTF_PRESENT 0x01
#define SLOTF_RTC     0x02
#define SLOTF_CGB     0x04
#define SLOTF_SAVE    0x08

_Static_assert(SLOT_HDR_LEN + GB_SLOTS * SLOT_ENTRY_STRIDE <= SLOT_PAGE_LEN,
               "the slot directory no longer fits its published page");

void romstore_init(void);

// Only populated slots are emitted, so entry i is not slot i — each entry carries its own index.
void romstore_build_page(uint8_t page[SLOT_PAGE_LEN]);

// The encoder alone, in slot_page.c: no flash, so firmware/test/ can round-trip it against the
// real rom/slots.c parser. hdrs[i] is NULL for an empty slot.
void slot_page_build(uint8_t page[SLOT_PAGE_LEN], const gb_slot_hdr_t *const hdrs[GB_SLOTS]);

// Select a slot (0 = none) and publish its info for core1. Cheap: header parse only.
void romstore_select(uint8_t slot);

// GB_SLOT_BUILTIN's cartridge: n 16 KB banks in RAM or flash, ROM only, no save. Title from $0134.
void romstore_set_builtin(const uint8_t *const *banks, uint16_t n);

// Reads the slot byte from drv_n64's CFG_DRV block; only core0 may parse the directory it names.
void romstore_config(const uint8_t *cfg);

// One bounded step of staging: a long core0 stall overflows write_capture's 8-word FIFO.
void romstore_poll(uint32_t now_ms);

// False while an N64 is using the pak: a W25Q128 sector erase takes XIP away for up to 400 ms.
bool romstore_flash_ok(uint32_t now_ms);

const char *romstore_slot_title(uint8_t slot);

// Flash's copy, not the session's. NULL with *len 0 for an empty slot, no save, or GB_SLOT_BUILTIN.
const uint8_t *romstore_rom(uint8_t slot, uint32_t *len);
const uint8_t *romstore_save(uint8_t slot, uint32_t *len);

// romstore_wr.c: filling a slot at run time, between N64 chunks; each call blocks for milliseconds.
// Placement is tools/mkslot.py's rule. Validates and latches the geometry; no flash touched yet.
bool romstore_wr_begin(uint8_t slot, uint32_t rom_len, uint32_t ram_len);

// Sequential only: `off` must be where the last call ended. Sectors are erased as first crossed.
bool romstore_wr_rom(uint32_t off, const uint8_t *buf, uint32_t len);
bool romstore_wr_save(uint32_t off, const uint8_t *buf, uint32_t len);
bool romstore_wr_rescue(uint32_t off, const uint8_t *buf, uint32_t len);

// Publishes the slot's header; until then the slot reads as before, so a failed dump is safe.
bool romstore_wr_commit(uint8_t slot, uint8_t mbc, bool has_rtc, uint8_t cgb_flag, const char *title);

void romstore_wr_abort(void);

#endif
