// LC86K instruction set: opcodes, format table and decoder. Transcribed from libevmu's
// evmu_isa.h / evmu_isa.c (MIT, Falco Girgis); see LICENSE-libevmu.
#pragma once
#include <stdint.h>

enum {
    VMU_OP_NOP      = 0x00, VMU_OP_BR       = 0x01, VMU_OP_LD       = 0x02, VMU_OP_LD_IND   = 0x04,
    VMU_OP_CALL     = 0x08, VMU_OP_CALLR    = 0x10, VMU_OP_BRF      = 0x11, VMU_OP_ST       = 0x12,
    VMU_OP_ST_IND   = 0x14, VMU_OP_CALLF    = 0x20, VMU_OP_JMPF     = 0x21, VMU_OP_MOV      = 0x22,
    VMU_OP_MOV_IND  = 0x24, VMU_OP_JMP      = 0x28, VMU_OP_MUL      = 0x30, VMU_OP_BEI      = 0x31,
    VMU_OP_BE       = 0x32, VMU_OP_BE_IND   = 0x34, VMU_OP_DIV      = 0x40, VMU_OP_BNEI     = 0x41,
    VMU_OP_BNE      = 0x42, VMU_OP_BNE_IND  = 0x44, VMU_OP_BPC      = 0x48, VMU_OP_LDF      = 0x50,
    VMU_OP_STF      = 0x51, VMU_OP_DBNZ     = 0x52, VMU_OP_DBNZ_IND = 0x54, VMU_OP_PUSH     = 0x60,
    VMU_OP_INC      = 0x62, VMU_OP_INC_IND  = 0x64, VMU_OP_BP       = 0x68, VMU_OP_POP      = 0x70,
    VMU_OP_DEC      = 0x72, VMU_OP_DEC_IND  = 0x74, VMU_OP_BZ       = 0x80, VMU_OP_ADDI     = 0x81,
    VMU_OP_ADD      = 0x82, VMU_OP_ADD_IND  = 0x84, VMU_OP_BN       = 0x88, VMU_OP_BNZ      = 0x90,
    VMU_OP_ADDCI    = 0x91, VMU_OP_ADDC     = 0x92, VMU_OP_ADDC_IND = 0x94, VMU_OP_RET      = 0xa0,
    VMU_OP_SUBI     = 0xa1, VMU_OP_SUB      = 0xa2, VMU_OP_SUB_IND  = 0xa4, VMU_OP_NOT1     = 0xa8,
    VMU_OP_RETI     = 0xb0, VMU_OP_SUBCI    = 0xb1, VMU_OP_SUBC     = 0xb2, VMU_OP_SUBC_IND = 0xb4,
    VMU_OP_ROR      = 0xc0, VMU_OP_LDC      = 0xc1, VMU_OP_XCH      = 0xc2, VMU_OP_XCH_IND  = 0xc4,
    VMU_OP_CLR1     = 0xc8, VMU_OP_RORC     = 0xd0, VMU_OP_ORI      = 0xd1, VMU_OP_OR       = 0xd2,
    VMU_OP_OR_IND   = 0xd4, VMU_OP_ROL      = 0xe0, VMU_OP_ANDI     = 0xe1, VMU_OP_AND      = 0xe2,
    VMU_OP_AND_IND  = 0xe4, VMU_OP_SET1     = 0xe8, VMU_OP_ROLC     = 0xf0, VMU_OP_XORI     = 0xf1,
    VMU_OP_XOR      = 0xf2, VMU_OP_XOR_IND  = 0xf4,
};

enum {
    VMU_ARG_NONE = 0, VMU_ARG_R8, VMU_ARG_R16, VMU_ARG_I8, VMU_ARG_D9,
    VMU_ARG_IND2, VMU_ARG_A12, VMU_ARG_A16, VMU_ARG_B3,
};

typedef struct vmu_fmt {
    const char *mnemonic;
    uint8_t     opcode;
    uint8_t     arg[3];
    uint8_t     bytes;
    uint8_t     cc;
} vmu_fmt_t;

// Field layout mirrors libevmu's EvmuDecodedInstruction so its test cases port unchanged.
typedef struct vmu_operands {
    union { uint16_t absolute, relative16, direct; };
    uint8_t bit;
    uint8_t indirect;
    int8_t  relative8;
    uint8_t immediate;
} vmu_operands_t;

typedef struct vmu_instr {
    vmu_operands_t operands;
    uint8_t        opcode;
} vmu_instr_t;

extern const vmu_fmt_t vmu_isa[256];

// bytes[] must hold vmu_isa[bytes[0]].bytes valid bytes.
void vmu_isa_decode(const uint8_t *bytes, vmu_instr_t *out);
