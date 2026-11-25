#include "sh2_recomp/decode.h"

#include <stdio.h>

typedef struct {
    uint16_t word;
    Sh2Instruction expected;
} DecodeCase;

int main(void)
{
    static const DecodeCase cases[] = {
        {0x2E34, {SH2_OP_MOV_B_PREDEC, 14, 3, 0, 0, false}},
        {0x2805, {SH2_OP_MOV_W_PREDEC, 8, 0, 0, 0, false}},
        {0x2FF6, {SH2_OP_MOV_L_PREDEC, 15, 15, 0, 0, false}},
        {0x68D2, {SH2_OP_MOV_L_LOAD, 8, 13, 0, 0, false}},
        {0x50D2, {SH2_OP_MOV_L_LOAD_DISP, 0, 13, 0, 8, false}},
        {0xC6FF, {SH2_OP_MOV_L_LOAD_GBR, 0, 0, 0, 1020, false}},
        {0xC209, {SH2_OP_MOV_L_STORE_GBR, 0, 0, 0, 36, false}},
        {0x6431, {SH2_OP_MOV_W_LOAD, 4, 3, 0, 0, false}},
        {0x65A5, {SH2_OP_MOV_W_POSTINC, 5, 10, 0, 0, false}},
        {0x6214, {SH2_OP_MOV_B_POSTINC, 2, 1, 0, 0, false}},
        {0x2122, {SH2_OP_MOV_L_STORE, 1, 2, 0, 0, false}},
        {0xC0FF, {SH2_OP_MOV_B_STORE_GBR, 0, 0, 0, 255, false}},
        {0x2819, {SH2_OP_AND_REG, 8, 1, 0, 0, false}},
        {0x2A1A, {SH2_OP_XOR_REG, 10, 1, 0, 0, false}},
        {0xCBF0, {SH2_OP_OR_IMM, 0, 0, 240, 0, false}},
        {0x654D, {SH2_OP_EXTU_W, 5, 4, 0, 0, false}},
        {0x6018, {SH2_OP_SWAP_B, 0, 1, 0, 0, false}},
        {0x6049, {SH2_OP_SWAP_W, 0, 4, 0, 0, false}},
        {0x203D, {SH2_OP_XTRCT, 0, 3, 0, 0, false}},
        {0x62e7, {SH2_OP_NOT, 2, 14, 0, 0, false}},
        {0x0008, {SH2_OP_CLRT, 0, 0, 0, 0, false}},
        {0x0018, {SH2_OP_SETT, 0, 0, 0, 0, false}},
        {0x0f29, {SH2_OP_MOVT, 0, 15, 0, 0, false}},
        {0x38B2, {SH2_OP_CMP_HS, 8, 11, 0, 0, false}},
        {0x3573, {SH2_OP_CMP_GE,  5,  7, 0, 0, false}},
        {0x3307, {SH2_OP_CMP_GT,  3,  0, 0, 0, false}},
        {0x480A, {SH2_OP_LDS_MACH,0,  8, 0, 0, false}},
        {0x4D28, {SH2_OP_SHLL16,  0, 13, 0, 0, false}},
        {0x0797, {SH2_OP_MUL_L,   7,  9, 0, 0, false}},
        {0x4100, {SH2_OP_SHLL, 0, 1, 0, 0, false}},
        {0x4A20, {SH2_OP_SHLL, 0, 10, 0, 0, false}},
        {0x4101, {SH2_OP_SHLR, 0, 1, 0, 0, false}},
        {0x4209, {SH2_OP_SHLR2, 0, 2, 0, 0, false}},
        {0x4319, {SH2_OP_SHLR8, 0, 3, 0, 0, false}},
        {0x4429, {SH2_OP_SHLR16, 0, 4, 0, 0, false}},
        {0x4F05, {SH2_OP_ROTR, 0, 15, 0, 0, false}},
        {0x4A21, {SH2_OP_SHAR, 0, 10, 0, 0, false}},
        {0x221F, {SH2_OP_MULS_W, 2, 1, 0, 0, false}},
        {0x231E, {SH2_OP_MULU_W, 3, 1, 0, 0, false}},
        {0x383D, {SH2_OP_DMULS_L, 8, 3, 0, 0, false}},
        {0x3835, {SH2_OP_DMULU_L, 8, 3, 0, 0, false}},
        {0x0028, {SH2_OP_CLRMAC, 0, 0, 0, 0, false}},
        {0x421F, {SH2_OP_MAC_W, 2, 1, 0, 0, false}},
        {0x00BF, {SH2_OP_MAC_L, 0, 11, 0, 0, false}},
        {0x0019, {SH2_OP_DIV0U, 0, 0, 0, 0, false}},
        {0x2317, {SH2_OP_DIV0S, 3, 1, 0, 0, false}},
        {0x3344, {SH2_OP_DIV1, 3, 4, 0, 0, false}},
        {0x4D24, {SH2_OP_ROTCL, 0, 13, 0, 0, false}},
        {0x4225, {SH2_OP_ROTCR, 0, 2, 0, 0, false}},
        {0x431b, {SH2_OP_TAS_B, 0, 3, 0, 0, false}},
        {0x4418, {SH2_OP_SHLL8, 0, 4, 0, 0, false}},
        {0x0124, {SH2_OP_MOV_B_STORE_INDEX, 1, 2, 0, 0, false}},
        {0x0315, {SH2_OP_MOV_W_STORE_INDEX, 3, 1, 0, 0, false}},
        {0x644b, {SH2_OP_NEG, 4, 4, 0, 0, false}},
        {0x85a4, {SH2_OP_MOV_W_LOAD_R0_DISP, 10, 0, 0, 8, false}},
        {0x3256, {SH2_OP_CMP_HI, 2, 5, 0, 0, false}},
        {0x642e, {SH2_OP_EXTS_B, 4, 2, 0, 0, false}},
        {0x631f, {SH2_OP_EXTS_W, 3, 1, 0, 0, false}},
        {0x0236, {SH2_OP_MOV_L_STORE_INDEX, 2, 3, 0, 0, false}},
        {0x4515, {SH2_OP_CMP_PL, 0, 5, 0, 0, false}},
        {0x001A, {SH2_OP_STS_MACL, 0, 0, 0, 0, false}},
        {0x0B2A, {SH2_OP_STS_PR, 0, 11, 0, 0, false}},
        {0xC9FF, {SH2_OP_AND_IMM, 0, 0, 255, 0, false}},
        {0x015C, {SH2_OP_MOV_B_LOAD_INDEX, 1, 5, 0, 0, false}},
        {0x001D, {SH2_OP_MOV_W_LOAD_INDEX, 0, 1, 0, 0, false}},
        {0x4510, {SH2_OP_DT, 0, 5, 0, 0, false}},
        {0x93FF, {SH2_OP_MOV_W_PC, 3, 0, 0, 510, false}},
        {0x809F, {SH2_OP_MOV_B_STORE_R0_DISP, 9, 0, 0, 15, false}},
        {0x819F, {SH2_OP_MOV_W_STORE_R0_DISP, 9, 0, 0, 30, false}},
        {0x2348, {SH2_OP_TST_REG, 3, 4, 0, 0, false}},
        {0x0009, {SH2_OP_NOP,        0,  0,    0,     0, false}},
        {0x000B, {SH2_OP_RTS,        0,  0,    0,     0, true}},
        {0xEF80, {SH2_OP_MOV_IMM,   15,  0, -128,     0, false}},
        {0xE27F, {SH2_OP_MOV_IMM,    2,  0,  127,     0, false}},
        {0x73FF, {SH2_OP_ADD_IMM,    3,  0,   -1,     0, false}},
        {0x6A53, {SH2_OP_MOV_REG,   10,  5,    0,     0, false}},
        {0x31EC, {SH2_OP_ADD_REG,    1, 14,    0,     0, false}},
        {0x3D40, {SH2_OP_CMP_EQ_REG,13,  4,    0,     0, false}},
        {0xA7FF, {SH2_OP_BRA,        0,  0,    0,  4094, true}},
        {0xA800, {SH2_OP_BRA,        0,  0,    0, -4096, true}},
        {0xBFFF, {SH2_OP_BSR,        0,  0,    0,    -2, true}},
        {0x0523, {SH2_OP_BRAF,       0,  5,    0,     0, true}},
        {0x897F, {SH2_OP_BT,         0,  0,    0,   254, false}},
        {0x8B80, {SH2_OP_BF,         0,  0,    0,  -256, false}},
        {0x8D00, {SH2_OP_BT_S,       0,  0,    0,     0, true}},
        {0x8FFF, {SH2_OP_BF_S,       0,  0,    0,    -2, true}},
        {0x4E2B, {SH2_OP_JMP,        0, 14,    0,     0, true}},
        {0x470B, {SH2_OP_JSR,        0,  7,    0,     0, true}},
        {0xD8FF, {SH2_OP_MOV_L_PC,    8,  0,    0,  1020, false}},
        {0x68A6, {SH2_OP_MOV_L_POSTINC,8, 10,   0,     0, false}},
        {0x191F, {SH2_OP_MOV_L_STORE_DISP,9,1,  0,    60, false}},
        {0x2A30, {SH2_OP_MOV_B_STORE,10,  3,    0,     0, false}},
        {0x2C41, {SH2_OP_MOV_W_STORE,12,  4,    0,     0, false}},
        {0xC4FE, {SH2_OP_MOV_B_LOAD_GBR,0,0,    0,   254, false}},
        {0xC1EA, {SH2_OP_MOV_W_STORE_GBR,0,0,   0,   468, false}},
        {0x4F0E, {SH2_OP_LDC_SR,     0, 15,    0,     0, false}},
        {0x4E1E, {SH2_OP_LDC_GBR,    0, 14,    0,     0, false}},
        {0x4D2E, {SH2_OP_LDC_VBR,    0, 13,    0,     0, false}},
        {0x0F02, {SH2_OP_STC_SR,     0, 15,    0,     0, false}},
        {0x0E12, {SH2_OP_STC_GBR,    0, 14,    0,     0, false}},
        {0x0D22, {SH2_OP_STC_VBR,    0, 13,    0,     0, false}},
        {0x4F12, {SH2_OP_STS_L_MACL, 0, 15,    0,     0, false}},
        {0x4F16, {SH2_OP_LDS_L_MACL, 0, 15,    0,     0, false}},
        {0x3E58, {SH2_OP_SUB_REG,   14,  5,    0,     0, false}},
        {0xC8FF, {SH2_OP_TST_IMM,    0,  0,  255,     0, false}},
        {0x8880, {SH2_OP_CMP_EQ_IMM, 0,  0, -128,     0, false}},
        {0x002B, {SH2_OP_RTE,       0,  0,    0,     0, true}},
        {0x0109, {SH2_OP_UNSUPPORTED,0,  0,    0,     0, false}},
        {0x3D41, {SH2_OP_UNSUPPORTED,0,  0,    0,     0, false}},
        {0x4E2A, {SH2_OP_LDS_PR,     0, 14,    0,     0, false}},
        {0xFFFF, {SH2_OP_UNSUPPORTED,0,  0,    0,     0, false}}
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Sh2Instruction actual = sh2_decode(cases[i].word);
        Sh2Instruction expected = cases[i].expected;
        if (actual.opcode != expected.opcode || actual.rn != expected.rn ||
            actual.rm != expected.rm || actual.immediate != expected.immediate ||
            actual.displacement != expected.displacement ||
            actual.delayed != expected.delayed) {
            fprintf(stderr, "Incorrect decode of %04X\n", (unsigned)cases[i].word);
            return 1;
        }
    }
    return 0;
}
