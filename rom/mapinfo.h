// mapinfo.h — reads the map-target page the firmware publishes at $0500 (firmware/pad_mapinfo.h).
//
// Same contract as catalog.h: seeded once at boot, never rewritten, validated on every find.
#ifndef MAPINFO_H
#define MAPINFO_H

#include <stdint.h>

#define MP_BASE      0x0500
#define MP_PAGE_LEN  0x200
#define MP_LABEL_MAX 8          // 7 chars + NUL

#ifdef MP_PAGE_PTR
extern volatile uint8_t MP_PAGE_PTR[MP_PAGE_LEN];
#endif

uint8_t mp_find(uint8_t proto);        // 1 and makes it current, 0 if the page has no such driver
uint8_t mp_map_off(void);              // config index of its 8-byte map; 0 = it has none
uint8_t mp_n(void);
uint8_t mp_action(uint8_t i);
void    mp_label(uint8_t i, char *out);     // out[MP_LABEL_MAX], trailing spaces trimmed
uint8_t mp_index_of(uint8_t action);        // 0xff if the driver does not list it

#endif
