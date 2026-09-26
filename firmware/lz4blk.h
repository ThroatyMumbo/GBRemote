// lz4blk.h — LZ4 blocks decodable by LZ4_decompress_safe(_usingDict). No SDK: the host tools
// build it as a shared library for their round-trip checks.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define LZ4BLK_BOUND(n) ((n) + (n) / 255u + 16u)

// A self-contained block.
size_t lz4blk_compress(const uint8_t *src, size_t n, uint8_t *dst);

// The next block of a stream: src[-hist, 0) must hold the stream's previous hist bytes, which the
// decoder passes as its dict. Calls must be consecutive in the stream; lz4blk_reset() starts a new one.
size_t lz4blk_compress_dict(const uint8_t *src, size_t n, size_t hist, uint8_t *dst);
void lz4blk_reset(void);

// A self-contained block into dst[0, cap): the decoded length, or -1 if it is malformed or overruns.
long lz4blk_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);
