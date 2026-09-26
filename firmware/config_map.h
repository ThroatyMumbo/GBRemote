// config_map.h — the layout of main.c's g_config[256] (core0's; drivers get a snapshot) and the GB map.
// 0xff is erased flash: accessors read it as the default without rewriting it, so "unset" survives.
#ifndef CONFIG_MAP_H
#define CONFIG_MAP_H

#include <stdint.h>
#include "pad.h"

#define CFG_LEN 0x100

enum {
    CFG_VERSION   = 0x00,   // layout version; 0xff = wholly unprogrammed
    CFG_PROTO_SEL = 0x01,   // PROTO_*; 0x00 or 0xff => PROTO_NONE, nothing is driven
    CFG_FLAGS     = 0x02,   // see CFGF_*
    // 0x03-0x0f reserved, config plane
    CFG_MAP       = 0x10,   // one 8-byte map per proto, cfg_map_off(); N64's is this one
    CFG_MAP_END   = 0x78,   // 0x78-0x7f reserved
    CFG_DRV_OWNER = 0x80,   // PROTO_* that wrote the block below; mismatch => unprogrammed
    CFG_DRV       = 0x81,   // 127 bytes, meaning defined by the active driver
};

#define CFG_LAYOUT_VERSION 1
#define CFG_MAP_LEN        8
#define CFG_DRV_LEN        (CFG_LEN - CFG_DRV)

// CFGF_ANY_CABLE: start a wired driver with the ID line open, for a cable with no R_id fitted.
enum { CFGF_ANY_CABLE = 1U << 2 };

// pad_action byte: what one pressed GB bit does to the canonical state.
#define ACT_BTN(n)        ((uint8_t)(n))                   // 0x00-0x1f: set canonical button bit n
#define ACT_AXIS(ax, pos) ((uint8_t)(0x40u | ((ax) << 1) | ((pos) ? 1u : 0u)))   // 0x40-0x4b: full deflection
#define ACT_PAD(bit)      ACT_BTN(__builtin_ctz(bit))      // from a PAD_* mask
#define ACT_NONE          0xfeu                            // explicitly none
#define ACT_UNSET         0xffu                            // unprogrammed: the compiled default

#define PAD_AXIS_FULL 127   // canonical full deflection; a driver scales to its own gate

typedef struct { uint8_t action[CFG_MAP_LEN]; } pad_map_t;

// The default is the identity, GB bit i -> canonical bit i; target semantics belong to each driver.
extern const pad_map_t pad_map_identity;

// Flash offset of proto's map, stable forever: 1-9 -> 0x10.., 16-19 -> 0x58..; 0 = proto has none.
uint8_t cfg_map_off(uint8_t proto);

// Compiles the SELECTED proto's map, so a controller switch must recompile.
void map_compile(const uint8_t cfg[CFG_LEN], pad_map_t *out);
void map_apply(const pad_map_t *m, uint8_t gb, pad_state_t *out);

// cfg[idx], or dflt when cfg is NULL (a driver block that is not ours) or the byte is erased flash.
uint8_t cfg_get(const uint8_t *cfg, unsigned idx, uint8_t dflt);
uint8_t cfg_get_nz(const uint8_t *cfg, unsigned idx, uint8_t dflt);    // ... or 0
uint8_t cfg_proto_sel(const uint8_t cfg[CFG_LEN]);
const uint8_t *cfg_drv_block(const uint8_t cfg[CFG_LEN], uint8_t proto);   // NULL if not ours

// Which reconfigure class a written byte belongs to.
enum { CFG_CLASS_NONE = 0, CFG_CLASS_PROTO, CFG_CLASS_DRV, CFG_CLASS_MAP };
uint8_t cfg_class_of(uint8_t idx);

#endif
