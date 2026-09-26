// gb_stage.h — the core0/core1 contract for a staged Game Boy cartridge; core1 includes it, so no XIP.
// Every field has one writer, and handshakes are monotonic counters, never a shared bitmask.
#ifndef GB_STAGE_H
#define GB_STAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/gbcart_emu.h"

#define GB_SLOTS      8
#define GB_SLOT_BUILTIN (GB_SLOTS + 1)  // an image the firmware registers, not the flash directory
#define GB_BANK_NONE  0xffffu    // never a legal bank, so have == want cannot be true spuriously

// Slot selector inside drv_n64's CFG_DRV block; core0 parses the flash directory it names.
#define GB_CFG_SLOT_OFF 3

// Transfer Pak writes to GB $8000-$9FFF (tpak_proto's feed), core1 -> core0.
#define GB_FEED_RING  16
#define GB_FEED_BLOCK 32
typedef struct { uint16_t off; uint8_t blk[GB_FEED_BLOCK]; } gb_feed_ent_t;

typedef struct {
    uint8_t  mbc;
    bool     has_rtc;
    uint16_t rom_banks;
    uint32_t ram_len;
    char     title[17];
    bool     valid;
} gb_slot_info_t;

typedef struct {
    // The handshake: core1 writes the MBC registers, core0 reads gbcart_want_bank() and publishes
    // win_bank[] only for the bank it actually copied.
    gbcart_t cart;

    // core1 is the only writer.
    volatile uint32_t epoch;        // ++ on attach after publishing, ++ on detach; odd = session live
    volatile uint32_t quiesce;      // ++ to make core0 abandon an in-flight copy
    volatile uint32_t flush_req;    // ++ to write the save back now, skipping debounce and busy gate
    volatile uint32_t busy_ms;      // last 0x02/0x03 only, so a poll-only game cannot block a config commit
    volatile uint32_t feed_head;    // ++ after feed[head % GB_FEED_RING] is written
    volatile uint32_t feed_drop;    // feed blocks the ring had no room for
    gb_feed_ent_t     feed[GB_FEED_RING];

    // The window's read half, core0 -> core1; a pointer so only an image that wants one pays for it.
    const uint8_t *volatile win;    // core0 writes; NULL = reads answer $FF, as a real pak does
    volatile uint32_t win_len;

    // core0 is the only writer.
    volatile uint8_t  slot;         // 0 = no pak, 1..GB_SLOTS, GB_SLOT_BUILTIN; the request, not the live session
    volatile uint32_t ready;        // ++ once the session's initial stage completed
    volatile uint32_t save_flushed; // echo of cart.save_dirty as of the last completed writeback
    volatile uint32_t quiesce_ack;
    volatile uint32_t flush_ack;
    volatile uint32_t feed_tail;
} gb_stage_t;

gb_stage_t *gb_stage(void);

void gb_feed_push(void *stage, uint16_t off, const uint8_t blk[GB_FEED_BLOCK]);   // core1
bool gb_feed_pop(uint16_t *off, uint8_t blk[GB_FEED_BLOCK]);                     // core0

// tpak_proto.c's window hook and its core0 arm; publish NULL to remove it, and hand over a static.
bool gb_win_pull(void *stage, uint16_t off, uint8_t out[GB_FEED_BLOCK]);         // core1
void gb_win_publish(const uint8_t *buf, uint32_t len);                           // core0

// core0 -> core1 seqlock, republished on a slot change; read from init()/reconfig(), never inside a frame.
bool gb_slot_info(gb_slot_info_t *out);
void gb_slot_info_publish(const gb_slot_info_t *v);

#endif
