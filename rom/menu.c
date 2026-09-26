#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "menu.h"
#include "ui.h"

void menu_clamp(menu_t *m)
{
    if (m->n == 0) { m->sel = 0; m->top = 0; return; }
    if (m->sel >= m->n) m->sel = (uint8_t)(m->n - 1);
    if (m->sel < m->top) m->top = m->sel;
    if (m->sel >= (uint8_t)(m->top + m->rows)) m->top = (uint8_t)(m->sel - m->rows + 1);
}

uint8_t menu_nav(menu_t *m, uint8_t hit)
{
    uint8_t was = m->sel;

    if (m->n == 0) return 0;
    // Wrap: a five-item list is faster to reach the bottom of by pressing up once.
    if (hit & J_UP)   m->sel = m->sel ? (uint8_t)(m->sel - 1) : (uint8_t)(m->n - 1);
    if (hit & J_DOWN) m->sel = (uint8_t)((m->sel + 1) % m->n);
    menu_clamp(m);
    return m->sel != was;
}

// One row, already scrolled. `dim` brackets a disabled item — there is one BG palette here, so
// the marker is the only thing that can carry "you cannot pick this".
void menu_row(const menu_t *m, uint8_t i, const char *label, uint8_t dim, char mark)
{
    uint8_t y = (uint8_t)(m->y0 + (i - m->top));

    ui_clear_row(y);
    gotoxy(0, y);
    setchar(i == m->sel ? '>' : ' ');
    gotoxy(1, y);
    if (dim) printf("(%s)", label);
    else     printf("%s", label);
    if (mark) { gotoxy(UI_COLS - 2, y); setchar(mark); }
}

void menu_draw(const menu_t *m)
{
    uint8_t i, end = (uint8_t)(m->top + m->rows);

    if (end > m->n) end = m->n;
    for (i = m->top; i < end; i++)
        menu_row(m, i, m->items[i].label, (uint8_t)!m->items[i].enabled, 0);
    // Blank the tail so a shorter list cannot leave rows of a longer one behind.
    for (i = (uint8_t)(end - m->top); i < m->rows; i++)
        ui_clear_row((uint8_t)(m->y0 + i));
}
