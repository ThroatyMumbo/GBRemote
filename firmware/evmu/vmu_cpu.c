// CPU, run loop and reset, from libevmu's evmu_cpu.c, evmu_clock.c and evmu_ram.c (MIT, Falco Girgis).
// Time is an integer ns deficit: cc * tcyc_ns per instruction, libevmu's pricing without the double.
#include "vmu_internal.h"
#include <string.h>

// Indexed by ((OCR >> 4) & 3) | (OCR7 ? 4 : 0). Values from libevmu's evmu_clock.h.
static const uint32_t tcyc_ns_[8] = {
    13648, 2000, 366210, 2000,      // /12: RC, CF, quartz, CF
    6824,  1000, 183105, 1000,      // /6
};

uint32_t vmu_tcyc_ns(const vmu_t *v) {
    uint8_t ocr = sfrc_(v, SFR_OCR);
    return tcyc_ns_[((ocr >> 4) & 3U) | ((ocr & OCR_OCR7) ? 4U : 0U)];
}

void vmu_execute(vmu_t *v, const vmu_instr_t *in) {
    const vmu_operands_t *op = &in->operands;
#define PC              v->pc
#define READ(a)         vmu_read(v, (uint16_t)(a))
#define READ_LATCH(a)   vmu_read_latch(v, (uint16_t)(a))
#define VIEW(a)         vmu_view(v, (uint16_t)(a))
#define WRITE(a, x)     vmu_write(v, (uint16_t)(a), (uint8_t)(x))
#define INDIRECT()      vmu_indirect_addr(v, op->indirect)
#define PUSH(x)         vmu_push(v, (uint8_t)(x))
#define POP()           vmu_pop(v)
#define PUSH_PC()       do { PUSH(PC & 0xff); PUSH(PC >> 8); } while (0)
#define POP_PC()        do { PC = (uint16_t)(POP() << 8); PC |= POP(); } while (0)
#define PSW_SET(m, e)   WRITE(SFR_PSW, (READ(SFR_PSW) & ~(m)) | ((e) ? (m) : 0))
#define LOGIC(o, rhs)   WRITE(SFR_ACC, READ(SFR_ACC) o (rhs))
#define BR(e, off)      do { if (e) PC = (uint16_t)(PC + (off)); } while (0)
#define ARITH(o, rhs, use_cy, cy_e, ac_e, ov_e) do {                           \
        const int a = READ(SFR_ACC);                                           \
        const int b = (rhs);                                                   \
        const int c = (use_cy) ? ((READ(SFR_PSW) & PSW_CY) >> 7) : 0;          \
        const int r = a o b o c;                                               \
        uint8_t p = READ(SFR_PSW) & (uint8_t)~(PSW_CY | PSW_AC | PSW_OV);      \
        p |= (cy_e) ? PSW_CY : 0;                                              \
        p |= (ac_e) ? PSW_AC : 0;                                              \
        p |= (ov_e) ? PSW_OV : 0;                                              \
        WRITE(SFR_ACC, r);                                                     \
        WRITE(SFR_PSW, p);                                                     \
    } while (0)
#define ADD_CY  (a + b + c > 255)
#define ADD_AC  ((a & 0xf) + ((b + c) & 0xf) > 0xf)
#define ADD_OV  (0x80 & (~a ^ (b + c)) & ((b + c) ^ (a + (b + c))))
#define SUB_CY  (a - b - c < 0)
#define SUB_AC  ((a & 0xf) - (b & 0xf) - c < 0)
#define SUB_OV_(x, y) (((int8_t)((x) ^ (y)) < 0) && ((int8_t)((y) ^ ((x) - (y))) >= 0))
#define SUB_OV  (SUB_OV_(a, (b + c)))
#define ADD(rhs)   ARITH(+, rhs, 0, ADD_CY, ADD_AC, ADD_OV)
#define ADDC(rhs)  ARITH(+, rhs, 1, ADD_CY, ADD_AC, ADD_OV)
#define SUB(rhs)   ARITH(-, rhs, 0, SUB_CY, SUB_AC, SUB_OV)
#define SUBC(rhs)  ARITH(-, rhs, 1, SUB_CY, SUB_AC, SUB_OV)
#define BR_DEC(a) do { const uint16_t addr_ = (uint16_t)(a); const uint8_t val_ = (uint8_t)(READ_LATCH(addr_) - 1); \
                       WRITE(addr_, val_); BR(val_ != 0, op->relative8); } while (0)
#define BR_CMP(v1e, cmp, v2e, off) do { const uint8_t v1_ = (uint8_t)(v1e); const uint8_t v2_ = (uint8_t)(v2e); \
                       PSW_SET(PSW_CY, v1_ < v2_); BR(v1_ cmp v2_, off); } while (0)

    switch (in->opcode) {
    default:
        v->unknown_ops++;
        break;
    case VMU_OP_NOP:
        break;
    case VMU_OP_BR:
        PC = (uint16_t)(PC + op->relative8);
        break;
    case VMU_OP_LD:
        WRITE(SFR_ACC, READ(op->direct));
        break;
    case VMU_OP_LD_IND:
        WRITE(SFR_ACC, READ(INDIRECT()));
        break;
    case VMU_OP_CALL:
        PUSH_PC();
        PC = (uint16_t)((PC & ~0xfffU) | (op->absolute & 0xfffU));
        break;
    case VMU_OP_CALLR:
        PUSH_PC();
        PC = (uint16_t)(PC + op->relative16 - 1);
        break;
    case VMU_OP_BRF:
        PC = (uint16_t)(PC + op->relative16 - 1);
        break;
    case VMU_OP_ST:
        WRITE(op->direct, VIEW(SFR_ACC));
        break;
    case VMU_OP_ST_IND:
        WRITE(INDIRECT(), VIEW(SFR_ACC));
        break;
    case VMU_OP_CALLF:
        PUSH_PC();
        PC = op->absolute;
        break;
    case VMU_OP_JMPF:
        PC = op->absolute;
        break;
    case VMU_OP_MOV:
        WRITE(op->direct, op->immediate);
        break;
    case VMU_OP_MOV_IND:
        WRITE(INDIRECT(), op->immediate);
        break;
    case VMU_OP_JMP: PC = (uint16_t)((PC & ~0xfffU) | (op->absolute & 0xfffU)); break;
    case VMU_OP_MUL: {
        const int t = (READ(SFR_C) | (READ(SFR_ACC) << 8)) * READ(SFR_B);
        WRITE(SFR_C,   t & 0xff);
        WRITE(SFR_ACC, (t >> 8) & 0xff);
        WRITE(SFR_B,   (t >> 16) & 0xff);
        PSW_SET(PSW_CY, 0);
        PSW_SET(PSW_OV, t > 65535);
        break;
    }
    case VMU_OP_BEI:
        BR_CMP(READ(SFR_ACC), ==, op->immediate, op->relative8);
        break;
    case VMU_OP_BE:
        BR_CMP(READ(SFR_ACC), ==, READ(op->direct), op->relative8);
        break;
    case VMU_OP_BE_IND:
        BR_CMP(READ(INDIRECT()), ==, op->immediate, op->relative8);
        break;
    case VMU_OP_DIV: {
        const int divisor = READ(SFR_B);
        if (divisor) {
            const int x = READ(SFR_C) | (READ(SFR_ACC) << 8);
            const int q = x / divisor;
            WRITE(SFR_B,   x % divisor);
            WRITE(SFR_C,   q & 0xff);
            WRITE(SFR_ACC, (q >> 8) & 0xff);
        } else {
            WRITE(SFR_ACC, 0xff);
        }
        PSW_SET(PSW_CY, 0);
        PSW_SET(PSW_OV, !divisor);
        break;
    }
    case VMU_OP_BNEI:
        BR_CMP(READ(SFR_ACC), !=, op->immediate, op->relative8);
        break;
    case VMU_OP_BNE:
        BR_CMP(READ(SFR_ACC), !=, READ(op->direct), op->relative8);
        break;
    case VMU_OP_BNE_IND:
        BR_CMP(READ(INDIRECT()), !=, op->immediate, op->relative8);
        break;
    case VMU_OP_BPC: {
        const uint8_t val = READ_LATCH(op->direct);
        const uint8_t mask = (uint8_t)(1U << op->bit);
        if (val & mask) {
            WRITE(op->direct, val & ~mask);
            PC = (uint16_t)(PC + op->relative8);
        }
        break;
    }
    case VMU_OP_LDF: {
        const uint32_t a = ((uint32_t)(READ(SFR_FPR) & FPR_ADDR) << 16) | ((uint32_t)READ(SFR_TRH) << 8) | READ(SFR_TRL);
        WRITE(SFR_ACC, v->flash[a & (VMU_FLASH_SIZE - 1)]);
        break;
    }
    case VMU_OP_STF: {
        uint32_t a = ((uint32_t)READ(SFR_TRH) << 8) | READ(SFR_TRL);
        const uint8_t acc = READ(SFR_ACC);
        const uint8_t fpr = READ(SFR_FPR);
        if (READ(SFR_EXT) & 1) {
            break; // user mode: ignored
        }
        if (fpr & FPR_UNLOCK) {
            switch (v->prg_state) {
            case 0:
                if (a == 0x5555 && acc == 0xaa) { v->prg_state = 1; }
                break;
            case 1:  v->prg_state = (a == 0x2aaa && acc == 0x55) ? 2 : 0; break;
            case 2:  v->prg_state = (a == 0x5555 && acc == 0xa0) ? FLASH_PRG_STATE_COUNT : 0; break;
            default: v->prg_state = 0; break;
            }
        } else if (v->prg_state < FLASH_PRG_STATE_COUNT) {
            v->prg_state = 0;                               // write without finishing the unlock
        } else {
            a |= (uint32_t)(fpr & FPR_ADDR) << 16;
            if (v->prg_state == FLASH_PRG_STATE_COUNT && (a & 0x7f)) {
                v->prg_state = 0;                           // unaligned first write
            } else {
                v->flash[a & (VMU_FLASH_SIZE - 1)] = acc;
                if (++v->prg_state == FLASH_PRG_BYTES + FLASH_PRG_STATE_COUNT) { v->prg_state = 0; }
            }
        }
        break;
    }
    case VMU_OP_DBNZ:
        BR_DEC(op->direct);
        break;
    case VMU_OP_DBNZ_IND:
        BR_DEC(INDIRECT());
        break;
    case VMU_OP_PUSH:
        PUSH(READ(op->direct));
        break;
    case VMU_OP_INC:
        WRITE(op->direct, READ_LATCH(op->direct) + 1);
        break;
    case VMU_OP_INC_IND: {
        const uint16_t a = INDIRECT();
        WRITE(a, READ_LATCH(a) + 1);
        break;
    }
    case VMU_OP_BP: BR(READ(op->direct) & (1U << op->bit), op->relative8); break;
    case VMU_OP_POP:
        WRITE(op->direct, POP());
        break;
    case VMU_OP_DEC:
        WRITE(op->direct, READ_LATCH(op->direct) - 1);
        break;
    case VMU_OP_DEC_IND: {
        const uint16_t a = INDIRECT();
        WRITE(a, READ_LATCH(a) - 1);
        break;
    }
    case VMU_OP_BZ:
        BR(!READ(SFR_ACC), op->relative8);
        break;
    case VMU_OP_ADDI:     ADD(op->immediate); break;
    case VMU_OP_ADD:      ADD(READ(op->direct)); break;
    case VMU_OP_ADD_IND:  ADD(READ(INDIRECT())); break;
    case VMU_OP_BN: BR(!(READ(op->direct) & (1U << op->bit)), op->relative8); break;
    case VMU_OP_BNZ:
        BR(READ(SFR_ACC), op->relative8);
        break;
    case VMU_OP_ADDCI:    ADDC(op->immediate); break;
    case VMU_OP_ADDC:     ADDC(READ(op->direct)); break;
    case VMU_OP_ADDC_IND: ADDC(READ(INDIRECT())); break;
    case VMU_OP_RET:
        POP_PC();
        break;
    case VMU_OP_SUBI:     SUB(op->immediate); break;
    case VMU_OP_SUB:      SUB(READ(op->direct)); break;
    case VMU_OP_SUB_IND:  SUB(READ(INDIRECT())); break;
    case VMU_OP_NOT1: WRITE(op->direct, READ_LATCH(op->direct) ^ (1U << op->bit)); break;
    case VMU_OP_RETI:
        vmu_pic_reti(v);
        break;
    case VMU_OP_SUBCI:    SUBC(op->immediate); break;
    case VMU_OP_SUBC:     SUBC(READ(op->direct)); break;
    case VMU_OP_SUBC_IND: SUBC(READ(INDIRECT())); break;
    case VMU_OP_ROR: {
        const uint8_t x = READ(SFR_ACC);
        WRITE(SFR_ACC, ((x & 1) << 7) | (x >> 1));
        break;
    }
    case VMU_OP_LDC: {
        uint32_t a = READ(SFR_ACC);
        a += READ(SFR_TRL) | (READ(SFR_TRH) << 8);
        if (READ(SFR_EXT) == 0x00) { a += 0x10000; }
        WRITE(SFR_ACC, vmu_ext_read(v, a));
        break;
    }
    case VMU_OP_XCH: {
        const uint8_t acc = READ(SFR_ACC);
        const uint8_t mem = READ(op->direct);
        WRITE(SFR_ACC, mem);
        WRITE(op->direct, acc);
        break;
    }
    case VMU_OP_XCH_IND: {
        const uint16_t a = INDIRECT();
        const uint8_t acc = READ(SFR_ACC);
        const uint8_t mem = READ(a);
        WRITE(SFR_ACC, mem);
        WRITE(a, acc);
        break;
    }
    case VMU_OP_CLR1: WRITE(op->direct, READ_LATCH(op->direct) & ~(1U << op->bit)); break;
    case VMU_OP_RORC: {
        const unsigned x = READ(SFR_ACC);
        const uint8_t psw = READ(SFR_PSW);
        WRITE(SFR_PSW, (psw & ~PSW_CY) | ((x & 1) << 7));
        WRITE(SFR_ACC, (x >> 1) | (psw & PSW_CY));
        break;
    }
    case VMU_OP_ORI:      LOGIC(|, op->immediate); break;
    case VMU_OP_OR:       LOGIC(|, READ(op->direct)); break;
    case VMU_OP_OR_IND:   LOGIC(|, READ(INDIRECT())); break;
    case VMU_OP_ROL: {
        const uint8_t x = READ(SFR_ACC);
        WRITE(SFR_ACC, (x << 1) | ((x & 0x80) >> 7));
        break;
    }
    case VMU_OP_ANDI:     LOGIC(&, op->immediate); break;
    case VMU_OP_AND:      LOGIC(&, READ(op->direct)); break;
    case VMU_OP_AND_IND:  LOGIC(&, READ(INDIRECT())); break;
    case VMU_OP_SET1: WRITE(op->direct, READ_LATCH(op->direct) | (1U << op->bit)); break;
    case VMU_OP_ROLC: {
        const unsigned x = READ(SFR_ACC);
        const uint8_t psw = READ(SFR_PSW);
        WRITE(SFR_PSW, (psw & ~PSW_CY) | (x & 0x80));
        WRITE(SFR_ACC, (x << 1) | ((psw & PSW_CY) >> 7));
        break;
    }
    case VMU_OP_XORI:     LOGIC(^, op->immediate); break;
    case VMU_OP_XOR:      LOGIC(^, READ(op->direct)); break;
    case VMU_OP_XOR_IND:  LOGIC(^, READ(INDIRECT())); break;
    }
#undef PC
#undef READ
#undef READ_LATCH
#undef VIEW
#undef WRITE
#undef INDIRECT
#undef PUSH
#undef POP
#undef PUSH_PC
#undef POP_PC
#undef PSW_SET
#undef LOGIC
#undef BR
#undef ARITH
#undef ADD_CY
#undef ADD_AC
#undef ADD_OV
#undef SUB_CY
#undef SUB_AC
#undef SUB_OV_
#undef SUB_OV
#undef ADD
#undef ADDC
#undef SUB
#undef SUBC
#undef BR_DEC
#undef BR_CMP
}

#ifdef VMU_TRACE
void vmu_trace_hook(const vmu_t *v);
#endif

uint32_t vmu_step(vmu_t *v) {
    uint8_t bytes[3];
#ifdef VMU_TRACE
    vmu_trace_hook(v);
#endif
    bytes[0] = vmu_ext_read(v, v->pc);
    const vmu_fmt_t *f = &vmu_isa[bytes[0]];
    for (unsigned i = 1; i < f->bytes; i++) { bytes[i] = vmu_ext_read(v, v->pc + i); }

    v->cc = f->cc;
    v->opcode = bytes[0];
    vmu_isa_decode(bytes, &v->cur);
    v->pc = (uint16_t)(v->pc + f->bytes);
    vmu_execute(v, &v->cur);
    v->instructions++;
    v->cycles += v->cc;

    // Landed in the firmware with no BIOS image: service the call in C and hop back to flash.
    if (!v->ext_is_flash && !v->rom) {
        uint16_t ret = vmu_bios_call(v, v->pc);
        if (ret) {
            v->pc = ret;
            vmu_write(v, SFR_EXT, (uint8_t)(vmu_read(v, SFR_EXT) | 1));
        }
    }
    return v->cc;
}

void vmu_run_ns(vmu_t *v, uint64_t ns) {
    v->deficit_ns += (int64_t)ns;
    vmu_gamepad_poll(v);
    while (v->deficit_ns > 0) {
        const uint8_t pcon = sfrc_(v, SFR_PCON);
        if (pcon & PCON_HOLD) { v->deficit_ns = 0; break; }
        vmu_pic_update(v);
        vmu_timers_update(v, v->cc);
        uint32_t cc;
        if (pcon & PCON_HALT) { cc = 1; v->cc = 1; v->cycles++;
        } else {
            cc = vmu_step(v);
        }
        const uint32_t cost = cc * vmu_tcyc_ns(v);
        v->deficit_ns -= cost;
        vmu_lcd_advance_ns(v, cost);
    }
}

void vmu_run_us(vmu_t *v, uint32_t us) { vmu_run_ns(v, (uint64_t)us * 1000U); }

void vmu_init(vmu_t *v, uint8_t *flash, const uint8_t *rom) {
    memset(v, 0, sizeof *v);
    v->flash = flash;
    v->rom = rom;
    vmu_reset(v);
}

// libevmu's EvmuRam_reset_, emulated-BIOS branch, replayed through vmu_write so the side effects
// (XBNK remap, base timer start, LCD enable) happen the same way.
void vmu_reset(vmu_t *v) {
    uint8_t *flash = v->flash;
    const uint8_t *rom = v->rom;
    uint32_t bare = v->bare_game_bytes;
    uint32_t seq = v->fb_seq;
    uint32_t seen = v->fb_seq_seen;
    uint64_t instructions = v->instructions;
    uint64_t cycles = v->cycles;
    uint32_t irqs = v->irqs;
    uint32_t unknown = v->unknown_ops;
    uint32_t flash_writes = v->flash_writes;
    uint32_t bios_calls[VMU_BIOS_COUNT];
    memcpy(bios_calls, v->bios_calls, sizeof bios_calls);

    memset(v, 0, sizeof *v);
    v->flash = flash;
    v->rom = rom;
    v->bare_game_bytes = bare;
    v->fb_seq = seq + 1;
    v->fb_seq_seen = seen;
    v->instructions = instructions;
    v->cycles = cycles;
    v->irqs = irqs;
    v->unknown_ops = unknown;
    v->flash_writes = flash_writes;     // host accounting: a game reset must not lose the dirty edge
    memcpy(v->bios_calls, bios_calls, sizeof bios_calls);

    v->cc = 1;
    v->process_this_instr = true;
    v->t0.tscale = 256;
    v->seg[2] = v->sfr;
    v->seg[3] = v->xram[0];
    v->seg[0] = v->ram[0];
    v->seg[1] = &v->ram[0][128];

    *sfr_(v, SFR_SP)  = 0x7f;
    *sfr_(v, SFR_P3)  = 0xff;
    *sfr_(v, SFR_PSW) = PSW_RAMBK0;
    vmu_write(v, SFR_P7,    P7_P71);
    vmu_write(v, SFR_IE,    0xff);
    vmu_write(v, SFR_IP,    0x00);
    vmu_write(v, SFR_P1FCR, 0xbf);
    vmu_write(v, SFR_P3INT, 0xfd);
    vmu_write(v, SFR_ISL,   0xc0);
    vmu_write(v, SFR_VSEL,  0xfc);
    v->t0.tscale = 256;
    v->pc = 0;

    if (rom) {
        v->seg[0] = v->ram[0];
        v->seg[1] = &v->ram[0][128];
        *sfr_(v, SFR_EXT) = 0;
        v->ext_is_flash = false;
    }

    vmu_set_datetime(v, VMU_BUILD_YEAR, VMU_BUILD_MONTH, VMU_BUILD_DAY, 12, 0, 0);

    if (!rom) {
        v->seg[0] = v->ram[1];
        v->seg[1] = &v->ram[1][128];
        *sfr_(v, SFR_EXT) = 1;
        v->ext_is_flash = true;
    }
    vmu_write(v, SFR_XBNK, 2);
    vmu_write(v, XRAM_ICN_GAME, 0x10);
    vmu_write(v, SFR_P1DDR, 0xff);
    vmu_write(v, SFR_P1FCR, 0xbf);
    vmu_write(v, SFR_P3INT, 0xfd);
    vmu_write(v, SFR_P3DDR, 0x00);
    vmu_write(v, SFR_ISL,   0xc0);
    vmu_write(v, SFR_VSEL,  0xfc);
    vmu_write(v, SFR_BTCR,  0x41);
    vmu_write(v, SFR_IE,    0xff);
    vmu_write(v, SFR_IP,    0x00);
    vmu_write(v, SFR_OCR,   OCR_OCR7 | OCR_OCR5 | OCR_OCR1 | OCR_OCR0);
    *sfr_(v, SFR_P7) = P7_P71;
    vmu_write(v, SFR_XBNK, 0);
    vmu_write(v, SFR_VCCR, VCCR_VCCR7);
    vmu_write(v, SFR_MCR,  MCR_MCR3);
    vmu_write(v, SFR_PCON, 0);
}
