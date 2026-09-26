#include "slots.h"

#define SLH_MAGIC0    0
#define SLH_MAGIC1    1
#define SLH_VERSION   2
#define SLH_COUNT     3
#define SLH_STRIDE    4
#define SLH_NAME_OFF  5
#define SLH_ENTRY_OFF 6

// firmware/test/ compiles this file with -DSLOT_PAGE_PTR=<a host array> to run the real parser
// against the real encoder. An array name is an address constant, so `pg` stays const-initialised
// and the GB build folds it exactly as before — the override costs the cart nothing.
#ifndef SLOT_PAGE_PTR
#define SLOT_PAGE_PTR ((const volatile uint8_t *)SLOT_BASE)
#endif
static const volatile uint8_t *const pg = SLOT_PAGE_PTR;

uint8_t slot_ok(void)
{
    uint8_t stride = pg[SLH_STRIDE];

    if (pg[SLH_MAGIC0] != 'G' || pg[SLH_MAGIC1] != 'B') return 0;
    if (pg[SLH_VERSION] != 1) return 0;
    // A stride of 0 would divide by zero in slot_count(); one past the name buffer would let
    // slot_name() run off `out`. Same bounds as catalog.c, same reasons.
    if (stride < 7 || stride > 64) return 0;
    return 1;
}

uint8_t slot_count(void)
{
    uint8_t n, max;

    if (!slot_ok()) return 0;
    n   = pg[SLH_COUNT];
    max = (uint8_t)((SLOT_PAGE_LEN - pg[SLH_ENTRY_OFF]) / pg[SLH_STRIDE]);
    return n > max ? max : n;
}

static const volatile uint8_t *entry(uint8_t i)
{
    return pg + pg[SLH_ENTRY_OFF] + (uint16_t)i * (uint16_t)pg[SLH_STRIDE];
}

uint8_t slot_index(uint8_t i)  { return i >= slot_count() ? 0 : entry(i)[0]; }
uint8_t slot_flags(uint8_t i)  { return i >= slot_count() ? 0 : entry(i)[1]; }
uint8_t slot_mbc(uint8_t i)    { return i >= slot_count() ? 0 : entry(i)[2]; }
uint8_t slot_ram_kb(uint8_t i) { return i >= slot_count() ? 0 : entry(i)[3]; }

uint16_t slot_banks(uint8_t i)
{
    const volatile uint8_t *e;
    if (i >= slot_count()) return 0;
    e = entry(i);
    return (uint16_t)e[4] | ((uint16_t)e[5] << 8);
}

void slot_name(uint8_t i, char *out)
{
    const volatile uint8_t *e;
    uint8_t j, off, room, c;

    out[0] = 0;
    if (i >= slot_count()) return;

    e    = entry(i);
    off  = pg[SLH_NAME_OFF];
    room = (uint8_t)(pg[SLH_STRIDE] - off);
    if (room > SLOT_NAME_MAX - 1) room = SLOT_NAME_MAX - 1;

    for (j = 0; j < room; j++) {
        c = e[off + j];
        if (c == 0) break;
        out[j] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
    }
    out[j] = 0;
}

const char *slot_mbc_name(uint8_t mbc)
{
    switch (mbc) {
    case 0:  return "none";
    case 1:  return "MBC1";
    case 2:  return "MBC2";
    case 3:  return "MBC3";
    case 4:  return "MBC5";
    default: return "?";
    }
}
