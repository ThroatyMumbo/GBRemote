#include "sneslink_proto.h"

// Symbol k of a byte is b(7-2k) on D0 and b(6-2k) on D1; a 1 drives the line low.
uint32_t slk_tx_word(const uint8_t b[4]) {
    uint32_t w = 0;
    for (unsigned j = 0; j < 4; j++)
        for (unsigned k = 0; k < 4; k++) {
            unsigned sym = ((b[j] >> (7 - 2 * k)) & 1u) | ((b[j] >> (6 - 2 * k)) & 1u) << 1;
            w |= (uint32_t)(~sym & 3u) << (2 * (4 * j + k));
        }
    return w;
}

// The SNES sends b6 b5 b4 b3 b2 b1 b0 b7: `cmp #$80` parks b7 in carry and `rol a` brings it last.
uint8_t slk_rx_byte(uint32_t w) {
    unsigned r = 0;
    for (unsigned k = 0; k < 8; k++) r = r << 1 | ((w >> (3 * (7 - k) + 2)) & 1u);
    return (uint8_t)(r >> 1 | r << 7);
}

bool slk_frame_init(slk_frame_t *f, bool to_snes, const uint8_t *tx, uint8_t *rx, uint32_t len) {
    *f = (slk_frame_t){ .to_snes = to_snes, .tx = tx, .rx = rx, .len = len };
    if (!len || len % (to_snes ? 4u : 2u)) return false;
    uint32_t clocks = to_snes ? len * 4u : len * 8u;
    f->tx_total = clocks / 16u + 1u;
    f->rx_total = clocks / 8u;
    return true;
}

bool slk_frame_tx_peek(const slk_frame_t *f, uint32_t *w) {
    if (f->tx_words >= f->tx_total) return false;
    uint32_t i = f->tx_words;
    *w = f->to_snes && i + 1u < f->tx_total ? slk_tx_word(f->tx + 4u * i) : SLK_RELEASED;
    return true;
}

bool slk_frame_rx(slk_frame_t *f, uint32_t w) {
    if (slk_frame_done(f)) return false;
    if (!slk_rx_clean(w)) f->dirty++;
    if (!f->to_snes) f->rx[f->rx_words] = slk_rx_byte(w);
    return ++f->rx_words == f->rx_total;
}
