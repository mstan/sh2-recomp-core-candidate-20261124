#include "sh2_recomp/decode.h"

typedef enum {
    NO_OPERANDS,
    IMMEDIATE_RN,
    RM_RN,
    TARGET_RM,
    DISP8,
    DISP12,
    LITERAL_WORD,
    LITERAL_LONG,
    RM_RN_DISP_LONG,
    R0_RN_DISP_BYTE,
    R0_RN_DISP_WORD,
    DISP_GBR_BYTE,
    DISP_GBR_WORD,
    SCALED_DISP8_LONG,
    IMM8_UNSIGNED,
    IMM8_SIGNED
} OperandFormat;

typedef struct {
    uint16_t mask;
    uint16_t value;
    Sh2Opcode opcode;
    OperandFormat format;
    bool delayed;
} Encoding;

static const Encoding encodings[] = {
    {0xffff, 0x002b, SH2_OP_RTE,       NO_OPERANDS, true},
    {0xFFFF, 0x0019, SH2_OP_DIV0U,      NO_OPERANDS, false},
    {0xFFFF, 0x0008, SH2_OP_CLRT,       NO_OPERANDS, false},
    {0xFFFF, 0x0018, SH2_OP_SETT,       NO_OPERANDS, false},
    {0xFFFF, 0x0009, SH2_OP_NOP,        NO_OPERANDS, false},
    {0xFFFF, 0x001B, SH2_OP_SLEEP,      NO_OPERANDS, false},
    {0xF0FF, 0x0029, SH2_OP_MOVT,       TARGET_RM, false},
    {0xFFFF, 0x0028, SH2_OP_CLRMAC,     NO_OPERANDS, false},
    {0xFFFF, 0x000B, SH2_OP_RTS,        NO_OPERANDS, true},
    {0xF000, 0xE000, SH2_OP_MOV_IMM,    IMMEDIATE_RN, false},
    {0xF000, 0x7000, SH2_OP_ADD_IMM,    IMMEDIATE_RN, false},
    {0xF00F, 0x6003, SH2_OP_MOV_REG,    RM_RN, false},
    {0xF00F, 0x300C, SH2_OP_ADD_REG,    RM_RN, false},
    {0xF00F, 0x3000, SH2_OP_CMP_EQ_REG, RM_RN, false},
    {0xF000, 0xA000, SH2_OP_BRA,        DISP12, true},
    {0xF000, 0xB000, SH2_OP_BSR,        DISP12, true},
    {0xF0FF, 0x0003, SH2_OP_BSRF,       TARGET_RM, true},
    {0xF0FF, 0x0023, SH2_OP_BRAF,       TARGET_RM, true},
    {0xFF00, 0x8900, SH2_OP_BT,         DISP8, false},
    {0xFF00, 0x8B00, SH2_OP_BF,         DISP8, false},
    {0xFF00, 0x8D00, SH2_OP_BT_S,       DISP8, true},
    {0xFF00, 0x8F00, SH2_OP_BF_S,       DISP8, true},
    {0xF0FF, 0x402B, SH2_OP_JMP,        TARGET_RM, true},
    {0xF0FF, 0x400B, SH2_OP_JSR,        TARGET_RM, true},
    {0xF000, 0xD000, SH2_OP_MOV_L_PC,   LITERAL_LONG, false},
    {0xF000, 0x9000, SH2_OP_MOV_W_PC,   LITERAL_WORD, false},
    {0xF00F, 0x6006, SH2_OP_MOV_L_POSTINC, RM_RN, false},
    {0xF00F, 0x6005, SH2_OP_MOV_W_POSTINC, RM_RN, false},
    {0xF00F, 0x6004, SH2_OP_MOV_B_POSTINC, RM_RN, false},
    {0xF000, 0x1000, SH2_OP_MOV_L_STORE_DISP, RM_RN_DISP_LONG, false},
    {0xFF00, 0x8000, SH2_OP_MOV_B_STORE_R0_DISP, R0_RN_DISP_BYTE, false},
    {0xFF00, 0x8100, SH2_OP_MOV_W_STORE_R0_DISP, R0_RN_DISP_WORD, false},
    {0xFF00, 0x8400, SH2_OP_MOV_B_LOAD_R0_DISP, R0_RN_DISP_BYTE, false},
    {0xFF00, 0x8500, SH2_OP_MOV_W_LOAD_R0_DISP, R0_RN_DISP_WORD, false},
    {0xF00F, 0x2000, SH2_OP_MOV_B_STORE, RM_RN, false},
    {0xF00F, 0x2001, SH2_OP_MOV_W_STORE, RM_RN, false},
    {0xF00F, 0x2002, SH2_OP_MOV_L_STORE, RM_RN, false},
    {0xF00F, 0x0004, SH2_OP_MOV_B_STORE_INDEX, RM_RN, false},
    {0xF00F, 0x0005, SH2_OP_MOV_W_STORE_INDEX, RM_RN, false},
    {0xF00F, 0x0006, SH2_OP_MOV_L_STORE_INDEX, RM_RN, false},
    {0xF00F, 0x2004, SH2_OP_MOV_B_PREDEC, RM_RN, false},
    {0xF00F, 0x2005, SH2_OP_MOV_W_PREDEC, RM_RN, false},
    {0xF00F, 0x2006, SH2_OP_MOV_L_PREDEC, RM_RN, false},
    {0xF00F, 0x6002, SH2_OP_MOV_L_LOAD, RM_RN, false},
    {0xF00F, 0x000C, SH2_OP_MOV_B_LOAD_INDEX, RM_RN, false},
    {0xF00F, 0x000D, SH2_OP_MOV_W_LOAD_INDEX, RM_RN, false},
    {0xF00F, 0x000E, SH2_OP_MOV_L_LOAD_INDEX, RM_RN, false},
    {0xF00F, 0x6001, SH2_OP_MOV_W_LOAD, RM_RN, false},
    {0xF00F, 0x6000, SH2_OP_MOV_B_LOAD, RM_RN, false},
    {0xF000, 0x5000, SH2_OP_MOV_L_LOAD_DISP, RM_RN_DISP_LONG, false},
    {0xFF00, 0xC400, SH2_OP_MOV_B_LOAD_GBR, DISP_GBR_BYTE, false},
    {0xFF00, 0xC500, SH2_OP_MOV_W_LOAD_GBR, DISP_GBR_WORD, false},
    {0xFF00, 0xC600, SH2_OP_MOV_L_LOAD_GBR, SCALED_DISP8_LONG, false},
    {0xFF00, 0xC000, SH2_OP_MOV_B_STORE_GBR, DISP_GBR_BYTE, false},
    {0xFF00, 0xC100, SH2_OP_MOV_W_STORE_GBR, DISP_GBR_WORD, false},
    {0xFF00, 0xC200, SH2_OP_MOV_L_STORE_GBR, SCALED_DISP8_LONG, false},
    {0xFF00, 0xC700, SH2_OP_MOVA,       SCALED_DISP8_LONG, false},
    {0xF0FF, 0x400E, SH2_OP_LDC_SR,     TARGET_RM, false},
    {0xF0FF, 0x401E, SH2_OP_LDC_GBR,    TARGET_RM, false},
    {0xF0FF, 0x402E, SH2_OP_LDC_VBR,    TARGET_RM, false},
    {0xF0FF, 0x4007, SH2_OP_LDC_L_SR,   TARGET_RM, false},
    {0xF0FF, 0x4017, SH2_OP_LDC_L_GBR,  TARGET_RM, false},
    {0xF0FF, 0x4027, SH2_OP_LDC_L_VBR,  TARGET_RM, false},
    {0xF0FF, 0x0002, SH2_OP_STC_SR,     TARGET_RM, false},
    {0xF0FF, 0x0012, SH2_OP_STC_GBR,    TARGET_RM, false},
    {0xF0FF, 0x0022, SH2_OP_STC_VBR,    TARGET_RM, false},
    {0xF0FF, 0x4003, SH2_OP_STC_L_SR,   TARGET_RM, false},
    {0xF0FF, 0x4013, SH2_OP_STC_L_GBR,  TARGET_RM, false},
    {0xF0FF, 0x4023, SH2_OP_STC_L_VBR,  TARGET_RM, false},
    {0xF0FF, 0x000A, SH2_OP_STS_MACH,   TARGET_RM, false},
    {0xF0FF, 0x001A, SH2_OP_STS_MACL,   TARGET_RM, false},
    {0xF0FF, 0x002A, SH2_OP_STS_PR,     TARGET_RM, false},
    {0xF0FF, 0x4022, SH2_OP_STS_L_PR,   TARGET_RM, false},
    {0xF0FF, 0x4026, SH2_OP_LDS_L_PR,   TARGET_RM, false},
    {0xF0FF, 0x4012, SH2_OP_STS_L_MACL, TARGET_RM, false},
    {0xF0FF, 0x4016, SH2_OP_LDS_L_MACL, TARGET_RM, false},
    {0xF0FF, 0x402A, SH2_OP_LDS_PR,    TARGET_RM, false},
    {0xF0FF, 0x400A, SH2_OP_LDS_MACH,  TARGET_RM, false},
    {0xF0FF, 0x401A, SH2_OP_LDS_MACL,  TARGET_RM, false},
    {0xF0FF, 0x4002, SH2_OP_STS_L_MACH, TARGET_RM, false},
    {0xF0FF, 0x4006, SH2_OP_LDS_L_MACH, TARGET_RM, false},
    {0xF00F, 0x3008, SH2_OP_SUB_REG,    RM_RN, false},
    {0xF00F, 0x300A, SH2_OP_SUBC,       RM_RN, false},
    {0xF00F, 0x300E, SH2_OP_ADDC,       RM_RN, false},
    {0xF00F, 0x600B, SH2_OP_NEG,        RM_RN, false},
    {0xF00F, 0x2009, SH2_OP_AND_REG,    RM_RN, false},
    {0xF00F, 0x200A, SH2_OP_XOR_REG,    RM_RN, false},
    {0xF00F, 0x200B, SH2_OP_OR_REG,     RM_RN, false},
    {0xF00F, 0x200D, SH2_OP_XTRCT,      RM_RN, false},
    {0xF00F, 0x6007, SH2_OP_NOT,        RM_RN, false},
    {0xF00F, 0x200F, SH2_OP_MULS_W,     RM_RN, false},
    {0xF00F, 0x200E, SH2_OP_MULU_W,     RM_RN, false},
    {0xF00F, 0x400F, SH2_OP_MAC_W,      RM_RN, false},
    {0xF00F, 0x000F, SH2_OP_MAC_L,      RM_RN, false},
    {0xF00F, 0x2007, SH2_OP_DIV0S,      RM_RN, false},
    {0xF00F, 0x3004, SH2_OP_DIV1,       RM_RN, false},
    {0xF00F, 0x3002, SH2_OP_CMP_HS,     RM_RN, false},
    {0xF00F, 0x3003, SH2_OP_CMP_GE,     RM_RN, false},
    {0xF00F, 0x3006, SH2_OP_CMP_HI,     RM_RN, false},
    {0xF00F, 0x3007, SH2_OP_CMP_GT,     RM_RN, false},
    {0xF00F, 0x0007, SH2_OP_MUL_L,      RM_RN, false},
    {0xF00F, 0x300D, SH2_OP_DMULS_L,    RM_RN, false},
    {0xF00F, 0x3005, SH2_OP_DMULU_L,    RM_RN, false},
    {0xF00F, 0x600D, SH2_OP_EXTU_W,     RM_RN, false},
    {0xF00F, 0x600C, SH2_OP_EXTU_B,     RM_RN, false},
    {0xF00F, 0x600E, SH2_OP_EXTS_B,     RM_RN, false},
    {0xF00F, 0x600F, SH2_OP_EXTS_W,     RM_RN, false},
    {0xF00F, 0x6008, SH2_OP_SWAP_B,     RM_RN, false},
    {0xF00F, 0x6009, SH2_OP_SWAP_W,     RM_RN, false},
    {0xF0DF, 0x4000, SH2_OP_SHLL,       TARGET_RM, false}, /* SHLL and SHAL. */
    {0xF0FF, 0x4001, SH2_OP_SHLR,       TARGET_RM, false},
    {0xF0FF, 0x4009, SH2_OP_SHLR2,      TARGET_RM, false},
    {0xF0FF, 0x4019, SH2_OP_SHLR8,      TARGET_RM, false},
    {0xF0FF, 0x4029, SH2_OP_SHLR16,     TARGET_RM, false},
    {0xF0FF, 0x4004, SH2_OP_ROTL,       TARGET_RM, false},
    {0xF0FF, 0x4005, SH2_OP_ROTR,       TARGET_RM, false},
    {0xF0FF, 0x4024, SH2_OP_ROTCL,      TARGET_RM, false},
    {0xF0FF, 0x4025, SH2_OP_ROTCR,      TARGET_RM, false},
    {0xF0FF, 0x4021, SH2_OP_SHAR,       TARGET_RM, false},
    {0xF0FF, 0x4008, SH2_OP_SHLL2,      TARGET_RM, false},
    {0xF0FF, 0x4018, SH2_OP_SHLL8,      TARGET_RM, false},
    {0xF0FF, 0x4028, SH2_OP_SHLL16,     TARGET_RM, false},
    {0xF0FF, 0x4011, SH2_OP_CMP_PZ,     TARGET_RM, false},
    {0xF0FF, 0x4015, SH2_OP_CMP_PL,     TARGET_RM, false},
    {0xF0FF, 0x4010, SH2_OP_DT,         TARGET_RM, false},
    {0xF0FF, 0x401B, SH2_OP_TAS_B,      TARGET_RM, false},
    {0xF00F, 0x2008, SH2_OP_TST_REG,    RM_RN, false},
    {0xFF00, 0xC800, SH2_OP_TST_IMM,    IMM8_UNSIGNED, false},
    {0xFF00, 0xC900, SH2_OP_AND_IMM,    IMM8_UNSIGNED, false},
    {0xFF00, 0xCB00, SH2_OP_OR_IMM,     IMM8_UNSIGNED, false},
    {0xFF00, 0xCA00, SH2_OP_XOR_IMM,    IMM8_UNSIGNED, false},
    {0xFF00, 0xCC00, SH2_OP_TST_B_IMM,  IMM8_UNSIGNED, false},
    {0xFF00, 0xCD00, SH2_OP_AND_B_IMM,  IMM8_UNSIGNED, false},
    {0xFF00, 0xCE00, SH2_OP_XOR_B_IMM,  IMM8_UNSIGNED, false},
    {0xFF00, 0xCF00, SH2_OP_OR_B_IMM,   IMM8_UNSIGNED, false},
    {0xFF00, 0x8800, SH2_OP_CMP_EQ_IMM, IMM8_SIGNED, false}
};

static int16_t sign_extend(uint16_t field, int32_t modulus)
{
    return (int16_t)(field < modulus / 2 ? field : (int32_t)field - modulus);
}

Sh2Instruction sh2_decode(uint16_t word)
{
    Sh2Instruction decoded = {0};
    for (unsigned i = 0; i < sizeof(encodings) / sizeof(encodings[0]); ++i) {
        const Encoding *encoding = &encodings[i];
        if ((word & encoding->mask) != encoding->value)
            continue;

        decoded.opcode = encoding->opcode;
        decoded.delayed = encoding->delayed;
        switch (encoding->format) {
        case NO_OPERANDS:
            break;
        case IMMEDIATE_RN:
            decoded.rn = (uint8_t)((word >> 8) & 15u);
            decoded.immediate = sign_extend(word & 0xFFu, 0x100);
            break;
        case RM_RN:
            decoded.rn = (uint8_t)((word >> 8) & 15u);
            decoded.rm = (uint8_t)((word >> 4) & 15u);
            break;
        case TARGET_RM:
            decoded.rm = (uint8_t)((word >> 8) & 15u);
            break;
        case DISP8:
            decoded.displacement = (int16_t)(2 * sign_extend(word & 0xFFu, 0x100));
            break;
        case DISP12:
            decoded.displacement = (int16_t)(2 * sign_extend(word & 0xFFFu, 0x1000));
            break;
        case LITERAL_WORD:
        case LITERAL_LONG:
            decoded.rn = (uint8_t)((word >> 8) & 15u);
            decoded.displacement = (int16_t)((encoding->format == LITERAL_WORD ? 2 : 4) * (word & 0xFFu));
            break;
        case RM_RN_DISP_LONG:
            decoded.rn = (uint8_t)((word >> 8) & 15u);
            decoded.rm = (uint8_t)((word >> 4) & 15u);
            decoded.displacement = (int16_t)(4 * (word & 15u));
            break;
        case R0_RN_DISP_BYTE:
        case R0_RN_DISP_WORD:
            decoded.rn = (uint8_t)((word >> 4) & 15u);
            decoded.displacement = (int16_t)((encoding->format == R0_RN_DISP_BYTE ? 1 : 2) * (word & 15u));
            break;
        case DISP_GBR_BYTE:
            decoded.displacement = (int16_t)(word & 0xFFu);
            break;
        case DISP_GBR_WORD:
            decoded.displacement = (int16_t)(2 * (word & 0xFFu));
            break;
        case SCALED_DISP8_LONG:
            decoded.displacement = (int16_t)(4 * (word & 0xFFu));
            break;
        case IMM8_UNSIGNED:
            decoded.immediate = (int16_t)(word & 0xFFu);
            break;
        case IMM8_SIGNED:
            decoded.immediate = sign_extend(word & 0xFFu, 0x100);
            break;
        }
        break;
    }
    return decoded;
}
