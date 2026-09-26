#include "vmu_store_fmt.h"

// The ordinary CRC-32, bitwise: capenc_crc32() is the same polynomial with a 1 KB table, but
// capenc.c is the capture encoder and is not in this image.
uint32_t vmu_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) { crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U))); }
    }
    return ~crc;
}

bool vmu_hdr_ok(const vmu_hdr_t *h) {
    return h->magic == VMU_HDR_MAGIC && h->len == VMU_CARD_BYTES;
}

int vmu_hdr_pick(const vmu_hdr_t *a, const vmu_hdr_t *b) {
    bool oa = vmu_hdr_ok(a), ob = vmu_hdr_ok(b);
    if (oa && ob) { return (int32_t)(a->seq - b->seq) >= 0 ? 0 : 1; }
    if (oa) { return 0; }
    if (ob) { return 1; }
    return -1;
}
