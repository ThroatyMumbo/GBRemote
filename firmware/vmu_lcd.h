// vmu_lcd.h — the VMU's 48x32 dots as Game Boy tiles. Pure: firmware/test/ decodes the result
// back to pixels, which is the only check that does not need a Dreamcast.
#ifndef VMU_LCD_FMT_H       // not VMU_LCD_H: that is the frame's height, below
#define VMU_LCD_FMT_H

#include <stdint.h>
#include "vmu_stage.h"

// 96x64 on screen: each dot is doubled both ways, so a frame is 12x8 tiles of 2bpp, 16 bytes each.
#define VMU_TILE_COLS  12u
#define VMU_TILE_ROWS  8u
#define VMU_TILE_BYTES (VMU_TILE_COLS * VMU_TILE_ROWS * 16u)
#define VMU_LCD_W      48u
#define VMU_LCD_H      32u
#define VMU_LCD_STRIDE 6u

// The LCD's origin is the corner opposite the Game Boy's top left, so the frame is turned end for
// end; a wrong setting puts the test pattern's solid corner diagonally opposite.
#ifndef VMU_ROTATE_180
#define VMU_ROTATE_180 1
#endif

// Struct order, not wire order: maple_unpack() has already un-reversed every 4-byte group by the
// time a frame reaches us.
#ifndef VMU_WORD_SWAP
#define VMU_WORD_SWAP 0
#endif

// Split so core0 never loops past ~12 us, where mailbox writes drop: prepare() does byte order and
// rotation once (~10 us), and each tile row is an eighth of the formatting.
#define VMU_TILE_ROW_BYTES (VMU_TILE_COLS * 16u)


// rotate is per frame, not per build: a frame the Dreamcast pushed is drawn for a card sitting in
// a controller, and one the emulated card drew itself is already upright (vmu_run.h).
void vmu_lcd_prepare_ex(const uint8_t frame[MAPLE_VMU_LCD_BYTES], uint8_t out[MAPLE_VMU_LCD_BYTES],
                        bool rotate);
void vmu_lcd_tile_row(const uint8_t prepared[MAPLE_VMU_LCD_BYTES],
                      uint8_t row[VMU_TILE_ROW_BYTES], unsigned r);

// The whole frame in one call, for the host tests and anything not sharing core0 with the bus.
void vmu_lcd_tiles(const uint8_t frame[MAPLE_VMU_LCD_BYTES], uint8_t tiles[VMU_TILE_BYTES]);

// The frame a bench key draws with no console attached: a border, a solid block in one corner and
// a sweeping column, so the whole publish path can be brought up on its own.
void vmu_lcd_test_frame(uint8_t frame[MAPLE_VMU_LCD_BYTES], uint32_t t);

#endif
