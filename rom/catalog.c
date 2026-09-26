#include "catalog.h"

#define CATH_MAGIC0    0
#define CATH_MAGIC1    1
#define CATH_VERSION   2
#define CATH_COUNT     3
#define CATH_STRIDE    4
#define CATH_NAME_OFF  5
#define CATH_ENTRY_OFF 6

static const volatile uint8_t *const cat = (const volatile uint8_t *)CAT_BASE;

// The page never changes, and the divide below costs most of a VBlank (LY 149 -> 1 in the sim).
static uint8_t count_cache = 0xff;

uint8_t cat_ok(void)
{
    uint8_t stride = cat[CATH_STRIDE];

    if (cat[CATH_MAGIC0] != 'D' || cat[CATH_MAGIC1] != 'R') return 0;
    if (cat[CATH_VERSION] != 1) return 0;
    // A stride of 0 would divide by zero in cat_count(); one bigger than the name buffer would
    // let cat_name() read past `out`. entry_off needs no bound — it is a byte, so it is already
    // inside the page, and cat_count() divides the remaining room by stride.
    if (stride < 3 || stride > 64) return 0;
    return 1;
}

uint8_t cat_count(void)
{
    uint8_t n, max;

    if (count_cache != 0xff) return count_cache;    // stride >= 3 caps a real count at 85
    if (!cat_ok()) return count_cache = 0;
    n   = cat[CATH_COUNT];
    // The PAGE is the ceiling, not the count byte.
    max = (uint8_t)((CAT_PAGE_LEN - cat[CATH_ENTRY_OFF]) / cat[CATH_STRIDE]);
    return count_cache = n > max ? max : n;
}

static const volatile uint8_t *entry(uint8_t i)
{
    return cat + cat[CATH_ENTRY_OFF] + (uint16_t)i * (uint16_t)cat[CATH_STRIDE];
}

uint8_t cat_proto(uint8_t i)
{
    if (i >= cat_count()) return 0;
    return entry(i)[0];
}

uint8_t cat_flags(uint8_t i)
{
    if (i >= cat_count()) return 0;
    return entry(i)[1];
}

void cat_name(uint8_t i, char *out)
{
    const volatile uint8_t *e;
    uint8_t j, off, room, c;

    out[0] = 0;
    if (i >= cat_count()) return;

    e    = entry(i);
    off  = cat[CATH_NAME_OFF];
    room = (uint8_t)(cat[CATH_STRIDE] - off);
    if (room > CAT_NAME_MAX - 1) room = CAT_NAME_MAX - 1;

    for (j = 0; j < room; j++) {
        c = e[off + j];
        if (c == 0) break;
        out[j] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
    }
    out[j] = 0;
}
