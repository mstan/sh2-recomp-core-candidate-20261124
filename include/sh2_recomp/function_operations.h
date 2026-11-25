#ifndef SH2_RECOMP_FUNCTION_OPERATIONS_H
#define SH2_RECOMP_FUNCTION_OPERATIONS_H

#if defined(__has_attribute)
#if __has_attribute(musttail) && !defined(SH2_PORTABLE_TAILS)
#define SH2_CAN_MUSTTAIL 1
#endif
#endif
#if defined(SH2_CAN_MUSTTAIL)
#define SH2_CHAIN(callee) __attribute__((musttail)) return callee(context)
#else
#define SH2_CHAIN(callee) do { context->next = (callee); return SH2_FUNCTION_TAIL; } while (0)
#endif

/* These adapters expand into a generated body's locals. Arithmetic is shared
   with the catalog operations. Memory effects use the native mapped ranges. */
#define sh2_op_shll(c, i) sh2_op_t(c, sh2_shift_left_one(&regs.r[i], false))
#define sh2_op_shlr(c, i) sh2_op_t(c, sh2_shift_right_one(&regs.r[i], false))
#define sh2_op_shar(c, i) sh2_op_t(c, sh2_shift_right_one(&regs.r[i], regs.r[i] >> 31))
#define sh2_op_rotl(c, i) sh2_op_t(c, sh2_shift_left_one(&regs.r[i], regs.r[i] >> 31))
#define sh2_op_rotr(c, i) sh2_op_t(c, sh2_shift_right_one(&regs.r[i], regs.r[i] & 1))
#define sh2_op_rotcl(c, i) sh2_op_t(c, sh2_shift_left_one(&regs.r[i], regs.sr & SH2_SR_T))
#define sh2_op_rotcr(c, i) sh2_op_t(c, sh2_shift_right_one(&regs.r[i], regs.sr & SH2_SR_T))
#define sh2_op_addc(c, i, j) sh2_op_t(c, sh2_add_with_carry(&regs.r[i], regs.r[j], regs.sr & SH2_SR_T))
#define sh2_op_subc(c, i, j) sh2_op_t(c, sh2_subtract_with_borrow(&regs.r[i], regs.r[j], regs.sr & SH2_SR_T))
#define sh2_op_clrmac(c) (regs.mach = regs.macl = 0)
#define sh2_op_muls_w(c, i, j) (regs.macl = sh2_signed_word_product(regs.r[i], regs.r[j]))
#define sh2_op_mulu_w(c, i, j) (regs.macl = (regs.r[i] & 0xffffu) * (regs.r[j] & 0xffffu))
#define sh2_op_mul_l(c, i, j) do { regs.macl = regs.r[i] * regs.r[j]; ++cost; } while (0)
#define SH2_DOUBLE_PRODUCT(product) do { \
    const uint64_t wide_product = (uint64_t)(product); \
    regs.mach = (uint32_t)(wide_product >> 32); regs.macl = (uint32_t)wide_product; ++cost; \
} while (0)
#define sh2_op_dmuls_l(c, i, j) SH2_DOUBLE_PRODUCT(sh2_signed_operand(regs.r[i], 0x80000000) * \
                                                   sh2_signed_operand(regs.r[j], 0x80000000))
#define sh2_op_dmulu_l(c, i, j) SH2_DOUBLE_PRODUCT((uint64_t)regs.r[i] * regs.r[j])
#define sh2_op_div0(c, q, m) sh2_divide_initialize(&regs.sr, (q), (m))
#define sh2_op_div1(c, i, j) sh2_divide_step(&regs.sr, &regs.r[i], &regs.r[j])

#define sh2_op_mac(c, i, j, n) do { \
    uint32_t left, right; \
    SH2_READ(regs.r[i], n, left); SH2_READ(regs.r[j] + ((i) == (j) ? (n) : 0), n, right); \
    regs.r[i] += (n); regs.r[j] += (n); \
    sh2_accumulate_product(&regs.mach, &regs.macl, regs.sr, left, right, (n)); cost += 2; \
} while (0)
#define sh2_op_mac_w(c, i, j) sh2_op_mac(c, i, j, 2)

#define sh2_op_test_byte(c, addr, mask) do { \
    uint32_t byte; SH2_READ(addr, 1, byte); sh2_op_t(c, (byte & (mask)) == 0); cost += 2; \
} while (0)
#define SH2_MODIFY_BYTE(c, addr, keep, toggle, set_flag) do { \
    const uint32_t byte_address = (addr); \
    uint32_t byte; SH2_READ(byte_address, 1, byte); \
    sh2_op_store(c, byte_address, (byte & (keep)) ^ (toggle), 1); \
    if (set_flag) sh2_op_t(c, byte == 0); \
    cost += (set_flag) ? 3 : 2; \
} while (0)
#define sh2_op_modify_byte(c, addr, opcode, keep, toggle) SH2_MODIFY_BYTE(c, addr, keep, toggle, false)
#define sh2_op_tas_byte(c, i) SH2_MODIFY_BYTE(c, regs.r[i], 0x7f, 0x80, true)

#endif
