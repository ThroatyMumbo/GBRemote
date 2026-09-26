// sneslink_proto.h — the SNES link's symbols and frames, pure logic that firmware/test/ runs
// against the assembled program; the wire is sneslink.c.
#ifndef SNESLINK_PROTO_H
#define SNESLINK_PROTO_H

#include <stdint.h>
#include <stdbool.h>

#define SLK_RELEASED 0xffffffffu        // TX word: 16 symbols of both lines high, reads as 0 0

// RX word: 8 samples of LATCH, CLK, IOBIT, first sample highest. Taken at the CLK rise, so every
// CLK bit is 1 and every LATCH bit 0 in a clean word.
#define SLK_RX_LATCH 0x249249u
#define SLK_RX_CLK   0x492492u

uint32_t slk_tx_word(const uint8_t b[4]);   // 4 bytes, 4 symbols each, as pin levels
uint8_t  slk_rx_byte(uint32_t w);           // IOBIT samples back in byte order
static inline bool slk_rx_clean(uint32_t w) {
    return (w & (SLK_RX_LATCH | SLK_RX_CLK | 0xff000000u)) == SLK_RX_CLK;
}

typedef struct {
    bool     to_snes;
    const uint8_t *tx;                  // to_snes: the payload
    uint8_t *rx;                        // !to_snes: where the payload lands
    uint32_t len;                       // payload bytes: a multiple of 4 to the SNES, 2 from it
    uint32_t tx_words, tx_total;        // pushed so far, of total (the released tail included)
    uint32_t rx_words, rx_total;        // one per 8 clocks, either direction
    uint32_t dirty;                     // RX words whose LATCH/CLK bits were not clean
} slk_frame_t;

bool slk_frame_init(slk_frame_t *f, bool to_snes, const uint8_t *tx, uint8_t *rx, uint32_t len);
bool slk_frame_tx_peek(const slk_frame_t *f, uint32_t *w);  // next TX word; false once the tail is out
static inline void slk_frame_tx_next(slk_frame_t *f) { f->tx_words++; }
bool slk_frame_rx(slk_frame_t *f, uint32_t w);      // true when this word completed the frame
static inline bool slk_frame_done(const slk_frame_t *f) { return f->rx_words >= f->rx_total; }
static inline uint32_t slk_frame_clocks(const slk_frame_t *f) { return f->rx_total * 8u; }

#endif
