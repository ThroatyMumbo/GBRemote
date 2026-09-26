#include "lz4blk.h"
#include <string.h>

#define HASH_LOG  12
#define MINMATCH  4
#define MFLIMIT   12                    // the last match must start this far before the end
#define LASTLIT   5                     // and the block must end in this many literals
#define SKIP_TRIG 6                     // reference LZ4's acceleration 1: step grows every 64 misses

// Positions are absolute across calls, so an entry below the window's start is stale and the
// table never needs clearing. The base starts past one full window so zeroed entries are stale too.
#define BASE0 0x10000u
static uint32_t s_tab[1u << HASH_LOG];
static uint32_t s_base = BASE0;

static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint32_t hash4(uint32_t v) { return (v * 2654435761u) >> (32 - HASH_LOG); }

static uint8_t *put_len(uint8_t *op, size_t len) {
    while (len >= 255) { *op++ = 255; len -= 255; }
    *op++ = (uint8_t)len;
    return op;
}

static uint8_t *put_lits(uint8_t *op, uint8_t *tok, const uint8_t *lit, size_t n) {
    *tok = (uint8_t)((n >= 15 ? 15 : n) << 4);
    if (n >= 15) op = put_len(op, n - 15);
    memcpy(op, lit, n);
    return op + n;
}

void lz4blk_reset(void) {
    if (s_base > 0xF0000000u) { memset(s_tab, 0, sizeof s_tab); s_base = BASE0; }
    else s_base += BASE0;
}

size_t lz4blk_compress(const uint8_t *src, size_t n, uint8_t *dst) {
    return lz4blk_compress_dict(src, n, 0, dst);
}

size_t lz4blk_compress_dict(const uint8_t *src, size_t n, size_t hist, uint8_t *dst) {
    if (hist > 65535u) hist = 65535u;
    if (s_base > 0xF0000000u) { lz4blk_reset(); hist = 0; }
    const uint32_t base = s_base, lo = base - (uint32_t)hist;
    const uint8_t *const lowref = src - hist;
    s_base += (uint32_t)n;

    const uint8_t *ip = src, *anchor = src;
    const uint8_t *const end = src + n;
    uint8_t *op = dst;

    if (n > MFLIMIT) {
        const uint8_t *const mflimit = end - MFLIMIT;
        const uint8_t *const mlimit = end - LASTLIT;
        s_tab[hash4(rd32(ip))] = base;
        ip++;
        uint32_t miss = 1u << SKIP_TRIG;
        while (ip <= mflimit) {
            uint32_t seq = rd32(ip);
            uint32_t h = hash4(seq);
            uint32_t cand = s_tab[h];
            uint32_t cur = base + (uint32_t)(ip - src);
            s_tab[h] = cur;
            const uint8_t *ref = src + (int32_t)(cand - base);
            if (cand < lo || cur - cand > 65535u || rd32(ref) != seq) {
                ip += miss++ >> SKIP_TRIG;
                continue;
            }
            miss = 1u << SKIP_TRIG;
            while (ip > anchor && ref > lowref && ip[-1] == ref[-1]) { ip--; ref--; }

            const uint8_t *mp = ip + MINMATCH, *rp = ref + MINMATCH;
            for (;;) {
                if (mp + 4 > mlimit) {
                    while (mp < mlimit && *mp == *rp) { mp++; rp++; }
                    break;
                }
                uint32_t d = rd32(mp) ^ rd32(rp);
                if (d) { mp += (unsigned)__builtin_ctz(d) >> 3; break; }
                mp += 4; rp += 4;
            }

            size_t ml = (size_t)(mp - ip) - MINMATCH;
            uint8_t *tok = op++;
            op = put_lits(op, tok, anchor, (size_t)(ip - anchor));
            *tok |= (uint8_t)(ml >= 15 ? 15 : ml);
            uint32_t off = (uint32_t)(ip - ref);
            *op++ = (uint8_t)off;
            *op++ = (uint8_t)(off >> 8);
            if (ml >= 15) op = put_len(op, ml - 15);

            ip = anchor = mp;
            s_tab[hash4(rd32(ip - 2))] = base + (uint32_t)(ip - 2 - src);
        }
    }
    uint8_t *tok = op++;
    return (size_t)(put_lits(op, tok, anchor, (size_t)(end - anchor)) - dst);
}

static int get_len(const uint8_t **ip, const uint8_t *iend, size_t *len) {
    unsigned b;
    do {
        if (*ip >= iend) return -1;
        b = *(*ip)++;
        *len += b;
    } while (b == 255);
    return 0;
}

long lz4blk_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    const uint8_t *ip = src, *const iend = src + n;
    uint8_t *op = dst, *const oend = dst + cap;
    while (ip < iend) {
        unsigned tok = *ip++;
        size_t lit = tok >> 4;
        if (lit == 15 && get_len(&ip, iend, &lit)) return -1;
        if (lit > (size_t)(iend - ip) || lit > (size_t)(oend - op)) return -1;
        memcpy(op, ip, lit);
        op += lit;
        ip += lit;
        if (ip == iend) break;                  // the last sequence is literals only
        if (iend - ip < 2) return -1;
        size_t off = ip[0] | (size_t)ip[1] << 8;
        ip += 2;
        if (!off || off > (size_t)(op - dst)) return -1;
        size_t ml = tok & 15u;
        if (ml == 15 && get_len(&ip, iend, &ml)) return -1;
        ml += MINMATCH;
        if (ml > (size_t)(oend - op)) return -1;
        const uint8_t *ref = op - off;
        while (ml--) *op++ = *ref++;            // bytewise: an overlapping match is a run
    }
    return (long)(op - dst);
}
