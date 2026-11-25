#ifndef SH2_RECOMP_OPERATIONS_H
#define SH2_RECOMP_OPERATIONS_H

#include "sh2_recomp/step.h"

/* One-instruction primitives shared by generated C and the interpreter.
 * The decoder supplies in-range register indices and aligned widths. */
enum { SH2_SR_T = 1, SH2_SR_S = 2, SH2_SR_Q = 0x100, SH2_SR_M = 0x200, SH2_SR_BITS = 0x3F3 };

static inline Sh2StepResult sh2_retire(Sh2Context *context, unsigned cycles)
{
    Sh2State *state = &context->state;
    state->irq_inhibit = false;
    state->cycles += cycles;
    if (state->delay_pending) {
        state->pc = state->delay_target;
        state->delay_target = 0;
        state->delay_pending = false;
    } else {
        state->pc += 2;
    }
    return SH2_STEP_OK;
}

static inline Sh2StepResult sh2_op_register(Sh2Context *context, unsigned rn,
                                            uint32_t value)
{
    context->state.r[rn] = value;
    return sh2_retire(context, 1);
}

static inline Sh2StepResult sh2_op_sleep(Sh2Context *context)
{
    if (context->state.delay_pending)
        return SH2_STEP_SLOT_ILLEGAL;
    context->state.sleeping = true;
    return sh2_retire(context, 3);
}

static inline Sh2StepResult sh2_op_t(Sh2Context *context, bool value)
{
    context->state.sr = (context->state.sr & ~(uint32_t)SH2_SR_T) | (uint32_t)value;
    return sh2_retire(context, 1);
}

static inline bool sh2_add_with_carry(uint32_t *destination, uint32_t operand, bool carry)
{
    const uint64_t sum = (uint64_t)*destination + operand + carry;
    *destination = (uint32_t)sum;
    return (sum >> 32) != 0;
}

static inline bool sh2_subtract_with_borrow(uint32_t *destination, uint32_t operand, bool borrow)
{
    const uint64_t minuend = *destination;
    const uint64_t subtrahend = (uint64_t)operand + borrow;
    *destination = (uint32_t)(minuend - subtrahend);
    return minuend < subtrahend;
}

static inline Sh2StepResult sh2_op_addc(Sh2Context *context, unsigned rn, unsigned rm)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_add_with_carry(&state->r[rn], state->r[rm], state->sr & SH2_SR_T));
}

static inline Sh2StepResult sh2_op_subc(Sh2Context *context, unsigned rn, unsigned rm)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_subtract_with_borrow(&state->r[rn], state->r[rm], state->sr & SH2_SR_T));
}

static inline Sh2StepResult sh2_op_control(Sh2Context *context, uint32_t *destination,
                                          uint32_t value)
{
    *destination = value;
    sh2_retire(context, 1);
    context->state.irq_inhibit = true;
    return SH2_STEP_OK;
}

static inline bool sh2_shift_right_one(uint32_t *register_value, bool incoming)
{
    const uint32_t value = *register_value;
    *register_value = (value >> 1) | ((uint32_t)incoming << 31);
    return (value & 1) != 0;
}

static inline bool sh2_shift_left_one(uint32_t *register_value, bool incoming)
{
    const uint32_t value = *register_value;
    *register_value = (value << 1) | (uint32_t)incoming;
    return (value >> 31) != 0;
}

static inline Sh2StepResult sh2_op_shlr(Sh2Context *context, unsigned rn)
{
    return sh2_op_t(context, sh2_shift_right_one(&context->state.r[rn], false));
}

static inline Sh2StepResult sh2_op_shll(Sh2Context *context, unsigned rn)
{
    return sh2_op_t(context, sh2_shift_left_one(&context->state.r[rn], false));
}

static inline Sh2StepResult sh2_op_rotl(Sh2Context *context, unsigned rn)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_shift_left_one(&state->r[rn], state->r[rn] >> 31));
}

static inline Sh2StepResult sh2_op_rotr(Sh2Context *context, unsigned rn)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_shift_right_one(&state->r[rn], state->r[rn] & 1));
}

static inline Sh2StepResult sh2_op_rotcl(Sh2Context *context, unsigned rn)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_shift_left_one(&state->r[rn], state->sr & SH2_SR_T));
}

static inline Sh2StepResult sh2_op_rotcr(Sh2Context *context, unsigned rn)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_shift_right_one(&state->r[rn], state->sr & SH2_SR_T));
}

static inline Sh2StepResult sh2_op_shar(Sh2Context *context, unsigned rn)
{
    Sh2State *state = &context->state;
    return sh2_op_t(context, sh2_shift_right_one(&state->r[rn], state->r[rn] >> 31));
}

static inline Sh2StepResult sh2_op_clrmac(Sh2Context *context)
{
    context->state.mach = context->state.macl = 0;
    return sh2_retire(context, 1);
}

static inline int64_t sh2_signed_operand(uint32_t value, uint32_t sign_bit)
{
    return (int64_t)(value ^ sign_bit) - sign_bit;
}

static inline uint32_t sh2_signed_word_product(uint32_t left, uint32_t right)
{
    return (uint32_t)(sh2_signed_operand(left & 0xffffu, 0x8000) *
                      sh2_signed_operand(right & 0xffffu, 0x8000));
}

static inline void sh2_accumulate_product(uint32_t *mach, uint32_t *macl, uint32_t sr,
                                          uint32_t first, uint32_t second, unsigned size)
{
    const bool wide = size == 4;
    const uint32_t sign = wide ? UINT32_C(0x80000000) : UINT32_C(0x8000);
    const uint32_t mask = (sign - 1) | sign;
    const int64_t product = sh2_signed_operand(first & mask, sign) * sh2_signed_operand(second & mask, sign);
    if ((sr & SH2_SR_S) && !wide) {
        int64_t sum = sh2_signed_operand(*macl, UINT32_C(0x80000000)) + product;
        if (sum > INT32_MAX)
            sum = INT32_MAX;
        else if (sum < INT32_MIN)
            sum = INT32_MIN;
        *macl = (uint32_t)sum;
        return;
    }

    uint64_t sum;
    if (sr & SH2_SR_S) {
        const int64_t limit = INT64_C(1) << 47;
        const int64_t accumulator =
            sh2_signed_operand(*mach & 0xffffu, 0x8000) * (INT64_C(1) << 32) + *macl;
        int64_t saturated = accumulator + product;
        if (saturated >= limit)
            saturated = limit - 1;
        else if (saturated < -limit)
            saturated = -limit;
        sum = (uint64_t)saturated;
    } else {
        const uint64_t accumulator = ((uint64_t)*mach << 32) | *macl;
        sum = accumulator + (uint64_t)product;
    }
    *mach = (uint32_t)(sum >> 32);
    *macl = (uint32_t)sum;
}

static inline Sh2StepResult sh2_op_muls_w(Sh2Context *context, unsigned rn, unsigned rm)
{
    Sh2State *state = &context->state;
    state->macl = sh2_signed_word_product(state->r[rn], state->r[rm]);
    return sh2_retire(context, 1);
}

static inline Sh2StepResult sh2_op_mulu_w(Sh2Context *context, unsigned rn, unsigned rm)
{
    Sh2State *state = &context->state;
    state->macl = (state->r[rn] & 0xffffu) * (state->r[rm] & 0xffffu);
    return sh2_retire(context, 1);
}

static inline uint32_t sh2_signed_order(uint32_t value)
{
    return value ^ UINT32_C(0x80000000);
}

static inline uint32_t sh2_extend_signed(uint32_t value, unsigned size)
{
    if (size < 4) {
        const uint32_t sign = UINT32_C(1) << (8 * size - 1);
        const uint32_t mask = (sign << 1) - 1;
        return ((value & mask) ^ sign) - sign;
    }
    return value;
}

static inline Sh2StepResult sh2_op_mul_l(Sh2Context *context, unsigned rn, unsigned rm)
{
    Sh2State *state = &context->state;
    state->macl = state->r[rn] * state->r[rm];
    return sh2_retire(context, 2);
}

static inline Sh2StepResult sh2_store_double_product(Sh2Context *context, uint64_t product)
{
    context->state.mach = (uint32_t)(product >> 32);
    context->state.macl = (uint32_t)product;
    return sh2_retire(context, 2);
}

static inline Sh2StepResult sh2_op_dmuls_l(Sh2Context *context, unsigned rn, unsigned rm)
{
    const Sh2State *state = &context->state;
    const int64_t left = (int64_t)sh2_signed_order(state->r[rn]) - INT64_C(0x80000000);
    const int64_t right = (int64_t)sh2_signed_order(state->r[rm]) - INT64_C(0x80000000);
    return sh2_store_double_product(context, (uint64_t)(left * right));
}

static inline Sh2StepResult sh2_op_dmulu_l(Sh2Context *context, unsigned rn, unsigned rm)
{
    const Sh2State *state = &context->state;
    return sh2_store_double_product(context, (uint64_t)state->r[rn] * state->r[rm]);
}

static inline void sh2_divide_initialize(uint32_t *sr, bool q, bool m)
{
    *sr = (*sr & ~(uint32_t)(SH2_SR_Q | SH2_SR_M | SH2_SR_T)) |
          (q ? SH2_SR_Q : 0u) | (m ? SH2_SR_M : 0u) | (q != m ? SH2_SR_T : 0u);
}

static inline void sh2_divide_step(uint32_t *sr, uint32_t *destination, const uint32_t *operand)
{
    const bool m = (*sr & SH2_SR_M) != 0;
    const bool add_divisor = ((*sr & SH2_SR_Q) != 0) != m;
    const bool high_bit = (*destination >> 31) != 0;
    const uint32_t shifted = (*destination << 1) | (*sr & SH2_SR_T);
    *destination = shifted;

    /* The SH-2 shifts Rn before the add/subtract, including when Rm is Rn. */
    const uint32_t divisor = *operand;
    const uint32_t partial = add_divisor ? shifted + divisor : shifted - divisor;
    const bool carry_or_borrow = add_divisor ? partial < shifted : partial > shifted;
    const bool q = (high_bit != carry_or_borrow) != m;
    *destination = partial;
    *sr = (*sr & ~(uint32_t)(SH2_SR_Q | SH2_SR_T)) | (q ? SH2_SR_Q : 0u) | (q == m ? SH2_SR_T : 0u);
}

static inline Sh2StepResult sh2_op_div0(Sh2Context *context, bool q, bool m)
{
    sh2_divide_initialize(&context->state.sr, q, m);
    return sh2_retire(context, 1);
}

static inline Sh2StepResult sh2_op_div1(Sh2Context *context, unsigned rn, unsigned rm)
{
    Sh2State *state = &context->state;
    sh2_divide_step(&state->sr, &state->r[rn], &state->r[rm]);
    return sh2_retire(context, 1);
}

static inline Sh2StepResult sh2_op_dt(Sh2Context *context, unsigned rn)
{
    return sh2_op_t(context, --context->state.r[rn] == 0);
}

static inline uint32_t sh2_literal_pc(const Sh2State *state)
{
    return state->delay_pending ? state->delay_target + 2 : state->pc + 4;
}

static inline Sh2StepResult sh2_op_branch(Sh2Context *context, uint32_t target,
                                         bool taken, bool delayed, bool link)
{
    Sh2State *state = &context->state;
    if (state->delay_pending)
        return SH2_STEP_SLOT_ILLEGAL;
    if (!taken && !delayed)
        return sh2_retire(context, 1);
    state->irq_inhibit = false;
    if (link)
        state->pr = state->pc + 4;
    if (delayed) {
        state->delay_target = taken ? target : state->pc + 4;
        state->delay_pending = true;
        state->pc += 2;
        state->cycles += taken ? 2 : 1;
    } else {
        state->pc = target;
        state->cycles += 3;
    }
    return SH2_STEP_OK;
}

#ifdef __cplusplus
extern "C" {
#endif

Sh2StepResult sh2_op_mov_l_pc(Sh2Context *context, unsigned rn, uint32_t displacement);
Sh2StepResult sh2_op_mov_w_pc(Sh2Context *context, unsigned rn, uint32_t displacement);
Sh2StepResult sh2_op_mov_l_postinc(Sh2Context *context, unsigned rn, unsigned rm);
Sh2StepResult sh2_op_sts_l_pr(Sh2Context *context, unsigned rn);
Sh2StepResult sh2_op_lds_l_pr(Sh2Context *context, unsigned rm);
Sh2StepResult sh2_op_sts_l(Sh2Context *context, unsigned rn, uint32_t value);
Sh2StepResult sh2_op_lds_l(Sh2Context *context, unsigned rm, uint32_t *destination);
Sh2StepResult sh2_op_ldc_l(Sh2Context *context, unsigned rm, uint32_t *destination, uint32_t mask);
Sh2StepResult sh2_op_stc_l(Sh2Context *context, unsigned rn, uint32_t value);
Sh2StepResult sh2_op_rte(Sh2Context *context);
Sh2StepResult sh2_op_mac(Sh2Context *context, unsigned rn, unsigned rm, unsigned size);

static inline Sh2StepResult sh2_op_mac_w(Sh2Context *context, unsigned rn, unsigned rm)
{
    return sh2_op_mac(context, rn, rm, 2);
}
Sh2StepResult sh2_op_load_postinc(Sh2Context *context, unsigned rn, unsigned rm, unsigned size);
Sh2StepResult sh2_op_load(Sh2Context *context, unsigned rn, uint32_t address, unsigned size);
Sh2StepResult sh2_op_store(Sh2Context *context, uint32_t address, uint32_t value,
                           unsigned size);
Sh2StepResult sh2_op_store_predec(Sh2Context *context, unsigned rn, unsigned rm,
                                  unsigned size);
Sh2StepResult sh2_op_test_byte(Sh2Context *context, uint32_t address, uint8_t mask);
Sh2StepResult sh2_op_modify_byte(Sh2Context *context, uint32_t address, uint16_t opcode,
                                 uint8_t keep, uint8_t toggle);
Sh2StepResult sh2_op_tas_byte(Sh2Context *context, unsigned rn);

#ifdef __cplusplus
}
#endif
#endif
