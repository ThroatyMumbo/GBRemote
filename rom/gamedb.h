// gamedb.h — what the shelf makes of the name a slot carries.
//
// The name itself is not ours: tools/mkslot.py puts a real one in the slot header ("Pokemon
// Crystal", not the $0134 build artifact "PM_CRYSTAL"), so nothing here tables names — that would
// be a second source of truth for them. What is ours is the colour.
#ifndef GAMEDB_H
#define GAMEDB_H

#include <stdint.h>

uint8_t     game_hue  (const char *title);   // CART_HUE_*, hashed from the title if it is unknown
const char *game_short(const char *s);       // less the series name, for a narrow column

#endif
