#include "pad_mapinfo.h"
#include "config_map.h"
#include <string.h>

static const pad_target_t k_gb_buttons[] = {
    { ACT_BTN(0), "Right" }, { ACT_BTN(1), "Left" }, { ACT_BTN(2), "Up" },     { ACT_BTN(3), "Down" },
    { ACT_BTN(4), "A" },     { ACT_BTN(5), "B" },    { ACT_BTN(6), "Select" }, { ACT_BTN(7), "Start" },
};

static int label_id(char tab[][PMI_LABEL_LEN], unsigned *nl, const char *s) {
    char pad[PMI_LABEL_LEN];
    memset(pad, ' ', sizeof pad);
    for (unsigned j = 0; s && j < PMI_LABEL_LEN && s[j]; j++) { pad[j] = s[j]; }

    for (unsigned i = 0; i < *nl; i++) {
        if (!memcmp(tab[i], pad, PMI_LABEL_LEN)) { return (int)i; }
    }
    if (*nl >= PMI_MAX_LABELS) { return -1; }
    memcpy(tab[*nl], pad, PMI_LABEL_LEN);
    return (int)(*nl)++;
}

unsigned pad_mapinfo_build(uint8_t out[PMI_LEN], const pad_driver_t *const *drv, unsigned n) {
    static char labels[PMI_MAX_LABELS][PMI_LABEL_LEN];
    static uint8_t ents[PMI_LEN];
    unsigned nl = 0;
    unsigned ne = 0;
    unsigned k = 0;

    memset(out, 0, PMI_LEN);
    for (unsigned i = 0; i < n; i++) {
        const pad_driver_t *d = drv[i];
        if (!d || d->proto == PROTO_NONE) { continue; }

        const pad_target_t *t = d->targets ? d->targets : k_gb_buttons;
        unsigned nt = d->targets ? d->ntargets : sizeof k_gb_buttons / sizeof *k_gb_buttons;
        unsigned nl_was = nl;
        unsigned need = 3 + 2 * nt;

        uint8_t e[3 + 2 * 255];
        e[0] = d->proto;
        e[1] = cfg_map_off(d->proto);
        e[2] = (uint8_t)nt;
        bool ok = true;
        for (unsigned j = 0; j < nt && ok; j++) {
            int id = label_id(labels, &nl, t[j].label);
            if (id < 0) { ok = false; break; }
            e[3 + 2 * j] = t[j].action;
            e[4 + 2 * j] = (uint8_t)id;
        }
        if (!ok || PMI_HDR_LEN + nl * PMI_LABEL_LEN + ne + need > PMI_LEN) { nl = nl_was; break; }
        memcpy(ents + ne, e, need);
        ne += need;
        k++;
    }

    unsigned lab_off = PMI_HDR_LEN;
    unsigned drv_off = lab_off + nl * PMI_LABEL_LEN;
    out[PMIH_MAGIC0]    = 'M';
    out[PMIH_MAGIC1]    = 'P';
    out[PMIH_VERSION]   = PMI_VERSION;
    out[PMIH_NDRV]      = (uint8_t)k;
    out[PMIH_NLABELS]   = (uint8_t)nl;
    out[PMIH_LABEL_LEN] = PMI_LABEL_LEN;
    out[PMIH_LAB_OFF]     = (uint8_t)lab_off;
    out[PMIH_LAB_OFF + 1] = (uint8_t)(lab_off >> 8);
    out[PMIH_DRV_OFF]     = (uint8_t)drv_off;
    out[PMIH_DRV_OFF + 1] = (uint8_t)(drv_off >> 8);
    memcpy(out + lab_off, labels, nl * PMI_LABEL_LEN);
    memcpy(out + drv_off, ents, ne);
    return k;
}
