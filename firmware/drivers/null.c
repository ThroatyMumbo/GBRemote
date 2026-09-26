// null.c — PROTO_NONE, the boot and failure state: owns and drives nothing, so the port stays Hi-Z.
#include "pad_driver.h"
#include "pico/time.h"

static pad_err_t null_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d; (void)cfg;
    return PAD_OK;
}

static void null_deinit(const pad_driver_t *d) { (void)d; }

static void null_service(const pad_driver_t *d, const pad_state_t *s, uint32_t idle_us) {
    (void)d; (void)s;
    busy_wait_us_32(idle_us);
}

static pad_err_t null_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d; (void)cfg;
    return PAD_OK;
}

static bool null_link_up(const pad_driver_t *d) { (void)d; return false; }

const pad_driver_t drv_null = {
    .name = "none", .proto = PROTO_NONE, .res = { 0 },
    .init = null_init, .deinit = null_deinit, .service = null_service,
    .reconfig = null_reconfig, .link_up = null_link_up,
};
