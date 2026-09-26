// libevmu's evmu_cpu_test_suite.c ported onto the flat core. Cases run in upstream registration
// order on one shared device: `call` leaves 0xbabe on the stack for `ret` to find, and so on.
#include "t.h"

static uint8_t g_flash[VMU_FLASH_SIZE + VMU_FLASH_SLACK];
static uint8_t g_rom[VMU_ROM_SIZE];
static vmu_t   g_vmu;

static void clear_psw_flags(tctx *T) {
    WR(SFR_PSW, RD(SFR_PSW) & ~(PSW_CY | PSW_AC | PSW_OV));
}

static void psw_flags(tctx *T, int cy, int ac, int ov) {
    uint8_t psw = RD(SFR_PSW);
    CMP((psw & PSW_CY) >> 7, cy);
    CMP((psw & PSW_AC) >> 6, ac);
    CMP((psw & PSW_OV) >> 2, ov);
}

T_CASE(nop)   { EXEC(.opcode = VMU_OP_NOP); }
T_CASE(ld)    { WR(0x2, 27); EXEC(.opcode = VMU_OP_LD, .operands = { .direct = 0x2 }); CMP(RD(SFR_ACC), 27); }
T_CASE(ldInd) { uint16_t ind = IND(3); WR(ind, 0xab); EXEC(.opcode = VMU_OP_LD_IND, .operands = { .indirect = 3 }); CMP(RD(SFR_ACC), 0xab); }
T_CASE(st)    { WR(SFR_ACC, 128); EXEC(.opcode = VMU_OP_ST, .operands = { .direct = 3 }); CMP(RD(3), 128); }
T_CASE(stInd) { WR(SFR_ACC, 129); EXEC(.opcode = VMU_OP_ST_IND, .operands = { .indirect = 2 }); CMP(RD(IND(2)), 129); }
T_CASE(mov)   { EXEC(.opcode = VMU_OP_MOV, .operands = { .direct = 4, .immediate = 255 }); CMP(RD(4), 255); }
T_CASE(movInd){ EXEC(.opcode = VMU_OP_MOV_IND, .operands = { .indirect = 3, .immediate = 245 }); CMP(RD(IND(3)), 245); }
T_CASE(push)  { EXEC(.opcode = VMU_OP_PUSH, .operands = { .direct = 3 }); CMP(STACK_AT(0), 128); CMP(STACK_DEPTH(), 1); }
T_CASE(pop)   { EXEC(.opcode = VMU_OP_POP, .operands = { .direct = 5 }); CMP(vmu_view(V, 5), 128); CMP(STACK_DEPTH(), 0); }
T_CASE(br)    { uint16_t pc = PC(); EXEC(.opcode = VMU_OP_BR, .operands = { .relative8 = 5 }); CMP(PC(), pc + 5); }
T_CASE(brf)   { uint16_t pc = PC(); EXEC(.opcode = VMU_OP_BRF, .operands = { .relative16 = 0x10ab }); CMP(PC(), (uint16_t)(pc + 0x10ab - 1)); }
T_CASE(jmp)   { EXEC(.opcode = VMU_OP_JMP, .operands = { .absolute = 0xabc }); CMP(PC(), 0x1abc); }
T_CASE(jmpf)  { EXEC(.opcode = VMU_OP_JMPF, .operands = { .absolute = 0xabc }); CMP(PC(), 0xabc); }

T_CASE(call) {
    V->pc = 0xbabe;
    EXEC(.opcode = VMU_OP_CALL, .operands = { .absolute = 0xdead });
    CMP(STACK_DEPTH(), 2);
    CMP(STACK_AT(1), 0xbe);
    CMP(STACK_AT(0), 0xba);
    CMP(PC(), 0xbead);
}

T_CASE(callr) {
    uint16_t pc = PC();
    EXEC(.opcode = VMU_OP_CALLR, .operands = { .relative16 = 0x1f1 });
    pc = (uint16_t)(pc + 0x1f1 - 1);
    CMP(STACK_DEPTH(), 4);
    CMP(STACK_AT(1), 0xad);
    CMP(STACK_AT(0), 0xbe);
    CMP(PC(), pc);
}

T_CASE(callf) {
    uint16_t pc = PC();
    EXEC(.opcode = VMU_OP_CALLF, .operands = { .absolute = 0x00a });
    CMP(STACK_DEPTH(), 6);
    CMP(STACK_AT(1), pc & 0xff);
    CMP(STACK_AT(0), pc >> 8);
    CMP(PC(), 0x00a);
}

T_CASE(ret) {
    EXEC(.opcode = VMU_OP_RET); CMP(STACK_DEPTH(), 4);
    EXEC(.opcode = VMU_OP_RET); CMP(STACK_DEPTH(), 2); CMP(PC(), 0xbead);
    EXEC(.opcode = VMU_OP_RET); CMP(STACK_DEPTH(), 0); CMP(PC(), 0xbabe);
}

T_CASE(bei) {
    WR(SFR_PSW, RD(SFR_PSW) & ~PSW_CY);
    WR(SFR_ACC, 44);
    EXEC(.opcode = VMU_OP_BEI, .operands = { .immediate = 44, .relative8 = -17 });
    CMP(PC(), 0xbabe - 17);
    VFY(!(RD(SFR_PSW) & PSW_CY));
    WR(SFR_ACC, 33);
    EXEC(.opcode = VMU_OP_BEI, .operands = { .immediate = 44, .relative8 = -17 });
    VFY(RD(SFR_PSW) & PSW_CY);
    CMP(PC(), 0xbabe - 17);
}

T_CASE(be) {
    WR(SFR_PSW, RD(SFR_PSW) & ~PSW_CY);
    WR(0xad, 80); WR(SFR_ACC, 80);
    uint16_t pc = PC();
    EXEC(.opcode = VMU_OP_BE, .operands = { .direct = 0xad, .relative8 = 22 });
    CMP(PC(), pc + 22);
    VFY(!(RD(SFR_PSW) & PSW_CY));
    WR(SFR_ACC, 60);
    EXEC(.opcode = VMU_OP_BE, .operands = { .direct = 0xad, .relative8 = 22 });
    VFY(RD(SFR_PSW) & PSW_CY);
    CMP(PC(), pc + 22);
}

T_CASE(beInd) {
    WR(SFR_PSW, RD(SFR_PSW) & ~PSW_CY);
    WR(IND(3), 77);
    uint16_t pc = PC();
    EXEC(.opcode = VMU_OP_BE_IND, .operands = { .indirect = 3, .immediate = 77, .relative8 = -128 });
    CMP(PC(), pc - 128);
    VFY(!(RD(SFR_PSW) & PSW_CY));
    EXEC(.opcode = VMU_OP_BE_IND, .operands = { .indirect = 3, .immediate = 78, .relative8 = -128 });
    VFY(RD(SFR_PSW) & PSW_CY);
    CMP(PC(), pc - 128);
}

T_CASE(bnei) {
    uint16_t pc = PC();
    WR(SFR_PSW, RD(SFR_PSW) & ~PSW_CY);
    WR(SFR_ACC, 43);
    EXEC(.opcode = VMU_OP_BNEI, .operands = { .immediate = 44, .relative8 = -17 });
    CMP(PC(), pc - 17);
    VFY(RD(SFR_PSW) & PSW_CY);
    WR(SFR_ACC, 44);
    EXEC(.opcode = VMU_OP_BNEI, .operands = { .immediate = 44, .relative8 = -17 });
    VFY(!(RD(SFR_PSW) & PSW_CY));
    CMP(PC(), pc - 17);
}

T_CASE(bne) {
    WR(SFR_PSW, RD(SFR_PSW) & ~PSW_CY);
    WR(0xad, 80); WR(SFR_ACC, 79);
    uint16_t pc = PC();
    EXEC(.opcode = VMU_OP_BNE, .operands = { .direct = 0xad, .relative8 = 22 });
    CMP(PC(), pc + 22);
    VFY(RD(SFR_PSW) & PSW_CY);
    WR(SFR_ACC, 80);
    EXEC(.opcode = VMU_OP_BNE, .operands = { .direct = 0xad, .relative8 = 22 });
    CMP(PC(), pc + 22);
    VFY(!(RD(SFR_PSW) & PSW_CY));
}

T_CASE(bneInd) {
    WR(SFR_PSW, RD(SFR_PSW) & ~PSW_CY);
    WR(IND(3), 76);
    uint16_t pc = PC();
    EXEC(.opcode = VMU_OP_BNE_IND, .operands = { .indirect = 3, .immediate = 77, .relative8 = -128 });
    CMP(PC(), pc - 128);
    VFY(RD(SFR_PSW) & PSW_CY);
    EXEC(.opcode = VMU_OP_BNE_IND, .operands = { .indirect = 3, .immediate = 76, .relative8 = -128 });
    CMP(PC(), pc - 128);
    VFY(!(RD(SFR_PSW) & PSW_CY));
}

T_CASE(bp) {
    uint16_t pc = PC();
    WR(0x3, 0xf8);
    EXEC(.opcode = VMU_OP_BP, .operands = { .direct = 0x3, .bit = 7, .relative8 = 127 });
    CMP(PC(), pc + 127);
    EXEC(.opcode = VMU_OP_BP, .operands = { .direct = 0x3, .bit = 1, .relative8 = (int8_t)128 });
    CMP(PC(), pc + 127);
}

T_CASE(bpc) {
    uint16_t pc = PC();
    WR(0x3, 0xff);
    EXEC(.opcode = VMU_OP_BPC, .operands = { .direct = 0x3, .bit = 7, .relative8 = 127 });
    CMP(PC(), pc + 127);
    CMP(RD(0x3), 0x7f);
    EXEC(.opcode = VMU_OP_BPC, .operands = { .direct = 0x3, .bit = 7, .relative8 = (int8_t)128 });
    CMP(PC(), pc + 127);
    CMP(RD(0x3), 0x7f);
}

T_CASE(bn) {
    uint16_t pc = PC();
    WR(0x3, 0x7f);
    EXEC(.opcode = VMU_OP_BN, .operands = { .direct = 0x3, .bit = 7, .relative8 = 127 });
    CMP(PC(), pc + 127);
    EXEC(.opcode = VMU_OP_BN, .operands = { .direct = 0x3, .bit = 6, .relative8 = (int8_t)128 });
    CMP(PC(), pc + 127);
}

T_CASE(bz) {
    uint16_t pc = PC();
    WR(SFR_ACC, 0x0);
    EXEC(.opcode = VMU_OP_BZ, .operands = { .relative8 = 127 });
    CMP(PC(), pc + 127);
    WR(SFR_ACC, 0x1);
    EXEC(.opcode = VMU_OP_BZ, .operands = { .direct = 0x3, .bit = 6, .relative8 = (int8_t)128 });
    CMP(PC(), pc + 127);
}

T_CASE(dbnz) {
    uint16_t pc = PC();
    WR(0x3, 0x2);
    EXEC(.opcode = VMU_OP_DBNZ, .operands = { .direct = 0x3, .relative8 = 11 });
    CMP(PC(), pc + 11);
    EXEC(.opcode = VMU_OP_DBNZ, .operands = { .direct = 0x3, .relative8 = 11 });
    CMP(PC(), pc + 11);
}

T_CASE(dbnzInd) {
    uint16_t pc = PC();
    WR(IND(2), 0x2);
    EXEC(.opcode = VMU_OP_DBNZ_IND, .operands = { .indirect = 2, .relative8 = 12 });
    CMP(PC(), pc + 12);
    EXEC(.opcode = VMU_OP_DBNZ_IND, .operands = { .indirect = 2, .relative8 = 12 });
    CMP(PC(), pc + 12);
}

// The four arithmetic steps every add/sub variant walks: no flags, AC, AC+OV, CY+OV.
#define ADD_STEPS(EXEC_STEP, SET_RHS)                                                    \
    do {                                                                                 \
        WR(SFR_ACC, 0x55);                                                               \
        SET_RHS(0x13); EXEC_STEP; CMP(RD(SFR_ACC), 0x68); T_CALL(psw_flags(T, 0, 0, 0)); \
        SET_RHS(0x0a); EXEC_STEP; CMP(RD(SFR_ACC), 0x72); T_CALL(psw_flags(T, 0, 1, 0)); \
        SET_RHS(0x0f); EXEC_STEP; CMP(RD(SFR_ACC), 0x81); T_CALL(psw_flags(T, 0, 1, 1)); \
        SET_RHS(0x80); EXEC_STEP; CMP(RD(SFR_ACC), 0x01); T_CALL(psw_flags(T, 1, 0, 1)); \
    } while (0)
#define SUB_STEPS(EXEC_STEP, SET_RHS)                                                    \
    do {                                                                                 \
        T_CALL(clear_psw_flags(T));                                                      \
        WR(SFR_ACC, 0x55);                                                               \
        SET_RHS(0x0c); EXEC_STEP; CMP(RD(SFR_ACC), 0x49); T_CALL(psw_flags(T, 0, 1, 0)); \
        SET_RHS(0x68); EXEC_STEP; CMP(RD(SFR_ACC), 0xe1); T_CALL(psw_flags(T, 1, 0, 0)); \
        T_CALL(clear_psw_flags(T));                                                      \
        WR(SFR_ACC, 0x80);                                                               \
        SET_RHS(0x02); EXEC_STEP; CMP(RD(SFR_ACC), 0x7e); T_CALL(psw_flags(T, 0, 1, 1)); \
        SET_RHS(0x95); EXEC_STEP; CMP(RD(SFR_ACC), 0xe9); T_CALL(psw_flags(T, 1, 0, 1)); \
    } while (0)

static uint8_t g_imm;
#define SET_IMM(x)   (g_imm = (x))
#define SET_D68(x)   WR(0x68, (x))
#define SET_IND1(x)  WR(IND(1), (x))

T_CASE(addi)    { ADD_STEPS(EXEC(.opcode = VMU_OP_ADDI, .operands = { .immediate = g_imm }), SET_IMM); }
T_CASE(add)     { ADD_STEPS(EXEC(.opcode = VMU_OP_ADD, .operands = { .direct = 0x68 }), SET_D68); }
T_CASE(addInd)  { ADD_STEPS(EXEC(.opcode = VMU_OP_ADD_IND, .operands = { .indirect = 1 }), SET_IND1); }

T_CASE(addci) {
    T_CALL(clear_psw_flags(T));
    ADD_STEPS(EXEC(.opcode = VMU_OP_ADDCI, .operands = { .immediate = g_imm }), SET_IMM);
    EXEC(.opcode = VMU_OP_ADDCI, .operands = { .immediate = 0x1 });
    CMP(RD(SFR_ACC), 0x3); T_CALL(psw_flags(T, 0, 0, 0));
}
T_CASE(addc) {
    T_CALL(clear_psw_flags(T));
    ADD_STEPS(EXEC(.opcode = VMU_OP_ADDC, .operands = { .direct = 0x68 }), SET_D68);
    WR(0x68, 0x1);
    EXEC(.opcode = VMU_OP_ADDC, .operands = { .direct = 0x68 });
    CMP(RD(SFR_ACC), 0x3); T_CALL(psw_flags(T, 0, 0, 0));
}
T_CASE(addcInd) {
    ADD_STEPS(EXEC(.opcode = VMU_OP_ADDC_IND, .operands = { .indirect = 1 }), SET_IND1);
    WR(IND(1), 0x1);
    EXEC(.opcode = VMU_OP_ADDC_IND, .operands = { .indirect = 1 });
    CMP(RD(SFR_ACC), 0x3); T_CALL(psw_flags(T, 0, 0, 0));
}

T_CASE(subi)    { SUB_STEPS(EXEC(.opcode = VMU_OP_SUBI, .operands = { .immediate = g_imm }), SET_IMM); }
T_CASE(sub)     { SUB_STEPS(EXEC(.opcode = VMU_OP_SUB, .operands = { .direct = 0x68 }), SET_D68); }
T_CASE(subInd)  { SUB_STEPS(EXEC(.opcode = VMU_OP_SUB_IND, .operands = { .indirect = 1 }), SET_IND1); }
T_CASE(subci)   { SUB_STEPS(EXEC(.opcode = VMU_OP_SUBCI, .operands = { .immediate = g_imm }), SET_IMM); }
T_CASE(subc)    { SUB_STEPS(EXEC(.opcode = VMU_OP_SUBC, .operands = { .direct = 0x68 }), SET_D68); }
T_CASE(subcInd) { SUB_STEPS(EXEC(.opcode = VMU_OP_SUBC_IND, .operands = { .indirect = 1 }), SET_IND1); }

T_CASE(mul) {
    WR(SFR_PSW, 0xc4); WR(SFR_ACC, 0x11); WR(SFR_C, 0x23); WR(SFR_B, 0x52);
    EXEC(.opcode = VMU_OP_MUL);
    CMP(RD(SFR_ACC), 0x7d); CMP(RD(SFR_C), 0x36); CMP(RD(SFR_B), 0x5);
    T_CALL(psw_flags(T, 0, 1, 1));
    WR(SFR_PSW, 0xc4); WR(SFR_ACC, 0x7); WR(SFR_C, 0x5); WR(SFR_B, 0x10);
    EXEC(.opcode = VMU_OP_MUL);
    CMP(RD(SFR_ACC), 0x70); CMP(RD(SFR_C), 0x50); CMP(RD(SFR_B), 0x00);
    T_CALL(psw_flags(T, 0, 1, 0));
}

T_CASE(div) {
    WR(SFR_PSW, 0xc4); WR(SFR_ACC, 0x79); WR(SFR_C, 0x5); WR(SFR_B, 0x7);
    EXEC(.opcode = VMU_OP_DIV);
    CMP(RD(SFR_ACC), 0x11); CMP(RD(SFR_C), 0x49); CMP(RD(SFR_B), 0x6);
    T_CALL(psw_flags(T, 0, 1, 0));
    WR(SFR_PSW, 0xc0); WR(SFR_ACC, 0x0); WR(SFR_C, 0x10); WR(SFR_B, 0x4);
    EXEC(.opcode = VMU_OP_DIV);
    CMP(RD(SFR_ACC), 0x0); CMP(RD(SFR_C), 0x4); CMP(RD(SFR_B), 0x0);
    T_CALL(psw_flags(T, 0, 1, 0));
    WR(SFR_PSW, 0xc0); WR(SFR_ACC, 0x7); WR(SFR_C, 0x10); WR(SFR_B, 0x0);
    EXEC(.opcode = VMU_OP_DIV);
    CMP(RD(SFR_ACC), 0xff); CMP(RD(SFR_C), 0x10); CMP(RD(SFR_B), 0x00);
    T_CALL(psw_flags(T, 0, 1, 1));
}

#define LOGIC4(EXEC_STEP, SET_RHS, start, a, ra, b, rb, c, rc, d, rd)  \
    do {                                                               \
        WR(SFR_ACC, start);                                            \
        SET_RHS(a); EXEC_STEP; CMP(RD(SFR_ACC), ra);                   \
        SET_RHS(b); EXEC_STEP; CMP(RD(SFR_ACC), rb);                   \
        SET_RHS(c); EXEC_STEP; CMP(RD(SFR_ACC), rc);                   \
        SET_RHS(d); EXEC_STEP; CMP(RD(SFR_ACC), rd);                   \
    } while (0)
#define SET_D23(x)  WR(0x23, (x))

T_CASE(andi)   { WR(SFR_ACC, 0xff); EXEC(.opcode = VMU_OP_ANDI, .operands = { .immediate = 0x55 }); CMP(RD(SFR_ACC), 0x55);
                 EXEC(.opcode = VMU_OP_ANDI, .operands = { .immediate = 0xaa }); CMP(RD(SFR_ACC), 0x00); }
T_CASE(and)    { WR(SFR_ACC, 0xff); WR(0x23, 0x55); EXEC(.opcode = VMU_OP_AND, .operands = { .direct = 0x23 }); CMP(RD(SFR_ACC), 0x55);
                 WR(0x23, 0xaa); EXEC(.opcode = VMU_OP_AND, .operands = { .direct = 0x23 }); CMP(RD(SFR_ACC), 0x00); }
T_CASE(andInd) { WR(SFR_ACC, 0xff); WR(IND(1), 0x55); EXEC(.opcode = VMU_OP_AND_IND, .operands = { .indirect = 1 }); CMP(RD(SFR_ACC), 0x55);
                 WR(IND(1), 0xaa); EXEC(.opcode = VMU_OP_AND_IND, .operands = { .indirect = 1 }); CMP(RD(SFR_ACC), 0x00); }
T_CASE(ori)    { LOGIC4(EXEC(.opcode = VMU_OP_ORI, .operands = { .immediate = g_imm }), SET_IMM, 0, 0x3, 0x3, 0xc, 0xf, 0x30, 0x3f, 0xc0, 0xff); }
T_CASE(or)     { LOGIC4(EXEC(.opcode = VMU_OP_OR, .operands = { .direct = 0x23 }), SET_D23, 0, 0x3, 0x3, 0xc, 0xf, 0x30, 0x3f, 0xc0, 0xff); }
T_CASE(orInd)  { LOGIC4(EXEC(.opcode = VMU_OP_OR_IND, .operands = { .indirect = 1 }), SET_IND1, 0, 0x3, 0x3, 0xc, 0xf, 0x30, 0x3f, 0xc0, 0xff); }
T_CASE(xori)   { LOGIC4(EXEC(.opcode = VMU_OP_XORI, .operands = { .immediate = g_imm }), SET_IMM, 0, 0xf, 0xf, 0xf0, 0xff, 0xf, 0xf0, 0xf0, 0x00); }
T_CASE(xor)    { LOGIC4(EXEC(.opcode = VMU_OP_XOR, .operands = { .direct = 0x23 }), SET_D23, 0, 0xf, 0xf, 0xf0, 0xff, 0xf, 0xf0, 0xf0, 0x00); }
T_CASE(xorInd) { LOGIC4(EXEC(.opcode = VMU_OP_XOR_IND, .operands = { .indirect = 1 }), SET_IND1, 0, 0xf, 0xf, 0xf0, 0xff, 0xf, 0xf0, 0xf0, 0x00); }

T_CASE(rol)  { WR(SFR_ACC, 0x55); EXEC(.opcode = VMU_OP_ROL); CMP(RD(SFR_ACC), 0xaa); EXEC(.opcode = VMU_OP_ROL); CMP(RD(SFR_ACC), 0x55); }
T_CASE(rolc) {
    clear_psw_flags(T);
    WR(SFR_ACC, 0x60);
    EXEC(.opcode = VMU_OP_ROLC); CMP(RD(SFR_ACC), 0xc0); T_CALL(psw_flags(T, 0, 0, 0));
    EXEC(.opcode = VMU_OP_ROLC); CMP(RD(SFR_ACC), 0x80); T_CALL(psw_flags(T, 1, 0, 0));
    EXEC(.opcode = VMU_OP_ROLC); CMP(RD(SFR_ACC), 0x1);  T_CALL(psw_flags(T, 1, 0, 0));
    EXEC(.opcode = VMU_OP_ROLC); CMP(RD(SFR_ACC), 0x3);  T_CALL(psw_flags(T, 0, 0, 0));
}
T_CASE(ror)  { WR(SFR_ACC, 0x1); EXEC(.opcode = VMU_OP_ROR); CMP(RD(SFR_ACC), 0x80); EXEC(.opcode = VMU_OP_ROR); CMP(RD(SFR_ACC), 0x40); }
T_CASE(rorc) {
    clear_psw_flags(T);
    WR(SFR_ACC, 0x6);
    EXEC(.opcode = VMU_OP_RORC); CMP(RD(SFR_ACC), 0x3);  T_CALL(psw_flags(T, 0, 0, 0));
    EXEC(.opcode = VMU_OP_RORC); CMP(RD(SFR_ACC), 0x1);  T_CALL(psw_flags(T, 1, 0, 0));
    EXEC(.opcode = VMU_OP_RORC); CMP(RD(SFR_ACC), 0x80); T_CALL(psw_flags(T, 1, 0, 0));
    EXEC(.opcode = VMU_OP_RORC); CMP(RD(SFR_ACC), 0xc0); T_CALL(psw_flags(T, 0, 0, 0));
}

T_CASE(inc) {
    clear_psw_flags(T);
    WR(0x23, 0x0);  EXEC(.opcode = VMU_OP_INC, .operands = { .direct = 0x23 }); CMP(RD(0x23), 0x1);
    WR(0x23, 0xf0); EXEC(.opcode = VMU_OP_INC, .operands = { .direct = 0x23 }); CMP(RD(0x23), 0xf1);
    WR(0x23, 0xff); EXEC(.opcode = VMU_OP_INC, .operands = { .direct = 0x23 }); CMP(RD(0x23), 0x0);
}
T_CASE(incInd) {
    clear_psw_flags(T);
    WR(IND(3), 0x0);  EXEC(.opcode = VMU_OP_INC_IND, .operands = { .indirect = 3 }); CMP(RD(IND(3)), 0x1);
    WR(IND(3), 0xf0); EXEC(.opcode = VMU_OP_INC_IND, .operands = { .indirect = 3 }); CMP(RD(IND(3)), 0xf1);
    WR(IND(3), 0xff); EXEC(.opcode = VMU_OP_INC_IND, .operands = { .indirect = 3 }); CMP(RD(IND(3)), 0x0);
}
T_CASE(dec) {
    clear_psw_flags(T);
    WR(0x23, 0x2);  EXEC(.opcode = VMU_OP_DEC, .operands = { .direct = 0x23 }); CMP(RD(0x23), 0x1);
    WR(0x23, 0xf0); EXEC(.opcode = VMU_OP_DEC, .operands = { .direct = 0x23 }); CMP(RD(0x23), 0xef);
    WR(0x23, 0x0);  EXEC(.opcode = VMU_OP_DEC, .operands = { .direct = 0x23 }); CMP(RD(0x23), 0xff);
}
T_CASE(decInd) {
    clear_psw_flags(T);
    WR(IND(3), 0x2);  EXEC(.opcode = VMU_OP_DEC_IND, .operands = { .indirect = 3 }); CMP(RD(IND(3)), 0x1);
    WR(IND(3), 0xf0); EXEC(.opcode = VMU_OP_DEC_IND, .operands = { .indirect = 3 }); CMP(RD(IND(3)), 0xef);
    WR(IND(3), 0x0);  EXEC(.opcode = VMU_OP_DEC_IND, .operands = { .indirect = 3 }); CMP(RD(IND(3)), 0xff);
}
T_CASE(xch)    { WR(SFR_ACC, 0x33); WR(0x23, 0xff); EXEC(.opcode = VMU_OP_XCH, .operands = { .direct = 0x23 }); CMP(RD(SFR_ACC), 0xff); CMP(RD(0x23), 0x33); }
T_CASE(xchInd) { WR(SFR_ACC, 0x33); WR(IND(1), 0xff); EXEC(.opcode = VMU_OP_XCH_IND, .operands = { .indirect = 1 }); CMP(RD(SFR_ACC), 0xff); CMP(RD(IND(1)), 0x33); }
T_CASE(clr1) {
    WR(0x23, 0x1);  EXEC(.opcode = VMU_OP_CLR1, .operands = { .bit = 0, .direct = 0x23 }); CMP(RD(0x23), 0x0);
    WR(0x23, 0x80); EXEC(.opcode = VMU_OP_CLR1, .operands = { .bit = 7, .direct = 0x23 }); CMP(RD(0x23), 0x00);
}
T_CASE(set1) {
    WR(0x23, 0x0);  EXEC(.opcode = VMU_OP_SET1, .operands = { .bit = 0, .direct = 0x23 }); CMP(RD(0x23), 0x1);
    WR(0x23, 0x00); EXEC(.opcode = VMU_OP_SET1, .operands = { .bit = 7, .direct = 0x23 }); CMP(RD(0x23), 0x80);
}
T_CASE(not1) {
    WR(0x23, 0x0);  EXEC(.opcode = VMU_OP_NOT1, .operands = { .bit = 0, .direct = 0x23 }); CMP(RD(0x23), 0x1);
    WR(0x23, 0x80); EXEC(.opcode = VMU_OP_NOT1, .operands = { .bit = 7, .direct = 0x23 }); CMP(RD(0x23), 0x0);
}

// libevmu's ROM storage is writable even with no BIOS image; here a scratch ROM stands in for it.
T_CASE(ldc) {
    WR(SFR_EXT, 0x01);
    vmu_ext_write(V, 0x123, 0x77);
    WR(SFR_TRH, 0x1); WR(SFR_TRL, 0x23); WR(SFR_ACC, 0x0);
    EXEC(.opcode = VMU_OP_LDC);
    CMP(RD(SFR_ACC), 0x77);
    V->rom = g_rom;
    WR(SFR_EXT, 0x08);
    g_rom[0x123] = 0x33;
    WR(SFR_TRH, 0x1); WR(SFR_TRL, 0x23); WR(SFR_ACC, 0x0);
    EXEC(.opcode = VMU_OP_LDC);
    CMP(RD(SFR_ACC), 0x33);
    WR(SFR_EXT, 0x01);
    V->rom = NULL;
}

T_CASE(reti) {
    vmu_irq_raise(V, IRQ_INT3_TBASE);
    vmu_pic_update(V);
    CMP(vmu_irq_depth(V), 1);
    EXEC(.opcode = VMU_OP_RETI);
    CMP(vmu_irq_depth(V), 0);
}

T_CASE(ldf) {
    V->flash[0x1abcd] = 0x89;
    WR(SFR_FPR, 0x1); WR(SFR_TRH, 0xab); WR(SFR_TRL, 0xcd);
    EXEC(.opcode = VMU_OP_LDF);
    CMP(RD(SFR_ACC), 0x89);
}

static void stf_unlock_to(tctx *T, int state) {
    uint8_t trh = RD(SFR_TRH), trl = RD(SFR_TRL), acc = RD(SFR_ACC);
    if (state >= 0) { WR(SFR_TRH, 0x55); WR(SFR_TRL, 0x55); WR(SFR_ACC, 0xaa); EXEC(.opcode = VMU_OP_STF); }
    if (state >= 1) { WR(SFR_TRH, 0x2a); WR(SFR_TRL, 0xaa); WR(SFR_ACC, 0x55); EXEC(.opcode = VMU_OP_STF); }
    if (state >= 2) { WR(SFR_TRH, 0x55); WR(SFR_TRL, 0x55); WR(SFR_ACC, 0xa0); EXEC(.opcode = VMU_OP_STF); }
    WR(SFR_TRH, trh); WR(SFR_TRL, trl); WR(SFR_ACC, acc);
}

T_CASE(stf) {
    V->flash[0x1ab00 + 129] = 0x76;
    WR(SFR_FPR, 0x1); WR(SFR_TRH, 0xab); WR(SFR_TRL, 0xcd);
    WR(SFR_EXT, 0x01);                                       // user mode: refused
    WR(SFR_ACC, 0x0);
    EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    WR(SFR_EXT, 0x08);                                       // system mode, state 0
    WR(SFR_FPR, 0x1 | FPR_UNLOCK);
    EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    stf_unlock_to(T, 0); EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    stf_unlock_to(T, 1); EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    stf_unlock_to(T, 2); EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    stf_unlock_to(T, 2); WR(SFR_FPR, 0x1);                   // unlocked, unaligned start
    EXEC(.opcode = VMU_OP_STF); CMP(V->flash[0x1ab00 + 129], 0x76);
    WR(SFR_FPR, 0x1 | FPR_UNLOCK);
    stf_unlock_to(T, 2);
    WR(SFR_FPR, 0x1);
    for (unsigned b = 0; b < 128; b++) {
        WR(SFR_TRL, b); WR(SFR_ACC, b);
        EXEC(.opcode = VMU_OP_STF);
        CMP(V->flash[0x1ab00 + b], b);
    }
    WR(SFR_TRL, 129); WR(SFR_ACC, 129);
    EXEC(.opcode = VMU_OP_STF);
    CMP(V->flash[0x1ab00 + 129], 0x76);
    WR(SFR_EXT, 0x01);
}

// libevmu feeds the timers the cycle count of the instruction currently in the CPU.
static void set_cur(tctx *T, uint8_t opcode) { V->cc = vmu_isa[opcode].cc; }
static void tick(tctx *T) { vmu_timers_update(V, V->cc); }

T_CASE(timerStartDelay16) {
    set_cur(T, VMU_OP_MOV);
    WR(SFR_T0PRR, 0xff); WR(SFR_T0LR, 0x00); WR(SFR_T0HR, 0x00);
    V->t0.tl = 0; V->t0.th = 0;
    WR(SFR_T0CNT, T0CNT_P0LONG | T0CNT_P0LRUN | T0CNT_P0HRUN);
    tick(T); CMP(RD(SFR_T0L), 0x01); CMP(RD(SFR_T0H), 0x00);
    set_cur(T, VMU_OP_NOP);
    tick(T); CMP(RD(SFR_T0L), 0x02); CMP(RD(SFR_T0H), 0x00);
    set_cur(T, VMU_OP_MOV);
    WR(SFR_T1LR, 0x00); WR(SFR_T1HR, 0x00);
    V->t1.tl = 0; V->t1.th = 0;
    WR(SFR_T1CNT, T1CNT_T1LONG | T1CNT_T1LRUN | T1CNT_T1HRUN);
    tick(T); CMP(RD(SFR_T1L), 0x01); CMP(RD(SFR_T1H), 0x00);
    set_cur(T, VMU_OP_NOP);
    tick(T); CMP(RD(SFR_T1L), 0x02); CMP(RD(SFR_T1H), 0x00);
}

T_CASE(timerCounterWriteStopped) {
    WR(SFR_T0CNT, 0x00); WR(SFR_T1CNT, 0x00);
    WR(SFR_T0L, 0x12); WR(SFR_T0H, 0x34); WR(SFR_T1L, 0x56); WR(SFR_T1H, 0x78);
    CMP(V->t0.tl, 0x12); CMP(V->t0.th, 0x34); CMP(V->t1.tl, 0x56); CMP(V->t1.th, 0x78);
    CMP(RD(SFR_T0L), 0x12); CMP(RD(SFR_T0H), 0x34); CMP(RD(SFR_T1L), 0x56); CMP(RD(SFR_T1H), 0x78);
}

T_CASE(clockTicksPerCycle) {
    const uint8_t ctl = OCR_OCR7 | OCR_OCR5 | OCR_OCR4 | OCR_OCR1 | OCR_OCR0;
    CMP(RD(SFR_OCR) & ctl, OCR_OCR7 | OCR_OCR5 | OCR_OCR1 | OCR_OCR0);
    CMP(vmu_tcyc_ns(V), 183105);
    const struct { uint8_t ocr; uint32_t ns; } cases[] = {
        { 0x00, 13648 }, { OCR_OCR7, 6824 }, { OCR_OCR5, 366210 }, { OCR_OCR5 | OCR_OCR7, 183105 },
        { OCR_OCR4, 2000 }, { OCR_OCR4 | OCR_OCR7, 1000 }, { OCR_OCR5 | OCR_OCR4, 2000 },
        { OCR_OCR5 | OCR_OCR4 | OCR_OCR7, 1000 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        WR(SFR_OCR, cases[i].ocr);
        CMP(vmu_tcyc_ns(V), cases[i].ns);
    }
}

T_CASE(clockOcrReadbackHighBits) {
    WR(SFR_OCR, OCR_OCR7 | OCR_OCR0);
    CMP(RD(SFR_OCR), 0xcd);
    CMP(RD(SFR_OCR) & (OCR_OCR7 | OCR_OCR5 | OCR_OCR4 | OCR_OCR1 | OCR_OCR0), OCR_OCR7 | OCR_OCR0);
}

static void step_base_timer(tctx *T, unsigned steps) {
    set_cur(T, VMU_OP_NOP);
    for (unsigned i = 0; i < steps; i++) tick(T);
}

static void base_timer_default_half_second_quartz(tctx *T) {
    WR(SFR_BTCR, 0); V->int_req = 0;
    WR(SFR_OCR, OCR_OCR7 | OCR_OCR5 | OCR_OCR1 | OCR_OCR0);
    WR(SFR_ISL, 0xc0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT0_EN);
    step_base_timer(T, 2730);
    CMP(V->bt.counter, 16380);
    CMP(RD(SFR_BTCR) & BTCR_INT0_SRC, 0);
    CMP(V->int_req & (1u << IRQ_INT3_TBASE), 0);
    step_base_timer(T, 1);
    CMP(V->bt.counter, 2);
    CMP(RD(SFR_BTCR) & BTCR_INT0_SRC, BTCR_INT0_SRC);
    CMP(V->int_req & (1u << IRQ_INT3_TBASE), 1u << IRQ_INT3_TBASE);
}

// The cycle-clock base timer skips the start-delay cycle of the instruction that armed it.
static void base_timer_cycle_clock_modes(tctx *T) {
    const uint8_t isl_cycle = 0xd0;
    WR(SFR_BTCR, 0); V->int_req = 0;
    WR(SFR_ISL, isl_cycle);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN);
    step_base_timer(T, 31);
    CMP(V->bt.counter, 30); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1);
    CMP(V->bt.counter, 31); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1);
    CMP(V->bt.counter, 32); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, BTCR_INT1_SRC);
    CMP(V->int_req & (1u << IRQ_INT3_TBASE), 1u << IRQ_INT3_TBASE);

    // INT1 cycle select 0x30 with INT0 cycle clear is the 0x800 period in libevmu's table.
    WR(SFR_BTCR, 0); V->int_req = 0;
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN | BTCR_INT1_CYCLE);
    step_base_timer(T, 0x7ff);
    CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1);
    CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1);
    CMP(V->bt.counter, 0x800); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, BTCR_INT1_SRC);

    WR(SFR_BTCR, 0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT0_EN | BTCR_INT0_CYCLE);
    V->int_req = 0;
    step_base_timer(T, 63);
    CMP(RD(SFR_BTCR) & BTCR_INT0_SRC, 0);
    step_base_timer(T, 1);
    CMP(RD(SFR_BTCR) & BTCR_INT0_SRC, 0);
    step_base_timer(T, 1);
    CMP(V->bt.counter, 64); CMP(RD(SFR_BTCR) & BTCR_INT0_SRC, BTCR_INT0_SRC);

    WR(SFR_BTCR, 0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN | BTCR_INT0_CYCLE | 0x20);
    V->int_req = 0;
    step_base_timer(T, 1); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1); CMP(V->bt.counter, 2); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, BTCR_INT1_SRC);

    WR(SFR_BTCR, 0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN | BTCR_INT0_CYCLE | 0x30);
    V->int_req = 0;
    step_base_timer(T, 7); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1); CMP(V->bt.counter, 8); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, BTCR_INT1_SRC);
}

static void base_timer_stop_clears_counter(tctx *T) {
    WR(SFR_BTCR, 0);
    WR(SFR_ISL, 0xd0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN);
    step_base_timer(T, 21);
    CMP(V->bt.counter, 20);
    WR(SFR_BTCR, 0);
    CMP(V->bt.counter, 0);
    step_base_timer(T, 40);
    CMP(V->bt.counter, 0);
    CMP(RD(SFR_BTCR) & (BTCR_INT0_SRC | BTCR_INT1_SRC), 0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN);
    step_base_timer(T, 33);
    CMP(V->bt.counter, 32);
    CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, BTCR_INT1_SRC);
}

static void base_timer_t0_prescaler_source(tctx *T) {
    WR(SFR_BTCR, 0);
    WR(SFR_T0PRR, 0xfd);
    WR(SFR_ISL, 0xf0);
    WR(SFR_BTCR, BTCR_OP_CTRL | BTCR_INT1_EN);
    step_base_timer(T, 95);
    CMP(V->bt.counter, 31); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, 0);
    step_base_timer(T, 1);
    CMP(V->bt.counter, 32); CMP(RD(SFR_BTCR) & BTCR_INT1_SRC, BTCR_INT1_SRC);
}

// libevmu's EvmuClock_* setters become the OCR/PCON writes they perform.
T_CASE(clockApiReflectsAndUpdatesRegisters) {
    WR(SFR_OCR, RD(SFR_OCR) | OCR_OCR1 | OCR_OCR0);
    CMP(RD(SFR_OCR) & (OCR_OCR1 | OCR_OCR0), OCR_OCR1 | OCR_OCR0);
    WR(SFR_OCR, RD(SFR_OCR) & ~(OCR_OCR1 | OCR_OCR0));
    CMP(RD(SFR_OCR) & (OCR_OCR1 | OCR_OCR0), 0);
    WR(SFR_PCON, PCON_HALT); CMP(RD(SFR_PCON), PCON_HALT);
    WR(SFR_PCON, PCON_HOLD); CMP(RD(SFR_PCON), PCON_HOLD);
    WR(SFR_PCON, 0);         CMP(RD(SFR_PCON), 0);
    WR(SFR_OCR, OCR_OCR5 | OCR_OCR7); CMP(vmu_tcyc_ns(V), 183105);
    WR(SFR_OCR, OCR_OCR4);            CMP(vmu_tcyc_ns(V), 2000);
    CMP(RD(SFR_OCR) & (OCR_OCR7 | OCR_OCR5 | OCR_OCR4), OCR_OCR4);
    WR(SFR_OCR, OCR_OCR7);            CMP(vmu_tcyc_ns(V), 6824);
    WR(SFR_OCR, OCR_OCR5);            CMP(vmu_tcyc_ns(V), 366210);
    T_CALL(base_timer_default_half_second_quartz(T));
    T_CALL(base_timer_cycle_clock_modes(T));
    T_CALL(base_timer_stop_clears_counter(T));
    T_CALL(base_timer_t0_prescaler_source(T));
}

static void set_pixel(tctx *T, unsigned x, unsigned y, bool on) {
    unsigned r = y & 15, bank = y >> 4;
    uint8_t *p = &V->xram[bank][(r >> 1) * 16 + (r & 1) * 6 + (x >> 3)];
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (on) *p |= m; else *p &= (uint8_t)~m;
}
static int frame_pixel(tctx *T, unsigned x, unsigned y) {
    uint8_t fb[VMU_LCD_BYTES];
    vmu_lcd_frame(V, fb);
    return (fb[y * 6 + (x >> 3)] >> (7 - (x & 7))) & 1;
}

T_CASE(clockHoldStopsCpuAndTimers) {
    uint16_t start_pc = PC();
    WR(SFR_T0PRR, 0xff); WR(SFR_T0LR, 0x00); WR(SFR_T0HR, 0x00);
    WR(SFR_T0CNT, T0CNT_P0LRUN | T0CNT_P0HRUN);
    V->t0.tl = 0x12; V->t0.th = 0x34;
    set_pixel(T, 0, 0, true); set_pixel(T, 1, 0, false);
    vmu_lcd_advance_ns(V, 6u * 199u * 1000u);
    VFY(frame_pixel(T, 0, 0) != frame_pixel(T, 1, 0));
    WR(SFR_PCON, PCON_HOLD);
    vmu_run_ns(V, 183105ull * 16);
    CMP(PC(), start_pc);
    CMP(V->t0.tl, 0x12); CMP(V->t0.th, 0x34);
    CMP(V->xram[0][0] >> 7, 1);
    CMP(frame_pixel(T, 0, 0), 0); CMP(frame_pixel(T, 1, 0), 0);
    CMP(RD(SFR_PCON), PCON_HOLD);
}

T_CASE(clockHoldCancelsOnPort3Input) {
    WR(SFR_PCON, PCON_HOLD);
    WR(SFR_P3INT, P3INT_P32INT);
    vmu_set_buttons(V, VMU_BTN_A);
    vmu_gamepad_poll(V);
    CMP(RD(SFR_PCON), 0);
    CMP(RD(SFR_P3INT) & P3INT_P31INT, P3INT_P31INT);
    vmu_set_buttons(V, 0);
}

T_CASE(lcdBlankWhenDisabled) {
    WR(SFR_VCCR, VCCR_VCCR7);
    set_pixel(T, 0, 0, true); set_pixel(T, 1, 0, false);
    vmu_lcd_advance_ns(V, 6u * 199u * 1000u);
    CMP(frame_pixel(T, 0, 0), 1); CMP(frame_pixel(T, 1, 0), 0);
    WR(SFR_VCCR, 0x00);
    CMP(V->xram[0][0] >> 7, 1);
    CMP(frame_pixel(T, 0, 0), 0); CMP(frame_pixel(T, 1, 0), 0);
    WR(SFR_VCCR, VCCR_VCCR7);
}

T_CASE(timer1ReloadCascade16) {
    set_cur(T, VMU_OP_MOV);
    WR(SFR_T1LR, 0xfe); WR(SFR_T1HR, 0xfe);
    V->t1.tl = 0xfe; V->t1.th = 0xfe;
    WR(SFR_T1CNT, T1CNT_T1LONG | T1CNT_T1LRUN | T1CNT_T1HRUN);
    V->t1.start_delay = 0;
    tick(T);
    CMP(RD(SFR_T1L), 0xfe); CMP(RD(SFR_T1H), 0xff);
    VFY(RD(SFR_T1CNT) & T1CNT_T1LOVF);
    VFY(!(RD(SFR_T1CNT) & T1CNT_T1HOVF));
    set_cur(T, VMU_OP_NOP);
    tick(T);
    CMP(RD(SFR_T1L), 0xff); CMP(RD(SFR_T1H), 0xff);
    tick(T);
    CMP(RD(SFR_T1L), 0xfe); CMP(RD(SFR_T1H), 0xfe);
    VFY(RD(SFR_T1CNT) & T1CNT_T1HOVF);
}

// Decoder round trip for every opcode byte, against the table's own field layout.
T_CASE(decodeAllOpcodes) {
    for (unsigned op = 0; op < 256; op++) {
        uint8_t bytes[3] = { (uint8_t)op, 0xa5, 0x3c };
        vmu_instr_t in;
        vmu_isa_decode(bytes, &in);
        CMP(in.opcode, vmu_isa[op].opcode);
    }
    uint8_t bp[3] = { 0x78 | 5, 0x42, 0xf0 };               // BP 0x142, bit 5, r8 -16
    vmu_instr_t in;
    vmu_isa_decode(bp, &in);
    CMP(in.opcode, VMU_OP_BP); CMP(in.operands.direct, 0x142); CMP(in.operands.bit, 5); CMP(in.operands.relative8, -16);
    uint8_t call[2] = { 0x18 | 3, 0x9a };                    // CALL a12 = 0xb9a
    vmu_isa_decode(call, &in);
    CMP(in.opcode, VMU_OP_CALL); CMP(in.operands.absolute, 0xb9a);
    uint8_t brf[3] = { 0x11, 0x34, 0x12 };                   // BRF r16 = 0x1234, low byte first
    vmu_isa_decode(brf, &in);
    CMP(in.operands.relative16, 0x1234);
    uint8_t mov[3] = { 0x23, 0x7f, 0x99 };                   // MOV #0x99, 0x17f
    vmu_isa_decode(mov, &in);
    CMP(in.operands.direct, 0x17f); CMP(in.operands.immediate, 0x99);
    uint8_t be3[3] = { 0x34 | 2, 0x11, 0x22 };               // BE @R2, #0x11, r8 0x22
    vmu_isa_decode(be3, &in);
    CMP(in.operands.indirect, 2); CMP(in.operands.immediate, 0x11); CMP(in.operands.relative8, 0x22);
}

int main(void) {
    tctx T = { &g_vmu, 0, 0, "" };
    int ran = 0, passed = 0;
    vmu_init(&g_vmu, g_flash, NULL);
    vmu_write(&g_vmu, SFR_EXT, 0x01);

    T_RUN(nop); T_RUN(ld); T_RUN(ldInd); T_RUN(st); T_RUN(stInd); T_RUN(mov); T_RUN(movInd);
    T_RUN(push); T_RUN(pop); T_RUN(br); T_RUN(brf); T_RUN(jmp); T_RUN(jmpf); T_RUN(call);
    T_RUN(callr); T_RUN(callf); T_RUN(ret); T_RUN(bei); T_RUN(be); T_RUN(beInd); T_RUN(bnei);
    T_RUN(bne); T_RUN(bneInd); T_RUN(bp); T_RUN(bpc); T_RUN(bn); T_RUN(bz); T_RUN(dbnz);
    T_RUN(dbnzInd); T_RUN(addi); T_RUN(add); T_RUN(addInd); T_RUN(addci); T_RUN(addc);
    T_RUN(addcInd); T_RUN(subi); T_RUN(sub); T_RUN(subInd); T_RUN(subci); T_RUN(subc);
    T_RUN(subcInd); T_RUN(mul); T_RUN(div); T_RUN(andi); T_RUN(and); T_RUN(andInd); T_RUN(ori);
    T_RUN(or); T_RUN(orInd); T_RUN(xori); T_RUN(xor); T_RUN(xorInd); T_RUN(rol); T_RUN(rolc);
    T_RUN(ror); T_RUN(rorc); T_RUN(inc); T_RUN(incInd); T_RUN(dec); T_RUN(decInd); T_RUN(xch);
    T_RUN(xchInd); T_RUN(clr1); T_RUN(set1); T_RUN(not1); T_RUN(ldc); T_RUN(reti); T_RUN(ldf);
    T_RUN(stf); T_RUN(timerStartDelay16); T_RUN(timerCounterWriteStopped); T_RUN(clockTicksPerCycle);
    T_RUN(clockOcrReadbackHighBits); T_RUN(clockApiReflectsAndUpdatesRegisters);
    T_RUN(clockHoldStopsCpuAndTimers); T_RUN(clockHoldCancelsOnPort3Input);
    T_RUN(lcdBlankWhenDisabled); T_RUN(timer1ReloadCascade16); T_RUN(decodeAllOpcodes);

    printf("%d/%d cases passed, %d checks, %d failures\n", passed, ran, T.checks, T.fails);
    return T.fails ? 1 : 0;
}
