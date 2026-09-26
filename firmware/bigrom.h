// bigrom.h — serving a ROM larger than SRAM by stalling the console while a bank is fetched.
// Pure logic, shared by the target and emulator/bigrom.
#ifndef BIGROM_H
#define BIGROM_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/gbcart_emu.h"

#define BR_BANK      0x4000u
#define BR_MAX_SLOTS 24

// Every stage shares the low byte $7F, so a release flips only the page of a `JP $xx7F`.
#define BR_LOOP      0x007Fu     // the stall loop, and the whole park
#define BR_PAGE_LOW  0x00u
#define BR_TRAMP     0x617Fu     // the resume: the interrupt repair, if one is owed, then `JP X`
#define BR_PAGE_TR   0x61u
#define BR_LOOP_LEN  3u

// The park must be in pinned bank 0: an interrupt's `reti` lands on it.
enum { BR_RUN, BR_CATCH, BR_FILL, BR_HANDOFF, BR_LEAVING };

#define BR_TRAMP_LEN 59u         // the guarded repair, then the three of `JP X`
// Branch targets in the repair, from BR_TRAMP: a fetch at one shows on the bus which way it went.
#define BR_TR_OWED   30u
#define BR_TR_SKIP   44u
// Where the skip path posts the stack word it rejected; inert on MBC5 and no-MBC only.
#define BR_SKIP_REPORT 0x6100u
#define BR_PEND      4u

// One missed vectoring while the stall bank is mounted ends interrupts for the run;
// the paranoia about that interval is load-bearing.
typedef struct {
    uint16_t x;                      // where the trap caught the console
    uint8_t *lo;                     // bank 0's slot, carrying the park
    uint8_t *hi;                     // the banked slot, carrying the trampoline
    uint8_t  lo_save[BR_LOOP_LEN];
    uint8_t  tr_save[BR_TRAMP_LEN];
    uint8_t  tr_len;                 // how much of it the trampoline actually used
} br_pend_t;

typedef struct {
    const uint8_t *img;              // whole ROM image: XIP flash on the target
    uint16_t       banks;
    uint8_t        mbc;
    bool           has_rtc;
    uint8_t *const *slot;            // nslots caller-owned SRAM banks; not one array, cartserve's
                                     // first two are its own low_bank/high_bank
    uint8_t        nslots;           // slot 0 is pinned to bank 0
    uint8_t       *trap;             // one BR_BANK scratch bank
    uint32_t       fill_us;          // extra freeze past the fill: 0 on hardware, the host's fill cost
    bool           no_stall;         // control: demand-fill with no wait state

    // Wired bit order, CLK-gated live writes, image in flash; NULL each on the host.
    uint16_t (*perm)(uint16_t off);
    void     (*poke)(uint8_t *base, uint16_t off, uint8_t v);
    void     (*fill)(uint8_t *dst, uint16_t bank);
    // Stepped fill, so core1 keeps draining the bus stream: 377 us of blind fill cost 198 us of drain.
    void     (*fill_start)(uint8_t *dst, uint16_t bank);
    bool     (*fill_step)(void);     // move some of it; true while it is not finished
    void     (*remap)(void);         // residency changed: republish the hardware bank table
    // Only where the stall moves a base itself: a select's own mount is the hardware's, and a late
    // copy from core1 would land a stale select on top of a newer one.
    void     (*mount)(const uint8_t *low, const uint8_t *high);
    // Preferred over mount(): replay `byte` through the select DMA, so the bases have one writer.
    void     (*remount)(uint8_t byte);
    // After the mount: the interrupt bit of a vectoring in cycles not yet fed here, or -1. NULL on host.
    int      (*late_vector)(void);
} bigrom_cfg_t;

typedef struct {
    bigrom_cfg_t cfg;
    gbcart_t     cart;
    int32_t      slot_bank[BR_MAX_SLOTS];
    uint32_t     slot_use[BR_MAX_SLOTS];
    uint32_t     use_clock;

    const uint8_t *base[2];          // what the serve ring is mounted on, low and high

    uint8_t   st;
    uint16_t  x;                     // where the trap caught the console
    uint16_t  want[2];
    uint8_t   sel_byte;              // the written byte that armed this stall
    uint32_t  t_arm, t_done;
    int8_t    irq_bit;               // an interrupt vectored into the stall; -1 = none

    // Outstanding resumes, one trampoline each, unwound in order.
    br_pend_t pend[BR_PEND];
    uint8_t   npend;
    uint8_t  *lo_ptr;                // what the release mounts low, decided when the fill lands
    bool      fix_used;              // the in-flight trampoline carries the interrupt repair
    bool      filling;               // an asynchronous fill is in flight into fill_slot
    uint8_t   fill_slot;
    uint16_t  fill_bank;
    // The owner is feeding reads older than the console's present; the release waits it out.
    bool      lagging;

    uint32_t  freezes, hits, misses;
    uint64_t  frozen_us;
    uint32_t  worst_us;
    uint32_t  high_entry;            // the trap caught a fetch in the banked region
    uint32_t  vec_entry;             // ... a vectoring: the interrupt beat the arm by one boundary
    uint32_t  x_clash;               // ... inside the bytes the stall overwrites: unresumable
    uint32_t  nested, nested_miss;   // a bank select arrived while a release was still pending
    uint32_t  nested_drop;           // ... and could not be nested: deeper than BR_PEND
    uint32_t  fixups;                // interrupts eaten by a stall, and repaired by the trampoline
    uint32_t  dup_arm;               // an arm that caught the stall itself, not the game
    uint32_t  stall_write;           // a write during the freeze itself: the stall does none
} bigrom_t;

void bigrom_init(bigrom_t *br, const bigrom_cfg_t *cfg);

// Either /RST edge: registers and any pending release, but not the trap or the cache. Cheap on
// purpose — the owner is about to be handed a running console's bus.
void bigrom_reset(bigrom_t *br);

// A cart-space write: mounts the selected pair, or the trap bank on a miss (on the target the DMA
// already did, inside the next M-cycle).
void bigrom_write(bigrom_t *br, uint16_t addr, uint8_t v, uint32_t now_us);

// Every read cycle, in bus order: learns the resume address and closes the release out.
void bigrom_read(bigrom_t *br, uint16_t addr, uint32_t now_us);

// core1: finishes the fill and runs the release once the freeze has cost what a fill costs.
void bigrom_tick(bigrom_t *br, uint32_t now_us);

// What the serve ring would hand the console for this address.
uint8_t bigrom_serve(const bigrom_t *br, uint16_t addr);

// Residency, for an owner publishing the bank table the arm DMA indexes.
uint8_t *bigrom_slot_ptr(const bigrom_t *br, int slot);

static inline bool bigrom_frozen(const bigrom_t *br) { return br->st != BR_RUN; }

#endif
