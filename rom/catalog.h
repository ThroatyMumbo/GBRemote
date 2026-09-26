// catalog.h — reads the driver catalog the firmware publishes at $0300.
//
// The page is seeded once at boot, before the console leaves reset, and never rewritten — so
// unlike $0010-$0015 it cannot tear. It is still read through volatile and still validated,
// because a firmware too old to publish it leaves it as $FF filler.
//
// Mirrors firmware/pad_catalog.h. The header carries stride/name_off/entry_off so this ROM keeps
// parsing if a later firmware grows the entry: the loop is `p += stride`, never `p += 16`.
#ifndef CATALOG_H
#define CATALOG_H

#include <stdint.h>

#define CAT_BASE     0x0300
#define CAT_PAGE_LEN 0x100
#define CAT_NAME_MAX 14

#define CATF_SUPPORTED 0x01
#define CATF_PINLESS   0x02
#define CATF_NEEDS_5V  0x04
#define CATF_LOGIC_5V  0x08
#define CATF_EXCL_BLE  0x10

uint8_t cat_ok(void);       // magic and version recognised
uint8_t cat_count(void);    // 0 if absent; clamped to what the page can physically hold

uint8_t cat_proto(uint8_t i);
uint8_t cat_flags(uint8_t i);
void    cat_name(uint8_t i, char *out);   // out[CAT_NAME_MAX], NUL-terminated, sanitised

#endif
