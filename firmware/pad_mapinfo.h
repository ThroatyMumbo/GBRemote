// pad_mapinfo.h — each driver's mapping-menu targets, published once at boot into the served ROM at $0500.
// Mirrored by rom/mapinfo.h.
#ifndef PAD_MAPINFO_H
#define PAD_MAPINFO_H

#include <stdint.h>
#include "pad_driver.h"

#define PMI_ADDR       0x0500
#define PMI_LEN        0x200
#define PMI_HDR_LEN    16
#define PMI_LABEL_LEN  7        // space-padded, not NUL-terminated
#define PMI_MAX_LABELS 64
#define PMI_VERSION    1

// Header: 'M' 'P' version ndrv nlabels label_len lab_off(le16) drv_off(le16).
// Label table: nlabels x label_len. Driver entries, packed: proto map_off n, n x (action label_id).
enum {
    PMIH_MAGIC0 = 0, PMIH_MAGIC1 = 1, PMIH_VERSION = 2, PMIH_NDRV = 3,
    PMIH_NLABELS = 4, PMIH_LABEL_LEN = 5, PMIH_LAB_OFF = 6, PMIH_DRV_OFF = 8,
};

// Fills exactly PMI_LEN bytes. Skips PROTO_NONE; stops at the first driver that would not fit.
// Returns the number of drivers written.
unsigned pad_mapinfo_build(uint8_t out[PMI_LEN], const pad_driver_t *const *drv, unsigned n);

#endif
