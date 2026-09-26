// slot_page.c — encodes the $0400 slot directory. No flash or SDK, so firmware/test/ round-trips
// it against rom/slots.c's parser.
#include "romstore.h"
#include <string.h>

void slot_page_build(uint8_t page[SLOT_PAGE_LEN], const gb_slot_hdr_t *const hdrs[GB_SLOTS]) {
    uint8_t n = 0;

    memset(page, 0, SLOT_PAGE_LEN);
    page[0] = 'G';
    page[1] = 'B';
    page[2] = 1;                                    // version
    page[4] = SLOT_ENTRY_STRIDE;
    page[5] = SLOT_NAME_OFF;
    page[6] = SLOT_HDR_LEN;

    for (uint8_t s = 1; s <= GB_SLOTS; s++) {
        const gb_slot_hdr_t *h = hdrs[s - 1];
        if (!h) { continue; }

        uint8_t *e = page + SLOT_HDR_LEN + (unsigned)n * SLOT_ENTRY_STRIDE;
        e[0] = s;
        e[1] = (uint8_t)(SLOTF_PRESENT
                         | (h->has_rtc ? SLOTF_RTC : 0)
                         | ((h->cgb_flag & 0x80) ? SLOTF_CGB : 0)
                         | (h->ram_len ? SLOTF_SAVE : 0));
        e[2] = h->mbc;
        e[3] = (uint8_t)(h->ram_len / 1024U); // 0 for MBC2's 512 bytes; SLOTF_SAVE says it
        e[4] = (uint8_t)h->rom_banks;
        e[5] = (uint8_t)(h->rom_banks >> 8);
        for (unsigned i = 0; i < SLOT_NAME_MAX - 1 && i < sizeof h->title; i++) {
            char c = h->title[i];
            if (!c) { break; }
            e[SLOT_NAME_OFF + i] = (c >= 0x20 && c < 0x7f) ? (uint8_t)c : ' ';
        }
        n++;
    }
    page[3] = n;
}
