#include "mapinfo.h"

#define MPH_VERSION   2
#define MPH_NDRV      3
#define MPH_NLABELS   4
#define MPH_LABEL_LEN 5
#define MPH_LAB_OFF   6
#define MPH_DRV_OFF   8

#ifndef MP_PAGE_PTR
#define MP_PAGE_PTR ((const volatile uint8_t *)MP_BASE)
#endif
static const volatile uint8_t *const pg = MP_PAGE_PTR;

static uint16_t cur;                    // offset of the current driver entry; 0 = none

static uint16_t u16(uint8_t o) { return (uint16_t)(pg[o] | ((uint16_t)pg[o + 1] << 8)); }

uint8_t mp_find(uint8_t proto)
{
    uint16_t p;
    uint8_t d, n;

    cur = 0;
    if (pg[0] != 'M' || pg[1] != 'P' || pg[MPH_VERSION] != 1) return 0;
    if (pg[MPH_LABEL_LEN] == 0 || pg[MPH_LABEL_LEN] >= MP_LABEL_MAX) return 0;
    p = u16(MPH_DRV_OFF);
    for (d = 0; d < pg[MPH_NDRV]; d++) {
        if (p + 3 > MP_PAGE_LEN) return 0;
        n = pg[p + 2];
        if (p + 3 + 2 * (uint16_t)n > MP_PAGE_LEN) return 0;
        if (pg[p] == proto) { cur = p; return 1; }
        p += 3 + 2 * (uint16_t)n;
    }
    return 0;
}

uint8_t mp_map_off(void) { return cur ? pg[cur + 1] : 0; }
uint8_t mp_n(void)       { return cur ? pg[cur + 2] : 0; }

uint8_t mp_action(uint8_t i)
{
    return i < mp_n() ? pg[cur + 3 + 2 * (uint16_t)i] : 0xff;
}

void mp_label(uint8_t i, char *out)
{
    uint8_t id, len = pg[MPH_LABEL_LEN], j, c;
    uint16_t at;

    out[0] = 0;
    if (i >= mp_n()) return;
    id = pg[cur + 4 + 2 * (uint16_t)i];
    if (id >= pg[MPH_NLABELS]) return;
    at = u16(MPH_LAB_OFF) + (uint16_t)id * len;
    if (at + len > MP_PAGE_LEN) return;
    for (j = 0; j < len; j++) {
        c = pg[at + j];
        out[j] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
    }
    while (j && out[j - 1] == ' ') j--;
    out[j] = 0;
}

uint8_t mp_index_of(uint8_t action)
{
    uint8_t i, n = mp_n();

    for (i = 0; i < n; i++) if (mp_action(i) == action) return i;
    return 0xff;
}
