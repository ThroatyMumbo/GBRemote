#include "bigrom.h"
#include <string.h>

// `JP BR_LOOP`'s operands run on an out-of-phase entry: $7F is LD A,A and $00 is NOP.
#define LOOP_LO   ((uint8_t)(BR_LOOP & 0xFF))
#define LOOP_HI   ((uint8_t)(BR_LOOP >> 8))

static inline void br_mount(bigrom_t *br) {
    if (br->cfg.mount) br->cfg.mount(br->base[0], br->base[1]);
}

static inline uint16_t br_p(const bigrom_t *br, uint16_t off) {
    return br->cfg.perm ? br->cfg.perm(off) : off;
}
static inline uint8_t br_peek(const bigrom_t *br, const uint8_t *base, uint16_t off) {
    return base[br_p(br, off)];
}
static inline void br_poke(bigrom_t *br, uint8_t *base, uint16_t off, uint8_t v) {
    if (br->cfg.poke) br->cfg.poke(base, off, v); else base[br_p(br, off)] = v;
}

static void put3(bigrom_t *br, uint8_t *base, uint16_t off, uint8_t lo, uint8_t hi) {
    br_poke(br, base, off,              0xC3);
    br_poke(br, base, (uint16_t)(off + 1), lo);
    br_poke(br, base, (uint16_t)(off + 2), hi);
}

static void trap_fill(bigrom_t *br, uint8_t *t) {
    for (unsigned i = 0; i < BR_BANK; i++) {
        unsigned ph = (i + 3u - (BR_LOOP % 3u)) % 3u;    // the JP has to start ON BR_LOOP
        t[br_p(br, (uint16_t)i)] = ph == 0 ? 0xC3 : ph == 1 ? LOOP_LO : LOOP_HI;
    }
    // Nothing else goes in this bank, not even vector stubs.
}

static int slot_find(const bigrom_t *br, uint16_t bank) {
    for (unsigned i = 0; i < br->cfg.nslots; i++)
        if (br->slot_bank[i] == (int32_t)bank) return (int)i;
    return -1;
}

uint8_t *bigrom_slot_ptr(const bigrom_t *br, int slot) { return br->cfg.slot[slot]; }

static void slot_touch(bigrom_t *br, int s) { br->slot_use[s] = ++br->use_clock; }

static void slot_load(bigrom_t *br, int s, uint16_t bank) {
    if (br->cfg.fill) br->cfg.fill(br->cfg.slot[s], bank);
    else              memcpy(br->cfg.slot[s], br->cfg.img + (uint32_t)bank * BR_BANK, BR_BANK);
    br->slot_bank[s] = bank;
    slot_touch(br, s);
    if (br->cfg.remap) br->cfg.remap();
}

// Never a victim: slot 0, anything mounted, or a slot carrying an outstanding release's stages.
static bool slot_staged(const bigrom_t *br, const uint8_t *p) {
    for (unsigned i = 0; i < br->npend; i++)
        if (br->pend[i].hi == p) return true;
    return false;
}

static int slot_victim(const bigrom_t *br) {
    int best = -1;
    for (unsigned i = 1; i < br->cfg.nslots; i++) {
        const uint8_t *p = br->cfg.slot[i];
        // An in-flight fill's slot reads as empty, so skip it explicitly.
        if (br->filling && i == br->fill_slot) continue;
        if (p == br->base[0] || p == br->base[1] || slot_staged(br, p)) continue;
        if (br->slot_bank[i] < 0) return (int)i;
        if (best < 0 || br->slot_use[i] < br->slot_use[best]) best = (int)i;
    }
    return best < 0 ? 1 : best;     // every slot mounted: only reachable with the stall disabled
}

#define TR_OFF (BR_TRAMP & (BR_BANK - 1))

// The resume `ei; jp X`, behind a self-checking repair for an eaten interrupt.
static unsigned write_tramp(bigrom_t *br, uint8_t *hi, uint16_t x, int irq) {
    uint8_t t[BR_TRAMP_LEN];
    unsigned n = 0;
    if (irq >= 0) {
        t[n++] = 0xF5;                       // push af
        t[n++] = 0xE5;                       // push hl
        t[n++] = 0xF8; t[n++] = 0x04;        // ld hl,sp+4: the word on top when we were entered
        t[n++] = 0x2A;                       // ld a,[hl+]
        t[n++] = 0x66;                       // ld h,[hl]
        t[n++] = 0x6F;                       // ld l,a      -> hl = the pushed PC
        t[n++] = 0x7C;                       // ld a,h
        t[n++] = 0xB7;                       // or a — HIGH(BR_LOOP) is 0, the park is in bank 0
        t[n++] = 0x20; t[n++] = 0x05;        // jr nz,.chkx
        t[n++] = 0x7D;                       // ld a,l
        t[n++] = 0xFE; t[n++] = LOOP_LO;     // cp LOW(BR_LOOP)
        t[n++] = 0x28; t[n++] = 0x0E;        // jr z,.owed
        t[n++] = 0x7D;                       // .chkx: ld a,l
        t[n++] = 0xD6; t[n++] = (uint8_t)x;  // sub LOW(x)
        t[n++] = 0x6F;                       // ld l,a
        t[n++] = 0x7C;                       // ld a,h
        t[n++] = 0xDE; t[n++] = (uint8_t)(x >> 8);   // sbc HIGH(x)
        t[n++] = 0x20; t[n++] = 0x13;        // jr nz,.no
        t[n++] = 0x7D;                       // ld a,l
        t[n++] = 0xFE; t[n++] = 0x03;        // cp 3
        t[n++] = 0x30; t[n++] = 0x0E;        // jr nc,.no
        if (n != BR_TR_OWED) n = 0;          // offsets are read off the bus; degrade to the plain
                                             // jump rather than to an empty trampoline
        t[n++] = 0xF0; t[n++] = 0x0F;        // .owed: ldh a,[$FF0F]
        t[n++] = 0xF6; t[n++] = (uint8_t)(1u << irq);
        t[n++] = 0xE0; t[n++] = 0x0F;        // ldh [$FF0F],a: re-request what vectoring ate
        t[n++] = 0xE1;                       // pop hl
        t[n++] = 0xF1;                       // pop af
        t[n++] = 0x33; t[n++] = 0x33;        // inc sp x2: drop it. INC SP sets no flags
        t[n++] = 0xFB;                       // ei
        t[n++] = 0xC3; t[n++] = (uint8_t)x; t[n++] = (uint8_t)(x >> 8);
        if (n != BR_TR_SKIP) n = 0;
        // .no: post the rejected stack word to the cart edge; $6000-$7FFF is a register on MBC1/MBC3.
        if (br->cart.mbc == MBC5 || br->cart.mbc == MBC_NONE) {
            t[n++] = 0xF8; t[n++] = 0x04;    // ld hl,sp+4
            t[n++] = 0x2A;                   // ld a,[hl+]
            t[n++] = 0xEA; t[n++] = (uint8_t)BR_SKIP_REPORT;
            t[n++] = (uint8_t)(BR_SKIP_REPORT >> 8);
            t[n++] = 0x7E;                   // ld a,[hl]
            t[n++] = 0xEA; t[n++] = (uint8_t)(BR_SKIP_REPORT + 1);
            t[n++] = (uint8_t)((BR_SKIP_REPORT + 1) >> 8);
        }
        t[n++] = 0xE1;
        t[n++] = 0xF1;
    }
    t[n++] = 0xC3; t[n++] = (uint8_t)x; t[n++] = (uint8_t)(x >> 8);
    for (unsigned i = 0; i < n; i++) br_poke(br, hi, (uint16_t)(TR_OFF + i), t[i]);
    return n;
}

// Copy the park into bank 0's unmounted slot so the later mount underneath is invisible.
static void br_stage_park(bigrom_t *br, br_pend_t *p, uint8_t *lo) {
    p->lo = lo;
    for (unsigned i = 0; i < BR_LOOP_LEN; i++)
        p->lo_save[i] = br_peek(br, lo, (uint16_t)(BR_LOOP + i));
    put3(br, lo, BR_LOOP, LOOP_LO, BR_PAGE_LOW);      // byte for byte what the stall bank holds
}

// Written after the mount: the first moment "is a repair owed?" has a final answer.
static void br_stage_tramp(bigrom_t *br, br_pend_t *p, uint8_t *hi, uint16_t x) {
    p->hi = hi;
    p->x  = x;
    for (unsigned i = 0; i < BR_TRAMP_LEN; i++)
        p->tr_save[i] = br_peek(br, hi, (uint16_t)(TR_OFF + i));
    p->tr_len = (uint8_t)write_tramp(br, hi, x, br->irq_bit);
    br->fix_used = br->irq_bit >= 0;
    if (br->fix_used) br->fixups++;
}

// Only once the console has read X, the one moment it provably stands on neither slot's patch.
static void br_unstage(bigrom_t *br, br_pend_t *p) {
    for (unsigned i = 0; i < BR_LOOP_LEN; i++)
        br_poke(br, p->lo, (uint16_t)(BR_LOOP + i), p->lo_save[i]);
    for (unsigned i = 0; i < p->tr_len; i++)
        br_poke(br, p->hi, (uint16_t)(TR_OFF + i), p->tr_save[i]);
}

// Any fetch in the vector page, not an exact $0040/$0048/... match: the sampled low bits jitter.
static int vector_bit(uint16_t a) {
    return (a >= 0x40 && a < 0x68) ? (int)((a - 0x40) >> 3) : -1;
}

void bigrom_init(bigrom_t *br, const bigrom_cfg_t *cfg) {
    memset(br, 0, sizeof *br);
    br->cfg = *cfg;
    gbcart_reset(&br->cart);
    br->cart.mbc       = cfg->mbc;
    br->cart.has_rtc   = cfg->has_rtc;
    br->cart.rom_banks = cfg->banks;
    br->irq_bit = -1;
    for (unsigned i = 0; i < cfg->nslots; i++) br->slot_bank[i] = -1;
    slot_load(br, 0, 0);
    slot_load(br, 1, 1);
    trap_fill(br, cfg->trap);
    br->base[0] = br->cfg.slot[0];
    br->base[1] = br->cfg.slot[1];
    br->st = BR_RUN;
}

uint8_t bigrom_serve(const bigrom_t *br, uint16_t addr) {
    return br_peek(br, br->base[(addr >> 14) & 1], addr & (BR_BANK - 1));
}

// Keeps the trap and the cache: a full bigrom_init() would blind the owner just as the bus ring fills.
void bigrom_reset(bigrom_t *br) {
    gbcart_mbc_reset(&br->cart);
    br->st       = BR_RUN;
    br->npend    = 0;
    br->filling  = false;
    br->fix_used = false;
    br->lagging  = false;
    br->irq_bit  = -1;
    br->x        = 0;

    if (br->slot_bank[0] != 0) slot_load(br, 0, 0);          // pinned, so normally already there
    int s1 = slot_find(br, 1);
    if (s1 < 0) { s1 = br->cfg.nslots > 1 ? 1 : 0; slot_load(br, s1, 1); }
    br->base[0] = br->cfg.slot[0];
    br->base[1] = br->cfg.slot[s1];
    if (br->cfg.remap) br->cfg.remap();
}

void bigrom_write(bigrom_t *br, uint16_t addr, uint8_t v, uint32_t now_us) {
    if (addr >= 0x8000) return;
    // While frozen the game cannot write, so this is the trampoline's own post.
    if (br->st != BR_RUN && (addr == BR_SKIP_REPORT || addr == BR_SKIP_REPORT + 1)) return;
    gbcart_write(&br->cart, addr, v);

    uint16_t w0 = gbcart_want_bank(&br->cart, 0), w1 = gbcart_want_bank(&br->cart, 1);
    int s0 = slot_find(br, w0), s1 = slot_find(br, w1);

    if (br->st == BR_CATCH || br->st == BR_FILL) { br->stall_write++; return; }

    // A handler's select during a release: counted, then handled like any other.
    if (br->st == BR_HANDOFF || br->st == BR_LEAVING) {
        br->nested++;
        if (s1 < 0) br->nested_miss++;
    }

    if (s0 >= 0 && s1 >= 0) {
        br->base[0] = br->cfg.slot[s0];
        br->base[1] = br->cfg.slot[s1];
        slot_touch(br, s0);
        slot_touch(br, s1);
        br->hits++;
        return;
    }

    // The arm; on the target the select DMA already did this and the mirror follows.
    if (!br->cfg.no_stall) br->base[0] = br->base[1] = br->cfg.trap;
    br->want[0]   = w0;
    br->want[1]   = w1;
    br->sel_byte  = v;
    br->st      = BR_CATCH;
    br->x        = 0xFFFF;
    br->irq_bit  = -1;
    br->t_arm   = now_us;
    br->t_done  = now_us + br->cfg.fill_us * (uint32_t)((s0 < 0) + (s1 < 0));
    br->misses++;
    br->freezes++;
}

void bigrom_read(bigrom_t *br, uint16_t addr, uint32_t now_us) {
    (void)now_us;
    if (addr >= 0x8000) return;

    switch (br->st) {
    case BR_CATCH:
        // X inside the stall's own code is a duplicate arm; unreachable with writes in bus order, so counted.
        if ((addr >= BR_LOOP  && addr < BR_LOOP + BR_LOOP_LEN) ||
            (addr >= BR_TRAMP && addr < BR_TRAMP + BR_TRAMP_LEN)) {
            br->dup_arm++;
            br->st = br->npend ? BR_HANDOFF : BR_RUN;   // walk the outstanding one again
            break;
        }
        br->x  = addr;                            // the opcode fetch the trap hijacked
        br->st = BR_FILL;
        if (addr >= 0x4000) br->high_entry++;
        // Counted only: the SM83 prefetches, so X is the real next PC and a vectoring follows it.
        if (vector_bit(addr) >= 0) br->vec_entry++;
        break;
    case BR_FILL: {
        // At most one per freeze: vectoring clears IME.
        int b = vector_bit(addr);
        if (b >= 0 && br->irq_bit < 0) br->irq_bit = b;
        break;
    }
    case BR_HANDOFF: {
        // Still on the stall bank, so a vectoring now is eaten too.
        int vb = vector_bit(addr);
        if (vb >= 0 && br->irq_bit < 0) br->irq_bit = vb;
        // Mount only on a park read that is the console's present, so the test above precedes it.
        if (addr < BR_LOOP || addr >= BR_LOOP + BR_LOOP_LEN) break;
        if (br->lagging) break;

        br_pend_t *p = &br->pend[br->npend - 1];
        // Nothing between the deciding read and the mount: inside a vectoring the two banks differ.
        br->base[0] = p->lo;
        br->base[1] = p->hi;
        if (br->cfg.remount) br->cfg.remount(br->sel_byte); else br_mount(br);
        // Catch a vectoring eaten between that read and the mount.
        if (br->cfg.late_vector) {
            int v = br->cfg.late_vector();
            if (v >= 0 && br->irq_bit < 0) br->irq_bit = v;
        }
        br_stage_tramp(br, p, p->hi, p->x);
        // The release: one byte turns `JP $007F` into `JP $617F`; either value is a legal target.
        br_poke(br, p->lo, BR_LOOP + 2, BR_PAGE_TR);
        br->st = BR_LEAVING;
        break;
    }

    case BR_LEAVING: {
        // Only the read at X ends a release; a handler taken on the way `reti`s back to intact stages.
        if (!br->npend) { br->st = BR_RUN; break; }
        br_pend_t *p = &br->pend[br->npend - 1];
        if (addr != p->x) break;
        br_unstage(br, p);
        if (--br->npend == 0) br->st = BR_RUN;   // else: back to waiting on the one that nested
        break;
    }
    default:
        break;
    }
}

void bigrom_tick(bigrom_t *br, uint32_t now_us) {
    if (br->st != BR_FILL || now_us < br->t_done) return;

    // Stepped where the owner supports it, so core1 keeps consuming the bus stream during the fill.
    if (br->cfg.fill_start) {
        if (br->filling) {
            if (br->cfg.fill_step()) return;
            br->filling = false;
            br->slot_bank[br->fill_slot] = (int32_t)br->fill_bank;
            slot_touch(br, br->fill_slot);
            if (br->cfg.remap) br->cfg.remap();
        }
        for (unsigned r = 0; r < 2; r++) {
            if (slot_find(br, br->want[r]) >= 0) continue;
            int s = slot_victim(br);
            // Empty before the first byte lands, so a select of the evicted bank hits the trap.
            br->slot_bank[s] = -1;
            br->fill_slot = (uint8_t)s;
            br->fill_bank = br->want[r];
            br->filling   = true;
            if (br->cfg.remap) br->cfg.remap();
            br->cfg.fill_start(br->cfg.slot[s], br->want[r]);
            return;
        }
    } else {
        for (unsigned r = 0; r < 2; r++) {
            if (slot_find(br, br->want[r]) >= 0) continue;
            slot_load(br, slot_victim(br), br->want[r]);
        }
    }
    int s0 = slot_find(br, br->want[0]), s1 = slot_find(br, br->want[1]);
    uint8_t *low = br->cfg.slot[s0], *hi = br->cfg.slot[s1];

    if (br->cfg.no_stall) {
        br->base[0] = low;
        br->base[1] = hi;
        br->st = BR_RUN;
        return;
    }

    if ((br->x >= BR_LOOP  && br->x < BR_LOOP + BR_LOOP_LEN) ||
        (br->x >= BR_TRAMP && br->x < BR_TRAMP + BR_TRAMP_LEN)) br->x_clash++;

    br->lo_ptr = low;
    // Never taken (IME bounds nesting at two); an overflow loses one resume, not memory.
    if (br->npend >= BR_PEND) { br->npend = BR_PEND - 1; br->nested_drop++; }
    br_pend_t *p = &br->pend[br->npend++];
    p->x  = br->x;
    p->lo = low;
    p->hi = hi;
    // Here, not at the handoff: keeps CLK-gated pokes out from between the deciding read and the mount.
    br_stage_park(br, p, low);

    br->frozen_us += now_us - br->t_arm;
    if (now_us - br->t_arm > br->worst_us) br->worst_us = now_us - br->t_arm;
    br->st = BR_HANDOFF;
}
