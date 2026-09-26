// pad_res.c — the static resource verdict, in its own file so host tests link it without the drivers.
#include "pad_driver.h"

pad_err_t pad_res_check(const pad_res_t *r) {
    // If BLE does hold PIO2, an excl_ble driver still fails init() at pio_can_add_program().
    unsigned words = 32U - (r->excl_ble ? 0U : PIO2_RESERVED_WORDS);
    unsigned sms = r->excl_ble ? 4U : 4U - PIO2_RESERVED_SMS;

    if (r->pio_sms > sms) { return PAD_ERR_NO_RESOURCE; }
    if (r->pio_words > words) { return PAD_ERR_NO_RESOURCE; }
    return PAD_OK;
}
