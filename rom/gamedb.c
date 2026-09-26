#include <stdint.h>
#include "gamedb.h"
#include "cart_gfx.h"

// Keys are folded: upper case, and '_' read as a space. That is what lets one table serve both the
// name mkslot.py writes ("Pokemon Crystal") and the raw $0134 header a store written before it
// carries ("PM_CRYSTAL"), which is also what the sim seeds. Colours are the shells the carts had.
static const struct { const char *prefix; uint8_t hue; } k_hues[] = {
    { "PM CRYSTAL",      CART_HUE_CRYSTAL },
    { "POKEMON CRYSTAL", CART_HUE_CRYSTAL },
    { "POKEMON GLD",     CART_HUE_GOLD    },
    { "POKEMON GOLD",    CART_HUE_GOLD    },
    { "POKEMON SLV",     CART_HUE_SILVER  },
    { "POKEMON SILVER",  CART_HUE_SILVER  },
    { "POKEMON RED",     CART_HUE_RED     },
    { "POKEMON BLUE",    CART_HUE_BLUE    },
    { "POKEMON GREEN",   CART_HUE_GREEN   },
    { "POKEMON YELLOW",  CART_HUE_YELLOW  },
    { "TETRIS",          CART_HUE_TEAL    },
    { "YUGIOUDS",        CART_HUE_PURPLE  },
    { "YU-GI-OH",        CART_HUE_PURPLE  },
};
#define N_HUES (sizeof k_hues / sizeof k_hues[0])

static char fold(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c == '_' ? ' ' : c;
}

static uint8_t pfx(const char *s, const char *p)
{
    while (*p)
        if (fold(*s++) != *p++) return 0;
    return 1;
}

uint8_t game_hue(const char *title)
{
    uint8_t i, h = 0;

    for (i = 0; i < N_HUES; i++)
        if (pfx(title, k_hues[i].prefix)) return k_hues[i].hue;
    while (*title) h = (uint8_t)(h * 31u + (uint8_t)*title++);
    return (uint8_t)(h % CART_NHUE);
}

// The root panel's value column is ten wide, which "Pokemon Crystal" does not fit and "Crystal" does.
const char *game_short(const char *s)
{
    return pfx(s, "POKEMON ") ? s + 8 : s;
}
