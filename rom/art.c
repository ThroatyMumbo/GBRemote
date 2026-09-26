#include "art.h"

static const volatile uint8_t *const win = ART_WIN_PTR;

static const volatile uint8_t *pal, *map, *attr;
static const volatile uint8_t *cx, *cy, *nb, *bidx, *cbtn, *coff, *cvar;
static const volatile uint8_t *boff, *bcell;
static uint16_t ntiles, tiles_off;
static uint8_t npals, nbtn, ncells, text_x, text_y;
static uint8_t bbyte[ART_MAX_BTN], bmask[ART_MAX_BTN];

// SDCC compiles a shift by a variable as a loop, and art_cell_tile() would run one per button.
static const uint8_t k_bit[ART_MAX_K] = { 1, 2, 4, 8 };

static uint8_t u8(uint16_t off) { return win[off]; }
static uint16_t u16(uint16_t off)
{
    return (uint16_t)(win[off] | ((uint16_t)win[off + 1] << 8));
}

uint8_t  art_npals(void)      { return npals; }
uint16_t art_ntiles(void)     { return ntiles; }
uint16_t art_tiles_addr(void) { return (uint16_t)(ART_BASE + tiles_off); }
uint8_t  art_ncells(void)     { return ncells; }
uint8_t  art_nbtn(void)       { return nbtn; }
uint8_t  art_text_x(void)     { return text_x; }
uint8_t  art_text_y(void)     { return text_y; }

const volatile uint8_t *art_pal(void)  { return pal; }
const volatile uint8_t *art_map(void)  { return map; }
const volatile uint8_t *art_attr(void) { return attr; }

uint8_t art_cell_x(uint8_t i) { return cx[i]; }
uint8_t art_cell_y(uint8_t i) { return cy[i]; }

uint8_t art_btn_first(uint8_t b) { return boff[b]; }
uint8_t art_btn_last(uint8_t b)  { return boff[b + 1]; }
uint8_t art_btn_cell(uint8_t j)  { return bcell[j]; }

// Every field is checked against the window rather than assumed. The counts here are what bound
// every index the caller then feeds back in, so a truncated or foreign blob can never reach a
// VRAM write with a wild tile number.
uint8_t art_parse(void)
{
    uint16_t total;
    uint8_t i, maxk;

    npals = nbtn = ncells = 0;
    if (u8(ARTH_MAGIC0) != ART_MAGIC0 || u8(ARTH_MAGIC1) != ART_MAGIC1) return 0;
    if (u8(ARTH_VERSION) != ART_VERSION || u8(ARTH_HDR_LEN) != ART_HDR_LEN) return 0;
    if (u8(ARTH_COLS) != ART_COLS || u8(ARTH_ROWS) != ART_ROWS) return 0;

    maxk = u8(ARTH_MAXK);
    if (maxk == 0 || maxk > ART_MAX_K) return 0;

    ntiles = u8(ARTH_NTILES);
    if (ntiles == 0) ntiles = ART_MAX_TILES;        // one byte cannot hold 256
    npals = u8(ARTH_NPALS);
    nbtn = u8(ARTH_NBTN);
    ncells = u8(ARTH_NCELLS);
    if (npals == 0 || npals > ART_MAX_PALS) goto bad;
    if (nbtn == 0 || nbtn > ART_MAX_BTN) goto bad;
    if (ncells == 0) goto bad;

    total = u16(ARTH_TOTAL_LEN);
    if (total > ART_WINDOW || total <= ART_HDR_LEN) goto bad;

    tiles_off = u16(ARTH_OFF_TILES);
    if (tiles_off != ART_OFF_TILES) goto bad;

    // Every table must start inside the blob. Checked before any of them is dereferenced.
    if (u16(ARTH_OFF_PAL) >= total || u16(ARTH_OFF_MAP) >= total ||
        u16(ARTH_OFF_ATTR) >= total || u16(ARTH_OFF_CELL_X) >= total ||
        u16(ARTH_OFF_CELL_Y) >= total || u16(ARTH_OFF_CELL_NB) >= total ||
        u16(ARTH_OFF_CELL_BIDX) >= total || u16(ARTH_OFF_CELL_BTN) >= total ||
        u16(ARTH_OFF_CELL_OFF) >= total || u16(ARTH_OFF_CELL_VAR) >= total ||
        u16(ARTH_OFF_BTN_BIT) >= total || u16(ARTH_OFF_BTN_OFF) >= total ||
        u16(ARTH_OFF_BTN_CELL) >= total) goto bad;

    pal   = win + u16(ARTH_OFF_PAL);
    map   = win + u16(ARTH_OFF_MAP);
    attr  = win + u16(ARTH_OFF_ATTR);
    cx    = win + u16(ARTH_OFF_CELL_X);
    cy    = win + u16(ARTH_OFF_CELL_Y);
    nb    = win + u16(ARTH_OFF_CELL_NB);
    bidx  = win + u16(ARTH_OFF_CELL_BIDX);
    cbtn  = win + u16(ARTH_OFF_CELL_BTN);
    coff  = win + u16(ARTH_OFF_CELL_OFF);
    cvar  = win + u16(ARTH_OFF_CELL_VAR);
    boff  = win + u16(ARTH_OFF_BTN_OFF);
    bcell = win + u16(ARTH_OFF_BTN_CELL);

    // The only variable shift in the module, and it runs nbtn times on a parse rather than per
    // frame. ART_MAX_BTN is what $001B-$001D can carry, so a blob naming a higher bit is refused.
    {
        const volatile uint8_t *bbit = win + u16(ARTH_OFF_BTN_BIT);
        for (i = 0; i < nbtn; i++) {
            uint8_t b = bbit[i];
            if (b >= ART_MAX_BTN) goto bad;
            bbyte[i] = (uint8_t)(b >> 3);
            bmask[i] = (uint8_t)(1u << (b & 7u));
        }
    }
    // Every cell's fan-out must fit the mask lookup, its buttons must be buttons that exist (they
    // index bbyte[]/bmask[], so a wild one reads past them), its variants must be inside the
    // variant table, and its position must be on the screen. What a variant NAMES is not checked:
    // a tile number past ntiles draws the wrong 8x8 and nothing worse, the packer asserts it, and
    // bank 0 has better uses for the bytes.
    {
        uint16_t nvar = (uint16_t)(total - u16(ARTH_OFF_CELL_VAR));
        for (i = 0; i < ncells; i++) {
            uint16_t at = (uint16_t)(coff[i * 2] | ((uint16_t)coff[i * 2 + 1] << 8));
            uint8_t j, k = nb[i];

            if (k == 0 || k > maxk) goto bad;
            if (cx[i] >= ART_COLS || cy[i] >= ART_ROWS) goto bad;
            if (at + (1u << k) > nvar) goto bad;
            for (j = 0; j < k; j++)
                if (cbtn[bidx[i] + j] >= nbtn) goto bad;
        }
        // The reverse index: scr_emu.c's art_step() feeds these back in as cell numbers.
        for (i = 0; i < boff[nbtn]; i++)
            if (bcell[i] >= ncells) goto bad;
    }

    text_x = u8(ARTH_TEXT_X);
    text_y = u8(ARTH_TEXT_Y);
    if (text_x >= ART_COLS || text_y >= ART_ROWS) goto bad;
    return 1;

bad:
    npals = nbtn = ncells = 0;
    return 0;
}

uint8_t art_cell_tile(uint8_t i, const uint8_t *live)
{
    const volatile uint8_t *b = cbtn + bidx[i];
    uint8_t j, v = 0, k = nb[i];

    for (j = 0; j < k; j++)
        if (live[bbyte[b[j]]] & bmask[b[j]]) v = (uint8_t)(v | k_bit[j]);
    return cvar[(uint16_t)(coff[i * 2] | ((uint16_t)coff[i * 2 + 1] << 8)) + v];
}

uint8_t art_btn_down(uint8_t b, const uint8_t *live)
{
    return (uint8_t)(live[bbyte[b]] & bmask[b]);
}
