// pad_catalog.h — the driver catalog the GB menu's Controllers list reads from the served ROM at $0300.
// Written once at boot from compile-time pad_res_t, so it cannot tear; runtime state is $0012-$0014.
#ifndef PAD_CATALOG_H
#define PAD_CATALOG_H

#include <stdint.h>
#include "pad_driver.h"

#define CAT_PAGE_LEN     0x100
#define CAT_HDR_LEN      16
#define CAT_ENTRY_STRIDE 16
#define CAT_NAME_OFF     2
#define CAT_NAME_LEN     14     // NUL-terminated => 13 printable chars
#define CAT_MAX_ENTRIES  ((CAT_PAGE_LEN - CAT_HDR_LEN) / CAT_ENTRY_STRIDE)   // 15

#define CAT_MAGIC0  'D'
#define CAT_MAGIC1  'R'
#define CAT_VERSION 1

// Header byte offsets. stride/name_off/entry_off are published so a v1 ROM still parses a grown entry.
enum {
    CATH_MAGIC0   = 0,
    CATH_MAGIC1   = 1,
    CATH_VERSION  = 2,
    CATH_COUNT    = 3,
    CATH_STRIDE   = 4,
    CATH_NAME_OFF = 5,
    CATH_ENTRY_OFF = 6,
};

enum {
    CATF_SUPPORTED = 1U << 0, // pad_res_check() == PAD_OK on this build: clear = no PIO/SM budget
    CATF_PINLESS = 1U << 1,   // ctrl_mask == 0: a transport, needs no adapter cable
    CATF_NEEDS_5V = 1U << 2,  // the target must source CTRL_5V
    CATF_LOGIC_5V = 1U << 3,  // 5 V signaling
    CATF_EXCL_BLE = 1U << 4,  // cannot coexist with the CYW43 gSPI
};

// Fills exactly CAT_PAGE_LEN bytes of out. Skips PROTO_NONE; truncates at CAT_MAX_ENTRIES.
// Returns the number of entries written.
unsigned pad_catalog_build(uint8_t out[CAT_PAGE_LEN],
                           const pad_driver_t *const *drv, unsigned n);

#endif
