// tpak_proto.h — the N64 Transfer Pak as pure logic filling n64_pak_t; the cartridge is gbcart_emu.c.
// Sources: libdragon joypad_accessory.c and tpak.c, TransferBoy's TransferPakReference.md.
#ifndef TPAK_PROTO_H
#define TPAK_PROTO_H

#include "n64_proto.h"
#include "gbcart_emu.h"

// From a real pak (tools/tpaksurvey.py); libdragon's names for bits 2 and 3 are wrong.
#define TPAK_ST_ACCESS    0x01
#define TPAK_ST_WAS_RESET 0x04  // latched when access goes off; a $B000 read clears it
#define TPAK_ST_RUNNING   0x08  // level: the cart is out of /RST, i.e. access is on
#define TPAK_ST_NO_CART   0x40  // libultra's GBCART_PULL: only read once GBCART_ON is set
#define TPAK_ST_POWERED   0x80  // libultra's GBCART_ON — powered AND holding a cartridge

// A block written to GB $8000-$9FFF, `off` from $8000. Runs in the Joybus turnaround.
typedef void (*tpak_feed_fn)(void *ctx, uint16_t off, const uint8_t blk[N64_BLOCK]);

// The window's read half, one 32-byte SRAM copy; false falls through to the cartridge ($FF).
typedef bool (*tpak_win_fn)(void *ctx, uint16_t off, uint8_t out[N64_BLOCK]);

typedef struct {
    gbcart_t *cart;         // NULL: powered pak with an empty slot, which is a legal state
    tpak_feed_fn feed;      // NULL: $8000-$9FFF writes go nowhere, as on a real pak
    void     *feed_ctx;
    tpak_win_fn win;        // NULL: $8000-$9FFF reads answer $FF, as on a real pak
    void     *win_ctx;
    bool      powered;
    bool      access;       // survives a power cycle, as on hardware
    uint8_t   bank;         // 0-3, the 16 KB slice of GB space at $C000
    bool      was_reset;    // TPAK_ST_WAS_RESET
    uint32_t  refill_req;   // bumped when a write moves the ROM bank
    uint32_t  faults;       // reads that arrived before core0 staged the bank; want zero
} tpak_t;

void tpak_init(tpak_t *t, gbcart_t *cart);
void tpak_set_feed(tpak_t *t, tpak_feed_fn fn, void *ctx);

// Makes the feed window readable (demos/stadium/bk_proto.h); independent of the feed hook.
void tpak_set_window(tpak_t *t, tpak_win_fn fn, void *ctx);

// Fills an n64_pak_t whose ctx is `t`; the callbacks run inside the Joybus turnaround.
void tpak_bind(tpak_t *t, n64_pak_t *out);

#endif
