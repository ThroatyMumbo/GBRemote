// glk_proto.h — the Genesis controller-port link, shared by demos/fmsynth's cart and console.
// Symbols are in console layout: D0-D3 = bits 0-3, TL = 4, TR = 5.
#ifndef GLK_PROTO_H
#define GLK_PROTO_H

#define GLK_MAGIC      0x2A     // header symbol 0
#define GLK_RPT_MAGIC  0xA5     // report byte 0
#define GLK_N          8192u    // payload symbols per test burst
#define GLK_RPT_BYTES  20u
#define GLK_RPT_SYMS   (GLK_RPT_BYTES * 2u)

// Header is [MAGIC][cmd][len_hi][len_lo], len in 4-symbol units. REPORT's len carries the
// console's own error count for the read test it follows.
#define GLK_W8REG   0x01
#define GLK_WBUF    0x02
#define GLK_WBYTE   0x03
#define GLK_RRAW    0x10        // + nop count between TH write and read: 0x10 0x11 0x12 0x14
#define GLK_RBYTE   0x18
#define GLK_REPORT  0x3F
#define GLK_IS_READ(c)  (((c) & 0x10) != 0)

#define GLK_F_TIMEOUT   0x01    // an edge never came
#define GLK_F_OVERFLOW  0x02    // the cart's RX FIFO stalled mid-burst

// Report byte offsets, all fields big-endian.
#define GLK_R_MAGIC  0
#define GLK_R_CMD    1
#define GLK_R_N      2
#define GLK_R_GOT    4
#define GLK_R_ERR    6
#define GLK_R_FIRST  8          // 0xFFFF = no bad symbol
#define GLK_R_TUS    10         // first to last payload edge, cart timer
#define GLK_R_SPAN   14         // symbol periods inside GLK_R_TUS
#define GLK_R_FLAGS  16

static inline unsigned glk_sym(unsigned j)  { return (j * 37u + (j >> 6)) & 63u; }
static inline unsigned glk_byte(unsigned i) { return (i * 73u + (i >> 8) * 5u + 1u) & 255u; }

// Byte tests send the high nibble on the even (TH low) symbol, TL/TR held 0.
static inline unsigned glk_expect(unsigned cmd, unsigned j) {
    if (cmd == GLK_W8REG) return glk_sym(j & 3u);
    if (cmd == GLK_WBYTE || cmd == GLK_RBYTE)
        return (j & 1u) ? (glk_byte(j >> 1) & 15u) : (glk_byte(j >> 1) >> 4);
    return glk_sym(j);
}

#endif
