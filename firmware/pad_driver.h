// pad_driver.h — the controller-protocol driver interface.
#ifndef PAD_DRIVER_H
#define PAD_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "pad.h"

typedef enum {
    PAD_OK              =  0,
    PAD_ERR_NO_RESOURCE = -1,   // PIO words, SMs or DMA channels unavailable
    PAD_ERR_UNSAFE      = -2,   // ctrl_claim() refused the pins
    PAD_ERR_CONFIG      = -3,
    PAD_ERR_HW          = -4,
} pad_err_t;

// Stored in g_config[CFG_PROTO_SEL]; never renumber.
enum {
    PROTO_NONE      = 0,
    PROTO_N64       = 1,
    PROTO_SNES      = 2,
    PROTO_NES       = 3,
    PROTO_GENESIS   = 4,
    PROTO_DREAMCAST = 5,
    PROTO_FAMICOM   = 6,
    PROTO_LOOPY     = 7,
    PROTO_GCV2      = 8,
    PROTO_SNES_MOUSE = 9,
    PROTO_USB_HID   = 16,       // pinless transports share the vtable: ctrl_mask == 0
    PROTO_BLE_HID   = 17,
    PROTO_VIS       = 18,       // the Tandy VIS controller, out of the cart's own IR LED on GP46
};

// Every proto from here up needs no adapter cable, so a plugged-in cable never overrides it.
// drv_apply() refuses a driver whose ctrl_mask disagrees with its number.
#define PROTO_PINLESS_FIRST PROTO_USB_HID
static inline bool proto_pinless(uint8_t p) { return p >= PROTO_PINLESS_FIRST && p != 0xff; }

// Declared statically so drv_check() can refuse a driver before init() touches a pin.
typedef struct {
    uint8_t ctrl_mask;          // bit n: CTRLn (GP32+n) is used
    uint8_t ctrl_out_mask;      // subset of ctrl_mask that is ever driven
    uint8_t pio_sms;
    uint8_t pio_words;
    uint8_t dma_chans;
    uint8_t logic_5v : 1;       // signal lines are 5 V (informational, published as CATF_LOGIC_5V)
    uint8_t needs_5v : 1;       // target sources CTRL_5V
    // Spends the gSPI's PIO2 reservation; legal only because PROTO_BLE_HID can never be co-resident.
    uint8_t excl_ble : 1;
} pad_res_t;

// One entry of the GB mapping menu: a config_map.h pad_action byte, in the target console's words.
typedef struct {
    uint8_t     action;
    const char *label;          // <= PMI_LABEL_LEN chars
} pad_target_t;

#define PAD_TARGETS(t) .targets = (t), .ntargets = sizeof (t) / sizeof *(t)

// The shared rows of a target table; ACT_* come from config_map.h.
#define PT_DPAD  { ACT_PAD(PAD_UP), "Up" },   { ACT_PAD(PAD_DOWN), "Down" }, \
                 { ACT_PAD(PAD_LEFT), "Left" }, { ACT_PAD(PAD_RIGHT), "Right" }
#define PT_STICK { ACT_AXIS(AX_LY, 1), "Stick U" }, { ACT_AXIS(AX_LY, 0), "Stick D" }, \
                 { ACT_AXIS(AX_LX, 0), "Stick L" }, { ACT_AXIS(AX_LX, 1), "Stick R" }

typedef struct pad_driver pad_driver_t;

struct pad_driver {
    const char *name;           // log token, printed as drv=%s in the telemetry line
    const char *label;          // menu label for the published catalog, <= 13 chars. NULL => name.
    uint8_t     proto;
    pad_res_t   res;
    const pad_target_t *targets;    // what the projection consumes; NULL => the eight GB buttons
    uint8_t             ntargets;

    // init/deinit install and remove any IRQ handler or alarm pool: irq_set_enabled() is per-core.
    pad_err_t (*init)    (const pad_driver_t *d, const uint8_t *drv_cfg);
    void      (*deinit)  (const pad_driver_t *d);
    // May spin inside one protocol frame; returns after at most idle_us with no traffic.
    void      (*service) (const pad_driver_t *d, const pad_state_t *s, uint32_t idle_us);
    pad_err_t (*reconfig)(const pad_driver_t *d, const uint8_t *drv_cfg);
    bool      (*link_up) (const pad_driver_t *d);   // end-to-end: the console addressed us recently
};

// PIO2 held back for the CYW43 gSPI; the declared budget, pio_can_add_program() is still the real gate.
#define PIO2_RESERVED_WORDS 6
#define PIO2_RESERVED_SMS   1

#define PAD_LINK_MS 250

// A link_up() verdict: the console addressed us within PAD_LINK_MS. last_ms 0 means never.
static inline bool pad_link_recent(uint32_t last_ms, uint32_t now_ms) {
    return last_ms && (now_ms - last_ms) < PAD_LINK_MS;
}

// The one static resource verdict: the catalog and drv_check() both call it, so they cannot disagree.
pad_err_t pad_res_check(const pad_res_t *r);

const pad_driver_t *pad_driver_for(uint8_t proto);

// For the $0300 catalog; k_drivers[] is const, so fixed for the run.
unsigned                   pad_driver_count(void);
const pad_driver_t *const *pad_driver_table(void);

#endif
