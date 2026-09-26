#include "maple_model.h"
#include <string.h>

// Only what maple_tx uses: SET, OUT, JMP, PULL, plus side-set and delay. Pin 0 is SDCKA and pin 1
// is SDCKB, matching sm_config_set_set_pins(pin_a, 2) / sm_config_set_sideset_pins(pin_b).
enum { OP_JMP = 0, OP_WAIT, OP_IN, OP_OUT, OP_PUSHPULL, OP_MOV, OP_IRQ, OP_SET };

const uint16_t maple_tx_reference[29] = {
    0xf080, 0x9ca0, 0x7020, 0xf083, 0xf043, 0xfc02, 0xac42, 0x1c86, 0xf603, 0xac42,
    0x6041, 0xe501, 0x0070, 0xb642, 0xfc02, 0x1613, 0xa642, 0xec00, 0xb642, 0x7601,
    0x0b4a, 0xec01, 0xbc42, 0xac42, 0xec00, 0xec01, 0xec00, 0xec01, 0xfc03,
};

typedef struct {
    uint32_t osr, x, y;
    unsigned osr_count;             // bits already shifted out; 32 == empty
    uint8_t  pin_out[2], pindirs[2];
    unsigned pc;
    const uint32_t *fifo;
    unsigned nwords, rd;
} sm_t;

static int fifo_pull(sm_t *s) {
    if (s->rd >= s->nwords) return 0;
    s->osr = s->fifo[s->rd++];
    s->osr_count = 0;
    return 1;
}

// Shift-left OUT: bits leave from the MSB end.
static int out_bits(sm_t *s, unsigned n, uint32_t *v) {
    if (s->osr_count >= 32 && !fifo_pull(s)) return 0;
    if (n == 32) {
        *v = s->osr;
        s->osr = 0;
        s->osr_count = 32;
        return 1;
    }
    *v = (s->osr >> (32 - n)) & ((1u << n) - 1u);
    s->osr <<= n;
    s->osr_count += n;
    return 1;
}

static uint8_t bus(const sm_t *s) {
    uint8_t a = s->pindirs[0] ? s->pin_out[0] : 1u;
    uint8_t b = s->pindirs[1] ? s->pin_out[1] : 1u;
    return (uint8_t)(a | (b << 1));
}

static void emit(maple_trace_t *t, uint8_t st, uint32_t cycles) {
    if (t->n && t->state[t->n - 1] == st) {
        t->hold[t->n - 1] += cycles;
        return;
    }
    if (t->n >= MAPLE_MAX_TRACE) { t->overflow = 1; return; }
    t->state[t->n] = st;
    t->hold[t->n] = cycles;
    t->n++;
}

void maple_tx_sim(const uint16_t *prog, unsigned len, unsigned wrap_target, unsigned wrap,
                  const uint32_t *words, unsigned nwords, maple_trace_t *out) {
    sm_t s;
    memset(&s, 0, sizeof s);
    s.fifo = words;
    s.nwords = nwords;
    s.osr_count = 32;
    s.pc = wrap_target;
    // maple_tx_program_init leaves both output registers high with both pins as inputs.
    s.pin_out[0] = s.pin_out[1] = 1;

    memset(out, 0, sizeof *out);

    // Bounded: any real frame ends long before this, and a stall must not hang the test.
    int started = 0;
    for (unsigned guard = 0; guard < 200000u; guard++) {
        if (s.pc >= len) break;
        uint16_t insn = prog[s.pc];
        unsigned op    = (insn >> 13) & 7u;
        unsigned side  = (insn >> 12) & 1u;
        unsigned delay = (insn >> 8) & 0xfu;
        unsigned arg   = insn & 0xffu;
        unsigned next  = (s.pc == wrap) ? wrap_target : s.pc + 1u;

        // Side-set is applied with the instruction. These programs never write SDCKB both ways in
        // one instruction, so the order between the two is not observable.
        s.pin_out[1] = (uint8_t)side;

        switch (op) {
        case OP_SET: {
            unsigned dst = (arg >> 5) & 7u, data = arg & 0x1fu;
            if (dst == 0) { s.pin_out[0] = data & 1u; s.pin_out[1] = (data >> 1) & 1u; }
            else if (dst == 1) s.x = data;
            else if (dst == 2) s.y = data;
            else if (dst == 4) { s.pindirs[0] = data & 1u; s.pindirs[1] = (data >> 1) & 1u; }
            break;
        }
        case OP_OUT: {
            unsigned dst = (arg >> 5) & 7u, n = arg & 0x1fu;
            if (n == 0) n = 32;
            uint32_t v;
            if (!out_bits(&s, n, &v)) return;       // stalled with an empty FIFO: frame over
            if (dst == 0) s.pin_out[0] = v & 1u;    // out pins,1 -> SDCKA only
            else if (dst == 1) s.x = v;
            else if (dst == 2) s.y = v;
            break;
        }
        case OP_PUSHPULL:
            if (arg & 0x80u) { if (!fifo_pull(&s)) return; }
            break;
        case OP_JMP: {
            unsigned cond = (arg >> 5) & 7u, addr = arg & 0x1fu;
            int take = 0;
            switch (cond) {
            case 0: take = 1; break;
            case 1: take = (s.x == 0); break;
            case 2: take = (s.x != 0); s.x--; break;
            case 3: take = (s.y == 0); break;
            case 4: take = (s.y != 0); s.y--; break;
            case 7: take = (s.osr_count < 32); break;
            default: break;
            }
            if (take) next = addr;
            break;
        }
        default:
            break;                                  // WAIT/IN/MOV/IRQ are unused by maple_tx
        }

        uint32_t cycles = 1u + delay;
        out->cycles += cycles;
        emit(out, bus(&s), cycles);
        s.pc = next;

        // One frame only. Past the wrap the OSR still holds the tail of the last word, which is
        // why mapledev.c restarts the SM before staging each reply.
        if (started && next == wrap_target) break;
        started = 1;
    }
}
