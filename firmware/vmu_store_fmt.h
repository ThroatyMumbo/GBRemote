// vmu_store_fmt.h — the card store's on-flash format. Pure, so firmware/test/ drives the real
// encoder, the same split slot_page.c has from romstore.c.
#ifndef VMU_STORE_FMT_H
#define VMU_STORE_FMT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vmu_stage.h"

// Two slots alternate and the header sector is written last, so a reset mid-write can only cost
// the newest save. 256 KB apart to keep each slot's header in a sector of its own.
#define VMU_STORE_OFF    0xF10000u
#define VMU_SLOT_STRIDE  0x40000u
#define VMU_SLOT_IMG     0x1000u        // the header takes the first sector; the card follows
#define VMU_SLOTS        2u
#define VMU_HDR_MAGIC    0x43554D56u    // 'VMUC'

typedef struct {
    uint32_t magic, seq, len, crc;
    uint32_t wall_secs;                 // the card's own clock when this was written
    uint32_t writes;                    // write phases the image has taken since boot
    uint32_t pad[2];
} vmu_hdr_t;

_Static_assert(sizeof(vmu_hdr_t) == 32, "the slot header must stay 32 bytes");
_Static_assert(offsetof(vmu_hdr_t, seq)  ==  4, "slot header layout");
_Static_assert(offsetof(vmu_hdr_t, len)  ==  8, "slot header layout");
_Static_assert(offsetof(vmu_hdr_t, crc)  == 12, "slot header layout");

uint32_t vmu_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

bool vmu_hdr_ok(const vmu_hdr_t *h);

// -1 when neither slot holds a card, else the newer one. Compared as a difference so the
// sequence number may wrap without picking the wrong slot.
int vmu_hdr_pick(const vmu_hdr_t *a, const vmu_hdr_t *b);

#endif
