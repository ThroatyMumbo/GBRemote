#include "pad_catalog.h"
#include <string.h>

static uint8_t cat_flags(const pad_driver_t *d) {
    uint8_t f = (pad_res_check(&d->res) == PAD_OK) ? CATF_SUPPORTED : 0;

    if (!d->res.ctrl_mask) { f |= CATF_PINLESS; }
    if (d->res.needs_5v) { f |= CATF_NEEDS_5V; }
    if (d->res.logic_5v) { f |= CATF_LOGIC_5V; }
    if (d->res.excl_ble) { f |= CATF_EXCL_BLE; }
    return f;
}

unsigned pad_catalog_build(uint8_t out[CAT_PAGE_LEN],
                           const pad_driver_t *const *drv, unsigned n) {
    memset(out, 0, CAT_PAGE_LEN);
    out[CATH_MAGIC0]    = CAT_MAGIC0;
    out[CATH_MAGIC1]    = CAT_MAGIC1;
    out[CATH_VERSION]   = CAT_VERSION;
    out[CATH_STRIDE]    = CAT_ENTRY_STRIDE;
    out[CATH_NAME_OFF]  = CAT_NAME_OFF;
    out[CATH_ENTRY_OFF] = CAT_HDR_LEN;

    unsigned k = 0;
    for (unsigned i = 0; i < n && k < CAT_MAX_ENTRIES; i++) {
        const pad_driver_t *d = drv[i];
        // The ROM synthesizes its own "Off" row for PROTO_NONE.
        if (!d || d->proto == PROTO_NONE) { continue; }

        uint8_t *e = out + CAT_HDR_LEN + k * CAT_ENTRY_STRIDE;
        e[0] = d->proto;
        e[1] = cat_flags(d);

        const char *s = d->label ? d->label : d->name;
        for (unsigned j = 0; s && j + 1 < CAT_NAME_LEN && s[j]; j++) {
            e[CAT_NAME_OFF + j] = (uint8_t)s[j];
        }
        k++;
    }
    out[CATH_COUNT] = (uint8_t)k;
    return k;
}
