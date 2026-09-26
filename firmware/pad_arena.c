#include "pad_arena.h"
#include <stdint.h>

static uint8_t g_pool[PAD_ARENA_BYTES] __attribute__((aligned(32)));
static size_t  g_used;

void *pad_arena_alloc(size_t bytes, size_t align) {
    if (align == 0) { align = 1; }
    size_t at = (g_used + align - 1) & ~(align - 1);
    if (at > PAD_ARENA_BYTES || bytes > PAD_ARENA_BYTES - at) { return NULL; }
    g_used = at + bytes;
    return g_pool + at;
}

void   pad_arena_reset(void)    { g_used = 0; }
size_t pad_arena_capacity(void) { return PAD_ARENA_BYTES; }
size_t pad_arena_used(void)     { return g_used; }
