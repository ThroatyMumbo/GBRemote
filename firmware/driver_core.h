// driver_core.h — owns core1 and the core0<->core1 boundary; neither side ever blocks on the other.
#ifndef DRIVER_CORE_H
#define DRIVER_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "pad_driver.h"
#include "config_map.h"

#define PAD_STALE_MS   250      // no fresh GB state for this long -> release everything
#define DRV_SETTLE_MS  50       // coalesce a UI config burst before tearing a driver down

// core0, once, before drv_start().
void drv_init_core0(const uint8_t cfg[CFG_LEN]);

// core0. Launches core1 after rsthold_release(), so a hung driver kills the port, not the console.
void drv_start(void);

// core0, from the mailbox. Maps the GB byte through the compiled map and publishes.
void drv_pad_from_gb(uint8_t gb, uint32_t now_ms);

// core0. A canonical state composed by the caller, for bits the 8-entry map cannot reach; s->t_ms
// must be fresh or core1 zeroes it after PAD_STALE_MS.
void drv_pad_publish(const pad_state_t *s);

// The canonical buttons as last published, core0 only. pad.h's PAD_* bits, post-CFG_MAP.
uint32_t drv_pad_live(void);

// core0, on the /RST falling edge. Without this a GB reset leaves buttons held on the target.
void drv_pad_release(uint32_t now_ms);

// core0, after a config write or a cable change. cls is a CFG_CLASS_*; CFG_CLASS_MAP is handled
// entirely on core0 and never disturbs core1.
void drv_config_changed(const uint8_t cfg[CFG_LEN], uint8_t cls, uint32_t now_ms);
void drv_cable_changed(uint32_t now_ms);

// Either core. A wired driver may run: an ID is latched, or CFGF_ANY_CABLE overrides the open line.
bool drv_cable_ok(const uint8_t cfg[CFG_LEN]);

// core0, for telemetry and the status byte. All wait-free reads of core1's state.
bool        drv_link_up(void);
int8_t      drv_error(void);
const char *drv_name(void);
uint32_t    drv_service_count(void);

// What core1 actually owns, and a counter that ticks once per drv_apply(). The GB menu waits for
// the counter to move rather than guessing how long a protocol switch takes.
uint8_t     drv_resident_proto(void);
uint32_t    drv_apply_count(void);

#endif
