#include "pad_driver.h"
#include "pad_catalog.h"

extern const pad_driver_t drv_null;
extern const pad_driver_t drv_n64;
extern const pad_driver_t drv_dc;
extern const pad_driver_t drv_snes;
extern const pad_driver_t drv_nes;
extern const pad_driver_t drv_genesis;
extern const pad_driver_t drv_snes_mouse;
extern const pad_driver_t drv_vis;

// One line per target. A new protocol is two files plus this entry. Append only: the e2e scripts
// walk the Controllers menu by counting rows.
static const pad_driver_t *const k_drivers[] = {
    &drv_null, &drv_n64, &drv_dc, &drv_snes, &drv_nes, &drv_genesis, &drv_snes_mouse, &drv_vis,
};

// drv_null is not published, hence the +1.
#define N_DRIVERS (sizeof k_drivers / sizeof k_drivers[0])

_Static_assert(N_DRIVERS <= CAT_MAX_ENTRIES + 1,
               "k_drivers[] no longer fits the $0300 catalog page");

const pad_driver_t *pad_driver_for(uint8_t proto) {
    for (unsigned i = 0; i < N_DRIVERS; i++) {
        if (k_drivers[i]->proto == proto) { return k_drivers[i]; }
    }
    return NULL;
}

unsigned pad_driver_count(void) { return N_DRIVERS; }

const pad_driver_t *const *pad_driver_table(void) { return k_drivers; }
