#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "canvas.h"

#define SPACE 0                         // the console font's blank, what cls() leaves behind
#define CELLS (CV_ROWS * CV_COLS)
#define NO_SPAN 0xff

static uint8_t want[CELLS], shown[CELLS];
static uint8_t has_cgb;
static uint8_t span_x[CV_ROWS], span_pal[CV_ROWS];   // cv_attr_row()'s cache; span_pal NO_SPAN = none

static const palette_color_t k_pal[8][4] = {
    { RGB(31, 31, 31), RGB(21, 21, 21), RGB(10, 10, 10), RGB(0, 0, 0) },
    { RGB(31, 31, 31), RGB(0, 26, 8),   RGB(10, 10, 10), RGB(0, 0, 0) },
    { RGB(31, 31, 31), RGB(31, 26, 0),  RGB(10, 10, 10), RGB(0, 0, 0) },
    { RGB(31, 31, 31), RGB(31, 6, 0),   RGB(10, 10, 10), RGB(0, 0, 0) },
    // The console font draws its glyphs in colour 3, so a row of spaces here is a solid bar.
    { RGB(3, 6, 17),   RGB(8, 13, 24),  RGB(20, 24, 31), RGB(31, 31, 31) },
    { RGB(27, 29, 31), RGB(19, 23, 29), RGB(10, 12, 16), RGB(0, 0, 0) },
    { RGB(27, 29, 31), RGB(19, 23, 29), RGB(10, 12, 16), RGB(0, 17, 4) },
    { RGB(27, 29, 31), RGB(19, 23, 29), RGB(10, 12, 16), RGB(22, 0, 0) },
};

// CGB mode installs no palette and ignores BGP_REG; without this it is white on white.
void cv_init(void)
{
    has_cgb = (uint8_t)(_cpu == CGB_TYPE);
    set_bkg_data(CV_T_BASE, CV_T_COUNT, k_cv_tiles);
    cv_pal_load();
}

void cv_pal_load(void)
{
    uint8_t i;

    if (!has_cgb) return;
    for (i = 0; i < 8; i++) set_bkg_palette(i, 1, k_pal[i]);
}

// With the display off, straight into VRAM: set_bkg_tiles() waits for an HBlank that never comes.
// It is also what makes a screen worth building behind DISPLAY_OFF — a safe write is ~0.15 ms a
// byte, so three whole planes are ten frames of a screen not polling its joypad.
static void put_row(uint8_t x, uint8_t y, uint8_t w, const uint8_t *d)
{
    if (LCDC_REG & LCDCF_ON) set_bkg_tiles(x, y, w, 1, d);
    else memcpy((uint8_t *)((LCDC_REG & LCDCF_BG9C00) ? 0x9C00 : 0x9800) + y * 32u + x, d, w);
}

// No shadow and no diff: a caller paints from enter(), where a whole-screen clear is 18 spans.
void cv_attr(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t pal)
{
    uint8_t row[CV_COLS];

    if (!has_cgb || x >= CV_COLS) return;
    if (x + w > CV_COLS) w = (uint8_t)(CV_COLS - x);
    memset(row, pal, w);
    VBK_REG = 1;
    for (; h && y < CV_ROWS; h--, y++) put_row(x, y, w, row);
    VBK_REG = 0;
}

void cv_attr_map(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *a)
{
    if (!has_cgb || x >= CV_COLS) return;
    VBK_REG = 1;
    for (; h && y < CV_ROWS; h--, y++, a += w) put_row(x, y, w, a);
    VBK_REG = 0;
}

void cv_attr_row(uint8_t x, uint8_t y, uint8_t w, uint8_t pal)
{
    if (!has_cgb || y >= CV_ROWS || (span_x[y] == x && span_pal[y] == pal)) return;
    span_x[y] = x;
    span_pal[y] = pal;
    cv_attr(x, y, w, 1, pal);
}

void cv_reset(void)
{
    memset(shown, SPACE, sizeof shown);
    memset(span_pal, NO_SPAN, sizeof span_pal);
    cv_pal_load();
    cv_attr(0, 0, CV_COLS, CV_ROWS, CV_PAL_TEXT);
    cv_clear();
}

void cv_clear(void) { memset(want, SPACE, sizeof want); }

void cv_map(const uint8_t *map) { memcpy(want, map, sizeof want); }

void cv_put(uint8_t x, uint8_t y, uint8_t tile)
{
    if (x < CV_COLS && y < CV_ROWS) want[y * CV_COLS + x] = tile;
}

void cv_text(uint8_t x, uint8_t y, const char *s)
{
    uint8_t *w = want + y * CV_COLS + x;

    if (y >= CV_ROWS) return;
    for (; *s && x < CV_COLS; x++, s++)
        *w++ = (uint8_t)*s >= 0x80 ? (uint8_t)*s : (uint8_t)(*s - 0x20);
}

// Repeated subtraction, not `/ 10`: SDCC's 16-bit divide is ~4 ms a call. The last two digits run
// in 8 bits, where the codegen is four times tighter, and the units digit is the remainder.
void cv_num(uint8_t x, uint8_t y, uint16_t v, uint8_t width)
{
    uint8_t d[5], n = 0, c, r;

    for (c = 0; v >= 10000u; c++) v -= 10000u;
    if (c) d[n++] = (uint8_t)('0' - 0x20 + c);
    for (c = 0; v >= 1000u; c++) v -= 1000u;
    if (c || n) d[n++] = (uint8_t)('0' - 0x20 + c);
    for (c = 0; v >= 100u; c++) v -= 100u;
    if (c || n) d[n++] = (uint8_t)('0' - 0x20 + c);
    r = (uint8_t)v;
    for (c = 0; r >= 10u; c++) r -= 10u;
    if (c || n) d[n++] = (uint8_t)('0' - 0x20 + c);
    d[n++] = (uint8_t)('0' - 0x20 + r);
    while (width > n) { cv_put(x++, y, SPACE); width--; }
    for (c = 0; c < n; c++) cv_put(x++, y, d[c]);
}

void cv_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t x, y;

    for (x = (uint8_t)(x0 + 1); x < x1; x++) {
        cv_put(x, y0, CV_T_BOX_T);
        cv_put(x, y1, CV_T_BOX_B);
    }
    for (y = (uint8_t)(y0 + 1); y < y1; y++) {
        memset(want + y * CV_COLS + x0 + 1, SPACE, (uint8_t)(x1 - x0 - 1));
        cv_put(x0, y, CV_T_BOX_L);
        cv_put(x1, y, CV_T_BOX_R);
    }
    cv_put(x0, y0, CV_T_BOX_TL);
    cv_put(x1, y0, CV_T_BOX_TR);
    cv_put(x0, y1, CV_T_BOX_BL);
    cv_put(x1, y1, CV_T_BOX_BR);
}

void cv_flush(void) { cv_flush_rows(0, CV_ROWS); }

void cv_flush_rows(uint8_t y, uint8_t n)
{
    uint8_t *w = want + y * CV_COLS, *s = shown + y * CV_COLS;

    for (n += y; y < n; y++, w += CV_COLS, s += CV_COLS) {
        uint8_t x0 = 0, x1 = CV_COLS - 1;
        while (x0 < CV_COLS && w[x0] == s[x0]) x0++;
        if (x0 == CV_COLS) continue;
        while (w[x1] == s[x1]) x1--;
        x1 = (uint8_t)(x1 - x0 + 1);
        put_row(x0, y, x1, w + x0);
        memcpy(s + x0, w + x0, x1);
    }
}
