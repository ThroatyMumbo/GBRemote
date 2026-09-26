#include "pio_core.h"
#include <stdio.h>
#include <stdlib.h>

enum { OP_JMP = 0, OP_WAIT, OP_IN, OP_OUT, OP_PUSHPULL, OP_MOV, OP_IRQ, OP_SET };

static void unsupported(const pio_core_sm_t *s, const char *what) {
    fprintf(stderr, "pio_core: %s at pc=%u insn=%04x\n", what, s->pc, s->prog[s->pc]);
    abort();
}

static uint32_t mask32(unsigned n) { return n >= 32 ? 0xffffffffu : ((1u << n) - 1u); }
static unsigned thresh(uint8_t t) { return t == 0 || t > 32 ? 32u : t; }

// Leaves cycle at 0: when the SM starts is the caller's, and reading it back here would touch
// uninitialized memory on a first init.
void pio_core_init(pio_core_sm_t *s, const uint16_t *prog, const pio_core_cfg_t *c, uint8_t pc) {
    *s = (pio_core_sm_t){ 0 };
    s->prog = prog;
    s->cfg = *c;
    s->pc = pc;
}

// An empty OSR, as SM_RESTART leaves it: the first autopull OUT pulls.
void pio_core_restart(pio_core_sm_t *s) {
    s->isr = s->osr = 0;
    s->isr_count = 0;
    s->osr_count = 32;
    s->push_pending = false;
    s->stalled = false;
}

static void advance(pio_core_sm_t *s) {
    s->pc = s->pc == s->cfg.wrap ? s->cfg.wrap_target : (uint8_t)(s->pc + 1u);
}

static uint32_t src_value(const pio_core_sm_t *s, unsigned src, uint64_t pins) {
    switch (src) {
    case 0: return (uint32_t)(pins >> s->cfg.in_base);
    case 1: return s->x;
    case 2: return s->y;
    case 3: return 0;
    case 6: return s->isr;
    case 7: return s->osr;
    default: return 0;
    }
}

static void isr_shift(pio_core_sm_t *s, uint32_t v, unsigned n) {
    v &= mask32(n);
    if (s->cfg.in_shift_right) s->isr = (s->isr >> n) | (n == 32 ? v : v << (32u - n));
    else                       s->isr = (n == 32 ? 0 : s->isr << n) | v;
    s->isr_count = (uint8_t)(s->isr_count + n > 32u ? 32u : s->isr_count + n);
}

static uint32_t osr_shift(pio_core_sm_t *s, unsigned n) {
    uint32_t v;
    if (s->cfg.out_shift_right) { v = s->osr & mask32(n); s->osr = n == 32 ? 0 : s->osr >> n; }
    else                        { v = n == 32 ? s->osr : s->osr >> (32u - n); s->osr = n == 32 ? 0 : s->osr << n; }
    s->osr_count = (uint8_t)(s->osr_count + n > 32u ? 32u : s->osr_count + n);
    return v;
}

static void write_pins(const pio_core_bus_t *bus, unsigned base,
                       unsigned count, uint32_t v, bool dirs) {
    if (!count) return;
    const uint64_t m = (uint64_t)mask32(count) << base;
    const uint64_t val = (uint64_t)(v & mask32(count)) << base;
    if (dirs) bus->dirs_set(bus->u, val, m);
    else      bus->pins_set(bus->u, val, m);
}

// Side-set is applied when the instruction issues, even if it then stalls; the delay is not.
static unsigned side_delay(pio_core_sm_t *s, const pio_core_bus_t *bus, uint16_t insn) {
    const unsigned field = (insn >> 8) & 0x1fu;
    const unsigned ss = s->cfg.sideset_bits;
    if (!ss) return field;
    const unsigned dbits = 5u - ss;
    const unsigned sv = field >> dbits;
    const unsigned enable = s->cfg.sideset_opt ? (sv >> (ss - 1u)) & 1u : 1u;
    const unsigned bits = s->cfg.sideset_opt ? ss - 1u : ss;
    if (enable)
        write_pins(bus,s->cfg.sideset_base, bits, sv & mask32(bits), s->cfg.sideset_pindirs);
    return field & mask32(dbits);
}

static bool do_push(pio_core_sm_t *s, const pio_core_bus_t *bus) {
    if (!bus->rx_push(bus->u, s->isr)) return false;
    s->isr = 0;
    s->isr_count = 0;
    return true;
}

bool pio_core_step(pio_core_sm_t *s, const pio_core_bus_t *bus) {
    // A stalled autopush retries the push alone: the IN already shifted and must not shift twice.
    if (s->push_pending) {
        if (!do_push(s, bus)) { s->stalled = true; s->stalls++; s->push_stalls++; return false; }
        s->push_pending = false;
        s->stalled = false;
        s->insns++;
        s->cycle++;
        advance(s);
        return true;
    }

    const uint16_t insn = s->prog[s->pc];
    const unsigned op = (insn >> 13) & 7u;
    const unsigned delay = side_delay(s, bus, insn);
    bool jumped = false;
    // Resolved on demand: most instructions never look at a pin, and this runs tens of times per
    // bus cycle over millions of cart reads.
    uint64_t pins = 0;
    bool pins_read = false;
#define PINS() (pins_read ? pins : (pins_read = true, pins = bus->pins_get(bus->u)))

    switch (op) {
    case OP_JMP: {
        const unsigned cond = (insn >> 5) & 7u;
        bool take = false;
        switch (cond) {
        case 0: take = true; break;
        case 1: take = s->x == 0; break;
        case 2: take = s->x != 0; s->x--; break;
        case 3: take = s->y == 0; break;
        case 4: take = s->y != 0; s->y--; break;
        case 5: take = s->x != s->y; break;
        case 6: take = ((PINS() >> s->cfg.jmp_pin) & 1u) != 0; break;
        case 7: take = s->osr_count < thresh(s->cfg.out_pull_threshold); break;
        }
        if (take) { s->pc = (uint8_t)(insn & 0x1fu); jumped = true; }
        break;
    }
    case OP_WAIT: {
        const unsigned pol = (insn >> 7) & 1u, wsrc = (insn >> 5) & 3u, idx = insn & 0x1fu;
        bool level;
        if (wsrc == 0)      level = ((PINS() >> idx) & 1u) != 0;
        else if (wsrc == 1) level = ((PINS() >> (s->cfg.in_base + idx)) & 1u) != 0;
        else if (wsrc == 2) level = bus->irq_get(bus->u, idx & 7u);
        else { unsupported(s, "wait on JMPPIN"); return false; }
        if (level != (pol != 0)) { s->stalled = true; s->stalls++; return false; }
        if (wsrc == 2 && pol) bus->irq_set(bus->u, idx & 7u, false);   // pol=1 clears on satisfy
        break;
    }
    case OP_IN: {
        const unsigned isrc = (insn >> 5) & 7u;
        unsigned n = insn & 0x1fu;
        if (!n) n = 32u;
        isr_shift(s, src_value(s, isrc, isrc == 0 ? PINS() : 0u), n);
        if (s->cfg.in_autopush && s->isr_count >= thresh(s->cfg.in_push_threshold)) {
            if (!do_push(s, bus)) {
                s->push_pending = true; s->stalled = true; s->stalls++; s->push_stalls++;
                return false;
            }
        }
        break;
    }
    case OP_OUT: {
        const unsigned dst = (insn >> 5) & 7u;
        unsigned n = insn & 0x1fu;
        if (!n) n = 32u;
        if (s->cfg.out_autopull && s->osr_count >= thresh(s->cfg.out_pull_threshold)) {
            uint32_t w;
            if (!bus->tx_pop(bus->u, &w)) { s->stalled = true; s->stalls++; return false; }
            s->osr = w;
            s->osr_count = 0;
        }
        const uint32_t v = osr_shift(s, n);
        switch (dst) {
        case 0: write_pins(bus,s->cfg.out_base, s->cfg.out_count, v, false); break;
        case 1: s->x = v; break;
        case 2: s->y = v; break;
        case 3: break;
        case 4: write_pins(bus,s->cfg.out_base, s->cfg.out_count, v, true); break;
        case 5: s->pc = (uint8_t)(v & 0x1fu); jumped = true; break;
        case 6: s->isr = v; s->isr_count = (uint8_t)n; break;
        default: unsupported(s, "out exec"); return false;
        }
        break;
    }
    case OP_PUSHPULL: {
        const bool is_pull = (insn >> 7) & 1u, cond = (insn >> 6) & 1u, block = (insn >> 5) & 1u;
        if (is_pull) {
            if (cond && s->osr_count < thresh(s->cfg.out_pull_threshold)) break;   // pull ifempty
            uint32_t w;
            if (!bus->tx_pop(bus->u, &w)) {
                if (block) { s->stalled = true; s->stalls++; return false; }
                s->osr = s->x;                                  // pull noblock copies X
            } else {
                s->osr = w;
            }
            s->osr_count = 0;
        } else {
            if (cond && s->isr_count < thresh(s->cfg.in_push_threshold)) break;    // push iffull
            if (!do_push(s, bus) && block) { s->stalled = true; s->stalls++; return false; }
            if (!block) { s->isr = 0; s->isr_count = 0; }
        }
        break;
    }
    case OP_MOV: {
        const unsigned dst = (insn >> 5) & 7u, xform = (insn >> 3) & 3u, msrc = insn & 7u;
        if (msrc == 5) unsupported(s, "mov from status");
        uint32_t v = src_value(s, msrc, msrc == 0 ? PINS() : 0u);
        if (xform == 1) v = ~v;
        else if (xform == 2) {
            uint32_t r = 0;
            for (unsigned i = 0; i < 32u; i++) if (v & (1u << i)) r |= 1u << (31u - i);
            v = r;
        }
        switch (dst) {
        case 0: write_pins(bus,s->cfg.out_base, s->cfg.out_count, v, false); break;
        case 1: s->x = v; break;
        case 2: s->y = v; break;
        case 5: s->pc = (uint8_t)(v & 0x1fu); jumped = true; break;
        case 6: s->isr = v; s->isr_count = 0; break;
        case 7: s->osr = v; s->osr_count = 0; break;
        default: unsupported(s, "mov exec"); return false;
        }
        break;
    }
    case OP_IRQ: {
        if (insn & 0x10u) unsupported(s, "irq rel");
        const unsigned num = insn & 7u, clr = (insn >> 6) & 1u, wait = (insn >> 5) & 1u;
        if (clr) {
            bus->irq_set(bus->u, num, false);
        } else if (wait) {
            if (bus->irq_get(bus->u, num)) { s->stalled = true; s->stalls++; return false; }
            bus->irq_set(bus->u, num, true);
        } else {
            bus->irq_set(bus->u, num, true);
        }
        break;
    }
    case OP_SET: {
        const unsigned dst = (insn >> 5) & 7u, data = insn & 0x1fu;
        switch (dst) {
        case 0: write_pins(bus,s->cfg.set_base, s->cfg.set_count, data, false); break;
        case 1: s->x = data; break;
        case 2: s->y = data; break;
        case 4: write_pins(bus,s->cfg.set_base, s->cfg.set_count, data, true); break;
        default: unsupported(s, "set to reserved destination"); return false;
        }
        break;
    }
    }

#undef PINS
    if (!jumped) advance(s);
    s->stalled = false;
    s->insns++;
    s->cycle += 1u + delay;
    return true;
}

void pio_core_run(pio_core_sm_t *const *sms, const pio_core_bus_t *buses, unsigned n,
                  uint64_t until) {
    uint64_t t = UINT64_MAX;
    for (unsigned i = 0; i < n; i++) if (sms[i]->cycle < t) t = sms[i]->cycle;
    for (; t < until; t++)
        for (unsigned i = 0; i < n; i++)
            if (sms[i]->cycle == t && !pio_core_step(sms[i], &buses[i])) sms[i]->cycle = t + 1u;
}
