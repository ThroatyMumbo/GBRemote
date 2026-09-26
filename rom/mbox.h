// mbox.h — the entire cart-bus contract, in one place.
//
// The RP2350 serves this ROM out of SRAM and rewrites bytes of it under us. Every address below
// $8000 named here is either something we write (the $6000 block, decoded out of write_capture)
// or something the firmware rewrites while we run. The latter MUST be re-read every pass and
// never cached across a loop iteration — see CLAUDE.md's load-bearing details.
//
// Mirrors firmware/main.c's mailbox block and firmware/config_map.h. Change both together.
#ifndef MBOX_H
#define MBOX_H

#include <stdint.h>

// ---- GB -> RP2350. Plain stores; no MBC, so A15 low is all write_capture needs. ----
#define MBOX_BTN     (*(volatile uint8_t *)0x6000)   // packed pad byte, GBDK J_* order
#define MBOX_MODE    (*(volatile uint8_t *)0x6001)
#define MBOX_CFG_IDX (*(volatile uint8_t *)0x6002)
#define MBOX_CFG_VAL (*(volatile uint8_t *)0x6003)
#define MBOX_COMMIT  (*(volatile uint8_t *)0x6004)   // any value; persists config to flash

#define MODE_CONFIG  0
#define MODE_CAPTURE 1

// ---- RP2350 -> GB. Rewritten under us; volatile is load-bearing, not decoration. ----
#define ST_STATUS    (*(const volatile uint8_t *)0x0010)
#define ST_VERSION   (*(const volatile uint8_t *)0x0011)
#define ST_ADAPTER   (*(const volatile uint8_t *)0x0012)   // ADAPT_*: 0 none, 1-9 slot, 0xff unknown
#define ST_DIAG      (*(const volatile uint8_t *)0x0013)   // pad_err_t as u8
#define ST_ACTIVE    (*(const volatile uint8_t *)0x0014)   // PROTO_* core1 actually owns
#define ST_APPLYSEQ  (*(const volatile uint8_t *)0x0015)   // ticks once per drv_apply()
// The Visual Memory in the Dreamcast driver's slot 1. SEQ ticks once per frame, after the whole
// frame is in place, so a screen blits on a change and never mid-write.
#define ST_VMU_SEQ   (*(const volatile uint8_t *)0x0016)
#define ST_VMU_STAT  (*(const volatile uint8_t *)0x0017)
#define ST_VMU_SAVES (*(const volatile uint8_t *)0x0018)   // low byte of completed writebacks

// The controller picture the cart stages at $4600 (firmware/padart_fmt.h). PROTO says whose it is,
// SEQ that it is whole — the firmware writes PROTO first and SEQ last, so a screen that waits for
// SEQ to move can never read a half-staged blob. SEQ 0 means nothing has been staged at all.
#define ART_SEQ      (*(const volatile uint8_t *)0x0019)
#define ART_PROTO    (*(const volatile uint8_t *)0x001a)

// What the cart is really driving, not what the Game Boy pressed: map_apply() has already run, so
// a CFG_MAP remap shows on the picture. pad.h's PAD_* bits, 0-23, low byte first. SEQ is bumped
// after the three, and art_take() re-reads until it is stable — the N64's C buttons straddle
// $001C and $001D, so a two-byte press really can be caught half applied.
#define PAD_LIVE0    (*(const volatile uint8_t *)0x001b)
#define PAD_LIVE1    (*(const volatile uint8_t *)0x001c)
#define PAD_LIVE2    (*(const volatile uint8_t *)0x001d)
#define PAD_LIVE_SEQ (*(const volatile uint8_t *)0x001e)

#define ST_ALIVE     0x01
#define ST_LINK_UP   0x02
#define ST_FORCE_CFG 0x80

#define VMU_ST_CARD   0x01
#define VMU_ST_DIRTY  0x02      // writes the flash copy does not have yet
#define VMU_ST_SAVING 0x04
#define VMU_ST_FRAME  0x08      // the console has drawn to the VMU screen this session

// The firmware formats the VMU's 48x32 LCD into GB 2bpp tiles here, in the served high bank, so
// the console never touches a pixel: one CGB general-purpose DMA per frame, ROM -> VRAM.
// 16-byte aligned because HDMA2/HDMA4 ignore the low four bits.
#define VMU_FB_ADDR  0x4000
#define VMU_COLS     12
#define VMU_ROWS     8
#define VMU_TILES    (VMU_COLS * VMU_ROWS)

#define ADAPT_NONE    0x00
#define ADAPT_UNKNOWN 0xff

// pad_err_t reaches us as a two's-complement byte.
#define DIAG_OK          0x00
#define DIAG_NO_RESOURCE 0xff
#define DIAG_UNSAFE      0xfe
#define DIAG_CONFIG      0xfd
#define DIAG_HW          0xfc

// ---- The config mirror, firmware/config_map.h ----
#define CFG_MIRROR_BASE 0x0200
#define CFG_VERSION     0x00
#define CFG_PROTO_SEL   0x01
#define CFG_FLAGS       0x02
#define CFG_MAP         0x10    // per-proto 8-byte maps to $77; the $0500 page names each one's offset
#define ACT_NONE        0xfe    // a map byte: this GB button does nothing
#define ACT_UNSET       0xff    // a map byte: the default, GB bit i -> PAD_* bit i
#define CFG_DRV_OWNER   0x80
#define CFG_DRV         0x81

// Inside drv_n64's CFG_DRV block. 0 = no pak, 1-8 = a Game Boy ROM slot. The firmware stamps
// CFG_DRV_OWNER itself on any CFG_DRV write, so a screen writes this byte and nothing else.
#define CFG_TPAK_SLOT   3

#define PROTO_NONE 0            // pinned at 0 forever; the "Off" row is synthesised from it
#define PROTO_N64  1            // stable on-flash id, firmware/pad_driver.h — never renumbered
#define PROTO_DREAMCAST 5       // the only driver with a Visual Memory in a slot

uint8_t mbox_cfg(uint8_t idx);          // one byte of the mirror, freshly read

// Index then value, in that order — the firmware latches the index and applies on the value.
// Writes take effect live; the firmware coalesces a burst for DRV_SETTLE_MS (50 ms) before it
// tears a driver down, so a screen may write several bytes back to back.
void mbox_set_cfg(uint8_t idx, uint8_t val);
void mbox_commit(void);                 // persist to flash

#endif
