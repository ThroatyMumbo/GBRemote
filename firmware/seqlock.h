// seqlock.h — single-writer, cross-core seqlock over a byte copy. seq is odd while the writer is inside.
#ifndef SEQLOCK_H
#define SEQLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef PAD_HOST                 // host unit tests: no pico-sdk, single-threaded
#define __dmb() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#else
#include "hardware/sync.h"
#endif

// Always inlined: core1 publishes from RAM-resident code inside a reply turnaround.
static inline __attribute__((always_inline))
void seq_write(volatile uint32_t *seq, void *dst, const void *src, size_t n) {
    uint32_t q = *seq + 1U;
    *seq = q;
    __dmb();
    memcpy(dst, src, n);
    __dmb();
    *seq = q + 1U;
}

// Bounded, never spins: false means keep the last good copy. *at (may be NULL) gets the seq read.
static inline __attribute__((always_inline))
bool seq_read(const volatile uint32_t *seq, void *dst, const void *src, size_t n, uint32_t *at) {
    for (int i = 0; i < 4; i++) {
        uint32_t a = *seq;
        __dmb();
        if (a & 1U) { continue; }
        memcpy(dst, src, n);
        __dmb();
        if (*seq == a) {
            if (at) { *at = a; }
            return true;
        }
    }
    return false;
}

#endif
