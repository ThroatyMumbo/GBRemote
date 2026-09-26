// Transcribed from libevmu's evmu_isa.c (MIT, Falco Girgis); see LICENSE-libevmu.
#include "vmu_isa.h"
#include <string.h>

#define F(mn, op, a1, a2, a3, by, c) { mn, op, { a1, a2, a3 }, by, c }
#define N VMU_ARG_NONE

const vmu_fmt_t vmu_isa[256] = {
    [0x00]        = F("NOP",            VMU_OP_NOP,      N,            N,           N,          1, 1),
    [0x01]        = F("BR r8",          VMU_OP_BR,       VMU_ARG_R8,   N,           N,          2, 2),
    [0x02 ... 0x03] = F("LD d9",        VMU_OP_LD,       VMU_ARG_D9,   N,           N,          2, 1),
    [0x04 ... 0x07] = F("LD @Ri",       VMU_OP_LD_IND,   VMU_ARG_IND2, N,           N,          1, 1),
    [0x08 ... 0x0f] = F("CALL a12",     VMU_OP_CALL,     VMU_ARG_A12,  N,           N,          2, 2),
    [0x10]        = F("CALLR r16",      VMU_OP_CALLR,    VMU_ARG_R16,  N,           N,          3, 4),
    [0x11]        = F("BRF r16",        VMU_OP_BRF,      VMU_ARG_R16,  N,           N,          3, 4),
    [0x12 ... 0x13] = F("ST d9",        VMU_OP_ST,       VMU_ARG_D9,   N,           N,          2, 1),
    [0x14 ... 0x17] = F("ST @Ri",       VMU_OP_ST_IND,   VMU_ARG_IND2, N,           N,          1, 1),
    [0x18 ... 0x1f] = F("CALL a12",     VMU_OP_CALL,     VMU_ARG_A12,  N,           N,          2, 2),
    [0x20]        = F("CALLF a16",      VMU_OP_CALLF,    VMU_ARG_A16,  N,           N,          3, 2),
    [0x21]        = F("JMPF a16",       VMU_OP_JMPF,     VMU_ARG_A16,  N,           N,          3, 2),
    [0x22 ... 0x23] = F("MOV #i8, d9",  VMU_OP_MOV,      VMU_ARG_D9,   VMU_ARG_I8,  N,          3, 2),
    [0x24 ... 0x27] = F("MOV #i8, @Rj", VMU_OP_MOV_IND,  VMU_ARG_IND2, VMU_ARG_I8,  N,          2, 1),
    [0x28 ... 0x2f] = F("JMP a12",      VMU_OP_JMP,      VMU_ARG_A12,  N,           N,          2, 2),
    [0x30]        = F("MUL",            VMU_OP_MUL,      N,            N,           N,          1, 7),
    [0x31]        = F("BE #i8, r8",     VMU_OP_BEI,      VMU_ARG_I8,   VMU_ARG_R8,  N,          3, 2),
    [0x32 ... 0x33] = F("BE d9, r8",    VMU_OP_BE,       VMU_ARG_D9,   VMU_ARG_R8,  N,          3, 2),
    [0x34 ... 0x37] = F("BE @Rj, #i8, r8", VMU_OP_BE_IND, VMU_ARG_IND2, VMU_ARG_I8, VMU_ARG_R8, 3, 2),
    [0x38 ... 0x3f] = F("JMP a12",      VMU_OP_JMP,      VMU_ARG_A12,  N,           N,          2, 2),
    [0x40]        = F("DIV",            VMU_OP_DIV,      N,            N,           N,          1, 7),
    [0x41]        = F("BNE #i8, r8",    VMU_OP_BNEI,     VMU_ARG_I8,   VMU_ARG_R8,  N,          3, 2),
    [0x42 ... 0x43] = F("BNE d9, r8",   VMU_OP_BNE,      VMU_ARG_D9,   VMU_ARG_R8,  N,          3, 2),
    [0x44 ... 0x47] = F("BNE @Rj, #i8, r8", VMU_OP_BNE_IND, VMU_ARG_IND2, VMU_ARG_I8, VMU_ARG_R8, 3, 2),
    [0x48 ... 0x4f] = F("BPC d9, b3, r8", VMU_OP_BPC,    VMU_ARG_B3,   VMU_ARG_D9,  VMU_ARG_R8, 3, 2),
    [0x50]        = F("LDF",            VMU_OP_LDF,      N,            N,           N,          1, 2),
    [0x51]        = F("STF",            VMU_OP_STF,      N,            N,           N,          1, 2),
    [0x52 ... 0x53] = F("DBNZ d9, r8",  VMU_OP_DBNZ,     VMU_ARG_D9,   VMU_ARG_R8,  N,          3, 2),
    [0x54 ... 0x57] = F("DBNZ @Ri, r8", VMU_OP_DBNZ_IND, VMU_ARG_IND2, VMU_ARG_R8,  N,          2, 2),
    [0x58 ... 0x5f] = F("BPC d9, b3, r8", VMU_OP_BPC,    VMU_ARG_B3,   VMU_ARG_D9,  VMU_ARG_R8, 3, 2),
    [0x60 ... 0x61] = F("PUSH d9",      VMU_OP_PUSH,     VMU_ARG_D9,   N,           N,          2, 2),
    [0x62 ... 0x63] = F("INC d9",       VMU_OP_INC,      VMU_ARG_D9,   N,           N,          2, 1),
    [0x64 ... 0x67] = F("INC @Ri",      VMU_OP_INC_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0x68 ... 0x6f] = F("BP d9, b3, r8", VMU_OP_BP,      VMU_ARG_B3,   VMU_ARG_D9,  VMU_ARG_R8, 3, 2),
    [0x70 ... 0x71] = F("POP d9",       VMU_OP_POP,      VMU_ARG_D9,   N,           N,          2, 2),
    [0x72 ... 0x73] = F("DEC d9",       VMU_OP_DEC,      VMU_ARG_D9,   N,           N,          2, 1),
    [0x74 ... 0x77] = F("DEC @Ri",      VMU_OP_DEC_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0x78 ... 0x7f] = F("BP d9, b3, r8", VMU_OP_BP,      VMU_ARG_B3,   VMU_ARG_D9,  VMU_ARG_R8, 3, 2),
    [0x80]        = F("BZ r8",          VMU_OP_BZ,       VMU_ARG_R8,   N,           N,          2, 2),
    [0x81]        = F("ADD #i8",        VMU_OP_ADDI,     VMU_ARG_I8,   N,           N,          2, 1),
    [0x82 ... 0x83] = F("ADD d9",       VMU_OP_ADD,      VMU_ARG_D9,   N,           N,          2, 1),
    [0x84 ... 0x87] = F("ADD @Ri",      VMU_OP_ADD_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0x88 ... 0x8f] = F("BN d9, b3, r8", VMU_OP_BN,      VMU_ARG_B3,   VMU_ARG_D9,  VMU_ARG_R8, 3, 2),
    [0x90]        = F("BNZ r8",         VMU_OP_BNZ,      VMU_ARG_R8,   N,           N,          2, 2),
    [0x91]        = F("ADDC #i8",       VMU_OP_ADDCI,    VMU_ARG_I8,   N,           N,          2, 1),
    [0x92 ... 0x93] = F("ADDC d9",      VMU_OP_ADDC,     VMU_ARG_D9,   N,           N,          2, 1),
    [0x94 ... 0x97] = F("ADDC @Ri",     VMU_OP_ADDC_IND, VMU_ARG_IND2, N,           N,          1, 1),
    [0x98 ... 0x9f] = F("BN d9, b3, r8", VMU_OP_BN,      VMU_ARG_B3,   VMU_ARG_D9,  VMU_ARG_R8, 3, 2),
    [0xa0]        = F("RET",            VMU_OP_RET,      N,            N,           N,          1, 2),
    [0xa1]        = F("SUB #i8",        VMU_OP_SUBI,     VMU_ARG_I8,   N,           N,          2, 1),
    [0xa2 ... 0xa3] = F("SUB d9",       VMU_OP_SUB,      VMU_ARG_D9,   N,           N,          2, 1),
    [0xa4 ... 0xa7] = F("SUB @Ri",      VMU_OP_SUB_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0xa8 ... 0xaf] = F("NOT1 d9, b3",  VMU_OP_NOT1,     VMU_ARG_B3,   VMU_ARG_D9,  N,          2, 1),
    [0xb0]        = F("RETI",           VMU_OP_RETI,     N,            N,           N,          1, 2),
    [0xb1]        = F("SUBC #i8",       VMU_OP_SUBCI,    VMU_ARG_I8,   N,           N,          2, 1),
    [0xb2 ... 0xb3] = F("SUBC d9",      VMU_OP_SUBC,     VMU_ARG_D9,   N,           N,          2, 1),
    [0xb4 ... 0xb7] = F("SUBC @Ri",     VMU_OP_SUBC_IND, VMU_ARG_IND2, N,           N,          1, 1),
    [0xb8 ... 0xbf] = F("NOT1 d9, b3",  VMU_OP_NOT1,     VMU_ARG_B3,   VMU_ARG_D9,  N,          2, 1),
    [0xc0]        = F("ROR",            VMU_OP_ROR,      N,            N,           N,          1, 1),
    [0xc1]        = F("LDC",            VMU_OP_LDC,      N,            N,           N,          1, 2),
    [0xc2 ... 0xc3] = F("XCH d9",       VMU_OP_XCH,      VMU_ARG_D9,   N,           N,          2, 1),
    [0xc4 ... 0xc7] = F("XCH @Ri",      VMU_OP_XCH_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0xc8 ... 0xcf] = F("CLR1 d9, b3",  VMU_OP_CLR1,     VMU_ARG_B3,   VMU_ARG_D9,  N,          2, 1),
    [0xd0]        = F("RORC",           VMU_OP_RORC,     N,            N,           N,          1, 1),
    [0xd1]        = F("OR #i8",         VMU_OP_ORI,      VMU_ARG_I8,   N,           N,          2, 1),
    [0xd2 ... 0xd3] = F("OR d9",        VMU_OP_OR,       VMU_ARG_D9,   N,           N,          2, 1),
    [0xd4 ... 0xd7] = F("OR @Ri",       VMU_OP_OR_IND,   VMU_ARG_IND2, N,           N,          1, 1),
    [0xd8 ... 0xdf] = F("CLR1 d9, b3",  VMU_OP_CLR1,     VMU_ARG_B3,   VMU_ARG_D9,  N,          2, 1),
    [0xe0]        = F("ROL",            VMU_OP_ROL,      N,            N,           N,          1, 1),
    [0xe1]        = F("AND #i8",        VMU_OP_ANDI,     VMU_ARG_I8,   N,           N,          2, 1),
    [0xe2 ... 0xe3] = F("AND d9",       VMU_OP_AND,      VMU_ARG_D9,   N,           N,          2, 1),
    [0xe4 ... 0xe7] = F("AND @Ri",      VMU_OP_AND_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0xe8 ... 0xef] = F("SET1 d9, b3",  VMU_OP_SET1,     VMU_ARG_B3,   VMU_ARG_D9,  N,          2, 1),
    [0xf0]        = F("ROLC",           VMU_OP_ROLC,     N,            N,           N,          1, 1),
    [0xf1]        = F("XOR #i8",        VMU_OP_XORI,     VMU_ARG_I8,   N,           N,          2, 1),
    [0xf2 ... 0xf3] = F("XOR d9",       VMU_OP_XOR,      VMU_ARG_D9,   N,           N,          2, 1),
    [0xf4 ... 0xf7] = F("XOR @Ri",      VMU_OP_XOR_IND,  VMU_ARG_IND2, N,           N,          1, 1),
    [0xf8 ... 0xff] = F("SET1 d9, b3",  VMU_OP_SET1,     VMU_ARG_B3,   VMU_ARG_D9,  N,          2, 1),
};

#undef F
#undef N

// Operands are peeled off the low end of the big-endian instruction word, last argument first,
// exactly as libevmu's EvmuIsa_decode does. Bit instructions carry d8 in opcode bit 4.
void vmu_isa_decode(const uint8_t *bytes, vmu_instr_t *out) {
    const vmu_fmt_t *f = &vmu_isa[bytes[0]];
    memset(out, 0, sizeof *out);
    out->opcode = f->opcode;
    if (f->arg[0] == VMU_ARG_NONE) { return; }

    uint32_t code = 0;
    for (uint32_t i = 0; i < f->bytes; i++) {
        code |= (uint32_t)bytes[i] << (8 * (f->bytes - 1 - i));
    }

#define TAKE(var, n)              do { out->operands.var = (code & ((1u << (n)) - 1u)); code >>= (n); } while (0)
#define TAKE_OR(var, n, src, dst) do { out->operands.var |= (code & (((1u << (n)) - 1u) << (src))) << (dst); code >>= (n); } while (0)

    if (f->arg[0] == VMU_ARG_B3 && f->arg[1] == VMU_ARG_D9) {
        if (f->arg[2] == VMU_ARG_R8) { TAKE(relative8, 8); }
        TAKE(direct, 8);
        TAKE(bit, 3);
        TAKE_OR(direct, 1, 1, 7);
        return;
    }
    for (int a = 2; a >= 0; a--) {
        switch (f->arg[a]) {
        case VMU_ARG_R8:   TAKE(relative8, 8); break;
        case VMU_ARG_R16:  out->operands.relative16 = (uint16_t)((code & 0xff) << 8); code >>= 8;
                           TAKE_OR(relative16, 8, 0, 0); break;
        case VMU_ARG_I8:   TAKE(immediate, 8); break;
        case VMU_ARG_D9:   TAKE(direct, 9); break;
        case VMU_ARG_IND2: TAKE(indirect, 2); break;
        case VMU_ARG_A12:  TAKE(absolute, 11); TAKE_OR(absolute, 1, 1, 10); break;
        case VMU_ARG_A16:  TAKE(absolute, 16); break;
        case VMU_ARG_B3:   TAKE(bit, 3); break;
        default: break;
        }
    }
#undef TAKE
#undef TAKE_OR
}
