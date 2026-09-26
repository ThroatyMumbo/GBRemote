#include "jb_model.h"
#include <string.h>

// Only what the two jbdev programs use: JMP, WAIT, IN, OUT, PULL, MOV, IRQ, SET. Both configure
// IN base and JMP pin to the single data line, so pin 0 is always the wire.
enum { OP_JMP = 0, OP_WAIT, OP_IN, OP_OUT, OP_PUSHPULL, OP_MOV, OP_IRQ, OP_SET };

void jb_rx_init(jb_sm_t *s, const uint16_t *prog, unsigned len, unsigned wt, unsigned wrap,
                unsigned pc, bool (*push)(void *, uint32_t), void *ctx) {
    memset(s, 0, sizeof *s);
    s->prog = prog; s->len = len; s->wrap_target = wt; s->wrap = wrap; s->pc = pc;
    s->push_thresh = 8;                 // sm_config_set_in_shift(&c, false, true, 8)
    s->pull_thresh = 32;
    s->osr_count = 32;
    s->push = push;
    s->ctx = ctx;
    s->enabled = true;
}

void jb_tx_init(jb_sm_t *s, const uint16_t *prog, unsigned len, unsigned wt, unsigned wrap,
                unsigned pc, bool (*pop)(void *, uint32_t *), void *ctx) {
    memset(s, 0, sizeof *s);
    s->prog = prog; s->len = len; s->wrap_target = wt; s->wrap = wrap; s->pc = pc;
    s->push_thresh = 32;
    s->pull_thresh = 8;                 // sm_config_set_out_shift(&c, false, true, 8)
    s->osr_count = 8;                   // empty
    s->sideset_bits = 2;                // .side_set 1 opt => one enable bit plus one value bit
    s->sideset_opt = true;
    s->pop = pop;
    s->ctx = ctx;
    s->enabled = true;
}

void jb_rx_resync(jb_sm_t *s, unsigned pc) {
    s->pc = pc;
    s->delay = 0;
    s->isr = 0;
    s->isr_count = 0;
    s->irq = 0;
}

uint8_t jb_line(const jb_bus_t *b) {
    return (b->master_low || b->tx.pindir) ? 0u : 1u;
}

// One cycle of one SM. Side-set is applied on issue, even when the instruction stalls; the delay
// is not, which is what the datasheet says and what the 4 us cell depends on.
static void step(jb_sm_t *s, uint8_t line) {
    if (!s->enabled || s->pc >= s->len) return;
    if (s->delay) { s->delay--; return; }

    // Autopull is level-triggered: an empty OSR refills the moment the FIFO has anything, which
    // is what starts the reply frame off `pull block` with no trigger of its own.
    if (s->pop && s->osr_count >= s->pull_thresh) {
        uint32_t w;
        if (s->pop(s->ctx, &w)) { s->osr = w; s->osr_count = 0; }
    }

    uint16_t insn = s->prog[s->pc];
    unsigned op   = (insn >> 13) & 7u;
    unsigned field= (insn >> 8) & 0x1fu;
    unsigned arg  = insn & 0xffu;
    unsigned next = (s->pc == s->wrap) ? s->wrap_target : s->pc + 1u;

    unsigned dbits = 5u - s->sideset_bits;
    unsigned delay = field & ((1u << dbits) - 1u);
    unsigned side  = field >> dbits;
    if (s->sideset_bits) {
        bool present = !s->sideset_opt || ((side >> (s->sideset_bits - 1u)) & 1u);
        if (present) s->pindir = (uint8_t)(side & 1u);       // side-set drives PINDIRS
    }

    switch (op) {
    case OP_WAIT: {
        unsigned pol = (arg >> 7) & 1u;
        if (line != pol) { s->stalls++; return; }
        break;
    }
    case OP_IN: {
        unsigned n = arg & 0x1fu;
        if (n == 0) n = 32u;
        uint32_t bits = line & ((n == 32u) ? 0xffffffffu : ((1u << n) - 1u));
        uint32_t isr = (s->isr << n) | bits;                 // shift left => MSB first
        unsigned cnt = s->isr_count + n;
        if (cnt >= s->push_thresh) {
            uint32_t v = isr & ((s->push_thresh == 32u) ? 0xffffffffu
                                                       : ((1u << s->push_thresh) - 1u));
            if (s->push && !s->push(s->ctx, v)) { s->stalls++; return; }   // FIFO full: stall
            isr = 0;
            cnt = 0;
        }
        s->isr = isr;
        s->isr_count = cnt;
        break;
    }
    case OP_OUT: {
        unsigned dst = (arg >> 5) & 7u, n = arg & 0x1fu;
        if (n == 0) n = 32u;
        uint32_t v = (n == 32u) ? s->osr : (s->osr >> (32u - n));
        s->osr = (n == 32u) ? 0u : (s->osr << n);
        s->osr_count += n;
        if (s->osr_count > 32u) s->osr_count = 32u;
        if      (dst == 1u) s->x = v;
        else if (dst == 2u) s->y = v;
        else if (dst == 4u) s->pindir = (uint8_t)(v & 1u);
        break;
    }
    case OP_PUSHPULL:
        // PULL with autopull on is a no-op while the OSR still holds bits, and stalls when empty.
        if (!(arg & 0x80u)) break;                           // PUSH: neither program uses it
        if (s->osr_count >= s->pull_thresh) { s->stalls++; return; }
        break;
    case OP_MOV: {
        unsigned dst = (arg >> 5) & 7u, src = arg & 7u;
        uint32_t v = 0;
        if      (src == 0u) v = line;
        else if (src == 1u) v = s->x;
        else if (src == 2u) v = s->y;
        else if (src == 6u) v = s->isr;
        else if (src == 7u) v = s->osr;
        if      (dst == 1u) s->x = v;
        else if (dst == 2u) s->y = v;
        else if (dst == 6u) { s->isr = v; s->isr_count = 0; }    // MOV ISR clears the counter
        else if (dst == 7u) { s->osr = v; s->osr_count = 0; }
        break;
    }
    case OP_IRQ:
        s->irq |= (uint8_t)(1u << (arg & 7u));
        break;
    case OP_SET: {
        unsigned dst = (arg >> 5) & 7u, v = arg & 0x1fu;
        if      (dst == 1u) s->x = v;
        else if (dst == 2u) s->y = v;
        else if (dst == 4u) s->pindir = (uint8_t)(v & 1u);
        break;
    }
    case OP_JMP: {
        unsigned cond = (arg >> 5) & 7u, addr = arg & 0x1fu;
        int take = 0;
        switch (cond) {
        case 0: take = 1; break;
        case 1: take = (s->x == 0); break;
        case 2: take = (s->x != 0); s->x--; break;
        case 3: take = (s->y == 0); break;
        case 4: take = (s->y != 0); s->y--; break;
        case 5: take = (s->x != s->y); break;
        case 6: take = (line != 0); break;                   // JMP PIN: the data line
        case 7: take = (s->osr_count < s->pull_thresh); break;   // !OSRE
        default: break;
        }
        s->pc = take ? addr : next;
        s->delay = delay;
        return;
    }
    default:
        break;
    }
    s->pc = next;
    s->delay = delay;
}

void jb_bus_step(jb_bus_t *b) {
    uint8_t line = jb_line(b);
    step(&b->rx, line);
    if ((b->tick & 1u) == 0u) step(&b->tx, line);            // TX runs at half the RX clock
    b->tick++;
}

enum { M_HUNT = 0, M_CELL, M_WAIT_HIGH };

void jb_master_reset(jb_master_t *m) {
    memset(m, 0, sizeof *m);
    m->prev = 1;
}

void jb_master_feed(jb_master_t *m, uint8_t level) {
    if (level) m->high_run++;
    else       m->high_run = 0;

    switch (m->state) {
    case M_HUNT:
        if (m->prev && !level) { m->state = M_CELL; m->t = 0; }
        break;
    case M_CELL:
        if (++m->t == JB_SAMPLE_US * JB_TICKS_US) {
            m->sr = (m->sr << 1) | (level & 1u);
            if (++m->nbits % 8u == 0u && m->n < JB_MASTER_MAX)
                m->bytes[m->n++] = (uint8_t)(m->sr & 0xffu);
            m->state = M_WAIT_HIGH;
        }
        break;
    default:
        if (level) m->state = M_HUNT;
        break;
    }
    m->prev = level;
}

bool jb_master_done(const jb_master_t *m) {
    return m->nbits && m->state == M_HUNT && m->high_run >= JB_QUIET_US * JB_TICKS_US;
}
