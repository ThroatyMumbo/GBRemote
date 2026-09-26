// devcommon.h — what the wire layers share: holding a set of PIO programs and state machines.
#pragma once
#include <stdbool.h>
#include "hardware/pio.h"
#include "pad_driver.h"

// A pioasm program's length as a constant expression; `_wrap + 1` is not one when code follows the wrap.
#define PIO_WORDS(name) (sizeof name##_program_instructions / sizeof name##_program_instructions[0])
#define PIO2_SHARED_WORDS (32u - PIO2_RESERVED_WORDS)   // what a driver without excl_ble may use

#define PIO_RES_MAX 4

typedef struct {
    PIO  pio;                                   // NULL unless every program and SM below is held
    uint nprog, nsm;
    const struct pio_program *prog[PIO_RES_MAX];
    uint off[PIO_RES_MAX];
    uint sm[PIO_RES_MAX];
} pio_res_t;

// The caller stops its SMs first.
static inline void pio_res_release(pio_res_t *r) {
    if (!r->pio) { return; }
    while (r->nsm) { pio_sm_unclaim(r->pio, r->sm[--r->nsm]); }
    while (r->nprog) {
        r->nprog--;
        pio_remove_program(r->pio, r->prog[r->nprog], r->off[r->nprog]);
    }
    r->pio = NULL;
}

// All or nothing: a half-claimed PIO2 would surface as the next driver's init failing.
static inline bool pio_res_claim(pio_res_t *r, PIO pio, const struct pio_program *const *progs,
                                 uint nprog, uint nsm) {
    *r = (pio_res_t){ 0 };
    if (nprog > PIO_RES_MAX || nsm > PIO_RES_MAX) { return false; }
    r->pio = pio;
    for (; r->nprog < nprog; r->nprog++) {
        if (!pio_can_add_program(pio, progs[r->nprog])) { goto fail; }
        r->prog[r->nprog] = progs[r->nprog];
        r->off[r->nprog]  = pio_add_program(pio, progs[r->nprog]);
    }
    for (; r->nsm < nsm; r->nsm++) {
        int sm = pio_claim_unused_sm(pio, false);
        if (sm < 0) { goto fail; }
        r->sm[r->nsm] = (uint)sm;
    }
    return true;
fail:
    pio_res_release(r);
    return false;
}
