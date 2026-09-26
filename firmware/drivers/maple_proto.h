// maple_proto.h — the Dreamcast Maple command set, device side; pure logic, the wire is mapledev.c.
// Frames are in struct order, 32-bit payload fields big-endian; maple_pack_words() makes wire order.
#ifndef MAPLE_PROTO_H
#define MAPLE_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "pad.h"

// A memory card's block-read reply is the longest frame: 4 + 4 func + 4 block + 512 + 1 = 525.
#define MAPLE_MAX_FRAME 528
#define MAPLE_MAX_WORDS (MAPLE_MAX_FRAME / 4 + 1)

enum {
    MAPLE_CMD_DEVICE_REQUEST     = 1,
    MAPLE_CMD_ALL_STATUS_REQUEST = 2,
    MAPLE_CMD_RESET_DEVICE       = 3,
    MAPLE_CMD_SHUTDOWN_DEVICE    = 4,
    MAPLE_CMD_RESP_DEVICE_STATUS = 5,
    MAPLE_CMD_RESP_ALL_STATUS    = 6,
    MAPLE_CMD_RESP_ACK           = 7,
    MAPLE_CMD_RESP_DATA_XFER     = 8,
    MAPLE_CMD_GET_CONDITION      = 9,
    // KOS's numbering (dc/maple.h); the VMU unit answers these.
    MAPLE_CMD_GET_MEDIA_INFO     = 10,
    MAPLE_CMD_BLOCK_READ         = 11,
    MAPLE_CMD_BLOCK_WRITE        = 12,
    MAPLE_CMD_BLOCK_SYNC         = 13,
    MAPLE_CMD_SET_CONDITION      = 14,

    // Refusals, KOS's signed int8 as the wire byte. RESEND is never sent (KOS requeues it forever);
    // KOS's NONE (0xff) is the console's own no-reply marker.
    MAPLE_RESP_FILE_ERR          = 0xfb,
    MAPLE_RESP_RESEND            = 0xfc,
    MAPLE_RESP_UNKNOWN_CMD       = 0xfd,
    MAPLE_RESP_UNKNOWN_FUNC      = 0xfe,
};

#define MAPLE_ADDR_DC         0x00
#define MAPLE_ADDR_CONTROLLER 0x20  // the main-peripheral flag; sub-device bits ride beside it
#define MAPLE_PORT_MASK       0xc0
#define MAPLE_PERIPH_MASK     0x3f
// A main peripheral's src carries a bitmap of its sub-devices: a real controller with a VMU in slot
// 1 sends port | 0x20 | 0x01 on every reply.
#define MAPLE_SUB_MASK        0x1f
#define MAPLE_SUB_UNIT(n)     (1u << ((n) - 1))     // n is 1..5

// Logical function codes: bswap32 of the values a Dreamcast reads back.
#define MAPLE_FUNC_CONTROLLER 1u
#define MAPLE_FUNC_MEMCARD    2u
#define MAPLE_FUNC_LCD        4u
#define MAPLE_FUNC_CLOCK      8u
#define MAPLE_FUNC_VMU        (MAPLE_FUNC_MEMCARD | MAPLE_FUNC_LCD | MAPLE_FUNC_CLOCK)

#define MAPLE_VMU_BLOCK   512u
#define MAPLE_VMU_BLOCKS  256u

// Active LOW on the wire: 1 is released. C/Z/D do not exist on an HKT-7700 and stay set.
#define MAPLE_BTN_C     0x0001
#define MAPLE_BTN_B     0x0002
#define MAPLE_BTN_A     0x0004
#define MAPLE_BTN_START 0x0008
#define MAPLE_BTN_UP    0x0010
#define MAPLE_BTN_DOWN  0x0020
#define MAPLE_BTN_LEFT  0x0040
#define MAPLE_BTN_RIGHT 0x0080
#define MAPLE_BTN_Z     0x0100
#define MAPLE_BTN_Y     0x0200
#define MAPLE_BTN_X     0x0400
#define MAPLE_BTN_D     0x0800

// 0x80 center, so full deflection lands on 0x01/0xff — the same range MaplePad's ADC map produces.
#define MAPLE_AXIS_CENTER 0x80
#define MAPLE_AXIS_FULL   127

typedef struct {
    uint16_t buttons;
    uint8_t  trig_r, trig_l;
    uint8_t  joy_x, joy_y, joy_x2, joy_y2;
} maple_cond_t;

static inline uint8_t maple_cmd   (const uint8_t *f) { return f[0]; }
static inline uint8_t maple_dst   (const uint8_t *f) { return f[1]; }
static inline uint8_t maple_src   (const uint8_t *f) { return f[2]; }
static inline uint8_t maple_nwords(const uint8_t *f) { return f[3]; }

// Logical value of payload word i, undoing the byte-swapped storage.
static inline uint32_t maple_data_word(const uint8_t *f, unsigned i) {
    const uint8_t *p = f + 4U + 4U * i;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// XOR of every byte. Order-independent, so the per-word reversal cannot change it.
uint8_t maple_checksum(const uint8_t *frame, unsigned n);

// Struct-order bytes -> PIO FIFO words. n is a whole frame including its checksum byte, so 4k+1.
// Returns the word count.
unsigned maple_pack_words(const uint8_t *frame, unsigned n, uint32_t *words);

// Wire order -> struct order, the inverse a received frame needs. n includes the checksum byte,
// which is dropped; returns the struct-order length (4k), or 0 if n is not a valid frame length.
unsigned maple_unpack(const uint8_t *wire, unsigned n, uint8_t *out);

// Bit pairs minus 1, the loop count maple_tx consumes ahead of the payload.
static inline uint32_t maple_bit_pairs_minus1(unsigned nbytes) {
    return (uint32_t)(nbytes * 4U) - 1U;
}

void maple_cond_default(maple_cond_t *c);

// Canonical pad -> Maple controller condition. dpad_analog routes the d-pad to the stick instead
// of the d-pad, which is the inverse of the N64 default: most DC games read the d-pad natively.
void maple_from_pad(const pad_state_t *p, bool dpad_analog, maple_cond_t *out);

// The bus: which addresses we answer for. Caller-owned, so this file stays pure and a test can
// build one on its stack.
#define MAPLE_MAX_UNITS  4
#define MAPLE_REPLY_NONE (-1)

// The 112/192-byte identity payload: one layout, per-unit values. Strings are space padded to
// their field width and never NUL terminated — the padding is part of the wire format.
typedef struct {
    uint32_t    func;
    uint32_t    func_data[3];
    uint8_t     area_code, connector_dir;
    const char *name, *license, *free_status;
    uint16_t    standby_power, max_power;       // 0.1 mA units
} maple_devid_t;

typedef struct maple_unit {
    uint8_t              addr;                  // MAPLE_ADDR_CONTROLLER, or MAPLE_SUB_UNIT(n)
    const maple_devid_t *id;
    // Returns the payload length in bytes, or MAPLE_REPLY_NONE to stay off the bus. Signed because
    // a refusal is a reply with a zero-length payload.
    int (*command)(const struct maple_unit *u, const uint8_t *req, unsigned req_len,
                   uint8_t *payload, uint8_t *resp);
    void *state;
} maple_unit_t;

typedef struct {
    const maple_unit_t *unit[MAPLE_MAX_UNITS];
    uint8_t n;
    uint8_t sub_mask;                           // what is plugged into us; rides the main src
} maple_bus_t;

void maple_bus_init(maple_bus_t *b);
bool maple_bus_add(maple_bus_t *b, const maple_unit_t *u);  // false: full, or the address is taken
int  maple_bus_find(const maple_bus_t *b, uint8_t dst);     // index, or -1

// The HKT-7700. cond must outlive the unit; core1 rewrites it between frames.
void maple_controller_unit(maple_unit_t *u, maple_cond_t *cond);

// A Visual Memory. card is MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK bytes and must outlive the unit; the
// callbacks keep cores and flash out of this file.
typedef struct {
    uint8_t *card;
    uint32_t now_ms;                    // stored in the idle gap, never inside a frame
    uint32_t clock_secs, clock_base_ms; // the clock as of clock_base_ms; it ticks from there
    void (*on_write)(void *ctx, unsigned block, uint32_t now_ms);
    void (*on_lcd)  (void *ctx, const uint8_t *frame, unsigned n);
    void *ctx;
} maple_vmu_t;

#define MAPLE_VMU_LCD_BYTES 192u        // 48x32 dots, 1bpp, 6 bytes a row
#define MAPLE_VMU_EPOCH     925257600u  // 1999-04-28 00:00:00 UTC, the free-status BIOS date

// The clock's seconds as a civil date. Public because vmu_run.c hands the same date to the
// LC8670 core, and two copies of Hinnant's algorithm is one too many.
void maple_date_from_secs(uint32_t secs, uint16_t *y, uint8_t *mo, uint8_t *d,
                          uint8_t *h, uint8_t *mi, uint8_t *s);

// Zeroes st, so the clock starts at MAPLE_VMU_EPOCH and the callbacks are unset until assigned.
void maple_vmu_unit(maple_unit_t *u, maple_vmu_t *st, uint8_t *card, uint8_t addr);

// Lay down an empty but valid filesystem: root, FAT and directory as a real card carries them.
// A zeroed card enumerates fine and then reads as unformatted, which is not what a VMU looks like.
void maple_vmu_format(uint8_t *card);

// Where the root block keeps the geometry, so a test can read it back without a second copy of
// these constants (Marcus Comstedt's layout; the values match build_minfo_card()).
#define VMU_ROOT_GEOM    0x40u  // the 24 bytes GET_MEDIA_INFO returns, inside the root block
#define VMU_ROOT_BLOCK   255u
#define VMU_FAT_BLOCK    254u
#define VMU_DIR_BLOCK    253u
#define VMU_DIR_BLOCKS   13u
#define VMU_USER_BLOCKS  200u
#define VMU_FAT_UNUSED   0xfffcu
#define VMU_FAT_LAST     0xfffau

#ifdef BENCH_PROTO
// Truncate a block read's payload, to find what reply length a console stops taking. 0 = 512.
void maple_vmu_read_cap(unsigned bytes);
#endif

// Returns the reply length in bytes including the checksum, 0 if the request needs no reply.
// req is a decoded frame in struct order, without its checksum byte.
unsigned maple_build_reply(const maple_bus_t *b, const uint8_t *req, unsigned req_len,
                           uint8_t out[MAPLE_MAX_FRAME]);

// Receive: Charlie Cole's transition graph (MaplePad's state_machine.c), stepped one 2-bit sample at
// a time from a 200-byte table rather than his 20 KB Machine[40][256].
#define MAPLE_RX_STATES 40

typedef enum { MAPLE_RX_MORE = 0, MAPLE_RX_FRAME, MAPLE_RX_ERROR } maple_rx_ev_t;

typedef struct {
    uint8_t  next[MAPLE_RX_STATES][4];      // 0xff = not a legal transition from here
    uint8_t  status[MAPLE_RX_STATES];
    uint8_t  nstates;
    uint8_t  state, byte, xorsum;
    bool     framing;
    unsigned len;
    uint8_t  buf[MAPLE_MAX_FRAME];          // complete frame including its checksum byte
} maple_rx_t;

void maple_rx_init(maple_rx_t *r);          // builds the table, then resets
void maple_rx_reset(maple_rx_t *r);

// One 2-bit line sample: bit0 = SDCKA, bit1 = SDCKB. On MAPLE_RX_FRAME the frame is in r->buf,
// r->len bytes, checksum already verified.
maple_rx_ev_t maple_rx_feed(maple_rx_t *r, uint8_t sample);

// True between a start pattern and its end pattern, so a caller can tell an idle bus from a frame
// still arriving. A 525-byte frame is 2.7 ms of wire time against driver_core's 1 ms idle budget.
static inline bool maple_rx_in_frame(const maple_rx_t *r) { return r->framing; }

#endif
