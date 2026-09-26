#include "config_map.h"

const pad_map_t pad_map_identity = {
    .action = { ACT_BTN(0), ACT_BTN(1), ACT_BTN(2), ACT_BTN(3),
                ACT_BTN(4), ACT_BTN(5), ACT_BTN(6), ACT_BTN(7) }
};

uint8_t cfg_get(const uint8_t *cfg, unsigned idx, uint8_t dflt) {
    if (!cfg) { return dflt; }
    uint8_t v = cfg[idx];
    return v == 0xffU ? dflt : v;
}

uint8_t cfg_get_nz(const uint8_t *cfg, unsigned idx, uint8_t dflt) {
    uint8_t v = cfg_get(cfg, idx, 0);
    return v ? v : dflt;
}

uint8_t cfg_proto_sel(const uint8_t cfg[CFG_LEN]) {
    return cfg_get(cfg, CFG_PROTO_SEL, 0);      // 0 == PROTO_NONE
}

const uint8_t *cfg_drv_block(const uint8_t cfg[CFG_LEN], uint8_t proto) {
    if (proto == 0 || cfg[CFG_DRV_OWNER] != proto) { return NULL; }
    return &cfg[CFG_DRV];
}

uint8_t cfg_map_off(uint8_t proto) {
    unsigned slot;
    if (proto >= 1 && proto <= 9) {
        slot = proto - 1U;
    } else if (proto >= 16 && proto <= 19) {
        slot = proto - 7U;
    } else {
        return 0;
    }
    return (uint8_t)(CFG_MAP + slot * CFG_MAP_LEN);
}

void map_compile(const uint8_t cfg[CFG_LEN], pad_map_t *out) {
    uint8_t off = cfg_map_off(cfg_proto_sel(cfg));
    for (int i = 0; i < CFG_MAP_LEN; i++) {
        uint8_t a = off ? cfg[off + i] : ACT_UNSET;
        out->action[i] = (a == ACT_UNSET) ? pad_map_identity.action[i] : a;
    }
}

void map_apply(const pad_map_t *m, uint8_t gb, pad_state_t *out) {
    int8_t ax[PAD_AXES] = { 0 };
    int pos[PAD_AXES] = {0};
    int neg[PAD_AXES] = {0};
    uint32_t btn = 0;

    for (int i = 0; i < CFG_MAP_LEN; i++) {
        if (!(gb & (1U << i))) { continue; }
        uint8_t a = m->action[i];
        if (a <= 0x1fU) {
            btn |= 1U << a;
        } else if (a >= 0x40U && a < 0x40U + PAD_AXES * 2) {
            uint8_t axis = (uint8_t)((a - 0x40U) >> 1);
            if (a & 1U) {
                pos[axis] = 1;
            } else {
                neg[axis] = 1;
            }
        }
    }
    for (int k = 0; k < PAD_AXES; k++) { // opposite directions cancel
        ax[k] = (int8_t)((pos[k] - neg[k]) * PAD_AXIS_FULL);
    }

    out->buttons = btn;
    for (int k = 0; k < PAD_AXES; k++) { out->axis[k] = ax[k]; }
    out->flags = PADF_FROM_GB;
    out->_rsv  = 0;
    out->t_ms  = 0;                                     // stamped by the publisher
}

uint8_t cfg_class_of(uint8_t idx) {
    if (idx == CFG_PROTO_SEL || idx == CFG_FLAGS) { return CFG_CLASS_PROTO; }
    if (idx >= CFG_MAP && idx < CFG_MAP_END) { return CFG_CLASS_MAP; }
    if (idx >= CFG_DRV_OWNER) { return CFG_CLASS_DRV; }
    return CFG_CLASS_NONE;
}
