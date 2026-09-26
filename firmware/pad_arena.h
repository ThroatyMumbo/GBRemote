// pad_arena.h — a bump allocator for the one resident pad driver, reset by drv_teardown().
// Core1 only, from init()/reconfig(); not the VMU card, whose writeback outlives a detach.
#ifndef PAD_ARENA_H
#define PAD_ARENA_H

#include <stddef.h>

// The largest consumer: a Transfer Pak's two 16 KB ROM windows plus 32 KB of cart RAM.
#define PAD_ARENA_BYTES (64u * 1024u)

void  *pad_arena_alloc(size_t bytes, size_t align);   // NULL if it does not fit; uninitialized
void   pad_arena_reset(void);
size_t pad_arena_capacity(void);
size_t pad_arena_used(void);

#endif
