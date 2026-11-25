#include "sh2_recomp/function.h"
#include "operation_source.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static uint16_t image_word(const Sh2FunctionImage *image, uint32_t pc)
{
    const uint8_t *bytes = image->bytes + (pc - image->base);
    return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
}

static Sh2Instruction decode(const Sh2FunctionImage *image, uint32_t pc)
{
    return sh2_decode(image_word(image, pc));
}

static bool direct_branch(Sh2Opcode opcode)
{
    return opcode == SH2_OP_BRA || opcode == SH2_OP_BSR || opcode == SH2_OP_BT || opcode == SH2_OP_BF ||
           opcode == SH2_OP_BT_S || opcode == SH2_OP_BF_S;
}

static bool direct_test(Sh2Opcode opcode)
{
    return opcode == SH2_OP_BT || opcode == SH2_OP_BF || opcode == SH2_OP_BT_S || opcode == SH2_OP_BF_S;
}

static bool indirect_branch(Sh2Opcode opcode)
{
    return opcode == SH2_OP_JMP || opcode == SH2_OP_JSR || opcode == SH2_OP_BRAF || opcode == SH2_OP_BSRF;
}

static bool has_indirect_targets(const Sh2FunctionImage *image, uint32_t pc)
{
    const size_t first = sh2_observed_target_begin(image, pc);
    uint32_t literal;
    return (first < image->target_count && image->targets[first].source == pc) ||
           sh2_function_literal_target(image, pc, &literal);
}

static bool call(Sh2Opcode opcode)
{
    return opcode == SH2_OP_BSR || opcode == SH2_OP_JSR || opcode == SH2_OP_BSRF;
}

static bool returns(Sh2Opcode opcode)
{
    return opcode == SH2_OP_RTS || opcode == SH2_OP_RTE;
}

static bool local_handoff(const Sh2FunctionImage *image, uint32_t pc)
{
    for (size_t i = 0; i < image->local_handoff_count; ++i) {
        if (image->local_handoffs[i].source == pc)
            return true;
    }
    return false;
}

static bool local_operation(Sh2Opcode opcode)
{
    return !direct_branch(opcode) && !indirect_branch(opcode) && !returns(opcode) &&
           opcode != SH2_OP_SLEEP && sh2_operation_source(opcode);
}

static bool unlowered(const Sh2FunctionImage *image, const Sh2Instruction op, uint32_t pc, uint32_t *at)
{
    *at = pc;
    if (!direct_branch(op.opcode) && !indirect_branch(op.opcode) && !returns(op.opcode) &&
        op.opcode != SH2_OP_SLEEP && !local_operation(op.opcode))
        return true;
    *at = pc + 2;
    return op.delayed && ((*at - image->base >= image->size) || !local_operation(decode(image, *at).opcode));
}

static bool unobserved(const Sh2FunctionImage *image, uint32_t pc)
{
    const size_t word = ((size_t)pc - image->base) / 2;
    return image->follow_static && !(image->observed[word / 8] & (1u << (word % 8)));
}

static void emit_operation(FILE *out, const Sh2FunctionImage *image, Sh2Instruction op,
                           uint32_t pc, const char *literal)
{
    fprintf(out, "    { enum { rn = %u, rm = %u, immediate = %d, displacement = %d };\n"
                 "      const uint32_t op_pc = SH2_PC(0x%08" PRIx32 ");\n"
                 "      const uint32_t literal_pc = %s;\n      (void)op_pc; (void)literal_pc;\n",
            (unsigned)op.rn, (unsigned)op.rm, op.immediate, op.displacement, pc, literal);
    const char *operation = sh2_operation_source(op.opcode);
    if (op.opcode == SH2_OP_CMP_HS || op.opcode == SH2_OP_CMP_HI ||
        op.opcode == SH2_OP_CMP_GE || op.opcode == SH2_OP_CMP_GT)
        fprintf(out, "      SH2_BIND_COMPARE(0x%08" PRIx32 ", 0x%04x, %s);\n",
                pc, (unsigned)image_word(image, pc), operation);
    else
        fprintf(out, "      %s;\n", operation);
    fputs("      ++cost; ++instructions; }\n", out);
}

typedef struct {
    const Sh2FunctionImage *image;
    const Sh2FunctionGraph *graph;
    const char *name;
    uint32_t entry;
    uint8_t *local;
    uint32_t *queue;
    size_t queued;
} Body;

static bool enqueue(Body *body, uint32_t pc, bool tail)
{
    if (pc & 1)
        return false;
    if (pc - body->image->base >= body->image->size)
        return tail;
    const size_t i = (pc - body->image->base) / 2;
    if (!(body->graph->flags[i] & SH2_FUNCTION_REACHED))
        return true;
    if (tail && pc != body->entry && (body->graph->flags[i] & SH2_FUNCTION_ENTRY))
        return true;
    if (!body->local[i]) {
        body->local[i] = 1;
        body->queue[body->queued++] = pc;
    }
    return true;
}

static bool build_body(Body *body, FILE *errors)
{
    enqueue(body, body->entry, false);
    for (size_t i = 0; i < body->queued; ++i) {
        const uint32_t pc = body->queue[i];
        const Sh2Instruction op = decode(body->image, pc);
        const uint32_t fallthrough = pc + (op.delayed ? 4 : 2);
        const uint32_t target = pc + 4 + (uint32_t)op.displacement;
        const bool indirect = indirect_branch(op.opcode);
        uint32_t unlowered_pc;
        if (unlowered(body->image, op, pc, &unlowered_pc)) {
            if (unobserved(body->image, pc))
                continue;
            fprintf(errors, "No function-native lowering at %08" PRIx32 "\n", unlowered_pc);
            return false;
        }
        if (indirect) {
            const uint32_t mask = body->image->alias_mask;
            const size_t first = sh2_observed_target_begin(body->image, pc);
            uint32_t literal;
            const bool recovered = sh2_function_literal_target(body->image, pc, &literal);
            /* Calls look up bound function entries. Jumps can also target
               labels inside this body, so their successor list is required. */
            if (!call(op.opcode) && !recovered && !unobserved(body->image, pc) &&
                (first == body->image->target_count || body->image->targets[first].source != pc)) {
                fprintf(errors, "No observed indirect target for function-native transfer at %08" PRIx32 "\n", pc);
                return false;
            }
            if (recovered && !enqueue(body, literal & ~mask, true))
                return false;
            for (size_t j = first; j < body->image->target_count && body->image->targets[j].source == pc; ++j)
                if (!enqueue(body, body->image->targets[j].target & ~mask, true))
                    return false;
        }
        if ((direct_branch(op.opcode) && !enqueue(body, target, true)) ||
            (op.opcode != SH2_OP_BRA && !returns(op.opcode) &&
             (!indirect || call(op.opcode)) && !enqueue(body, fallthrough, true))) {
            fprintf(errors, "Unbound function-native successor at %08" PRIx32 "\n", pc);
            return false;
        }
    }
    return true;
}

static void emit_idle_poll(FILE *out, const Body *body, Sh2Instruction branch, uint32_t pc, uint32_t target)
{
    const Sh2FunctionImage *image = body->image;
    const uint32_t offset = target - image->base;
    if (target >= pc || pc - target < 4 || pc - target > 6 || offset >= image->size || !body->local[offset / 2])
        return;

    const Sh2Instruction load = decode(image, target);
    const unsigned size = load.opcode == SH2_OP_MOV_B_LOAD ? 1 : load.opcode == SH2_OP_MOV_W_LOAD ? 2 : 0;
    if (!size || load.rn == load.rm)
        return;
    uint32_t mask = size == 1 ? UINT32_C(0xff) : UINT32_C(0xffff);
    if (pc - target == 6) {
        const Sh2Instruction narrow = decode(image, target + 2);
        if (narrow.opcode == SH2_OP_AND_IMM && load.rn == 0)
            mask &= (uint32_t)narrow.immediate;
        else if (narrow.opcode != (size == 1 ? SH2_OP_EXTU_B : SH2_OP_EXTU_W) ||
                 narrow.rm != load.rn || narrow.rn != load.rn)
            return;
    }
    const Sh2Instruction test = decode(image, pc - 2);
    if (test.opcode == SH2_OP_TST_IMM && load.rn == 0)
        mask &= (uint32_t)test.immediate;
    else if (test.opcode != SH2_OP_TST_REG || test.rn != load.rn || test.rm != load.rn)
        return;
    if (branch.delayed) {
        const Sh2Instruction slot = decode(image, pc + 2);
        if (slot.opcode != SH2_OP_NOP && (slot.opcode != SH2_OP_MOV_IMM || slot.rn != load.rn))
            return;
    }

    /* This loop loads one stable address and only writes scratch state. */
    const bool wait_while_zero = branch.opcode == SH2_OP_BT || branch.opcode == SH2_OP_BT_S;
    if (mask)
        fprintf(out, "    SH2_IDLE(regs.r[%u], %u, UINT32_C(0x%04" PRIx32 "), %s);\n",
                (unsigned)load.rm, size, mask, wait_while_zero ? "true" : "false");
}

static void emit_jump(FILE *out, const Body *body, uint32_t pc, uint32_t target)
{
    const uint32_t offset = target - body->image->base;
    if (offset >= body->image->size) {
        fprintf(out, "    const uint32_t target = SH2_PC(0x%08" PRIx32 ");\n"
                     "    SH2_TAIL(target, 0x%08" PRIx32 ", %s);\n", target, pc, body->name);
    } else if (body->local[offset / 2]) {
        if (target <= pc)
            fprintf(out, "    SH2_SAFE(SH2_PC(0x%08" PRIx32 "));\n", target);
        fprintf(out, "    goto pc_%08" PRIx32 ";\n", target);
    } else if (body->graph->flags[offset / 2] & SH2_FUNCTION_ENTRY) {
        fprintf(out, "    SH2_PUBLISH(SH2_PC(0x%08" PRIx32 ")); SH2_CHAIN(fn_%08" PRIx32 ");\n", target, target);
    } else {
        fprintf(out, "    SH2_UNBOUND(0x%08" PRIx32 ", SH2_PC(0x%08" PRIx32 "));\n", pc, target);
    }
}

static void emit_indirect_case(FILE *out, const Body *body, uint32_t pc, uint32_t target)
{
    const Sh2FunctionImage *image = body->image;
    fprintf(out, "    case 0x%08" PRIx32 ": {\n", target);
    if ((target & ~image->alias_mask) - image->base < image->size) {
        fprintf(out, "    pc_bias = 0x%08" PRIx32 ";\n", target & image->alias_mask);
        emit_jump(out, body, pc, target & ~image->alias_mask);
    } else {
        fprintf(out, "    SH2_TAIL(destination, 0x%08" PRIx32 ", %s);\n", pc, body->name);
    }
    fputs("    }\n", out);
}

static void emit_indirect_jump(FILE *out, const Body *body, uint32_t pc)
{
    const Sh2FunctionImage *image = body->image;
    fputs("    switch (destination) {\n", out);
    const size_t first = sh2_observed_target_begin(image, pc);
    uint32_t literal;
    bool has_literal = sh2_function_literal_target(image, pc, &literal);
    for (size_t j = first; j < image->target_count && image->targets[j].source == pc; ++j) {
        const uint32_t target = image->targets[j].target;
        if (j > first && target == image->targets[j - 1].target)
            continue;
        emit_indirect_case(out, body, pc, target);
        if (has_literal && literal == target)
            has_literal = false;
    }
    if (has_literal)
        emit_indirect_case(out, body, pc, literal);
    fprintf(out, "    default: SH2_UNBOUND(0x%08" PRIx32 ", destination);\n    }\n", pc);
}

static bool linear_byte_copy(const Body *body)
{
    /* r4 = destination, r5 = source, r6 = count. Both pointers advance;
       the return delay slot is a NOP. */
    static const uint16_t code[] = {
        0x6143, 0x6053, 0x6263, 0x6304, 0x2130, 0x4210, 0x8ffb, 0x7101, 0x000b, 0x0009
    };
    const size_t offset = body->entry - body->image->base;
    if (sizeof(code) > body->image->size - offset)
        return false;
    for (unsigned i = 0; i < sizeof(code) / sizeof(*code); ++i)
        if (image_word(body->image, body->entry + 2 * i) != code[i])
            return false;
    return true;
}

static void emit_body(FILE *out, const Body *body)
{
    fprintf(out, "static Sh2FunctionResult fn_%08" PRIx32 "(Sh2FunctionContext *context)\n{\n", body->entry);
    if (linear_byte_copy(body))
        fprintf(out, "    SH2_BIND_LINEAR_COPY(0x%08" PRIx32 ");\n", body->entry);
    fprintf(out, "    Sh2FunctionRegisters regs = context->registers;\n"
                 "    Sh2FunctionRegisters *state = &regs;\n"
                 "    (void)state;\n    uint64_t cost = 0, instructions = 0;\n"
                 "    uint32_t pc_bias = context->pc & 0x%08" PRIx32 ";\n"
                 "    SH2_SAFE(SH2_PC(0x%08" PRIx32 ")); goto pc_%08" PRIx32 ";\n",
            body->image->alias_mask, body->entry, body->entry);
    for (size_t i = 0; i < body->graph->words; ++i) {
        if (!body->local[i])
            continue;
        const uint32_t pc = body->image->base + (uint32_t)(i * 2);
        const Sh2Instruction op = decode(body->image, pc);
        const uint32_t next = pc + (op.delayed ? 4 : 2);
        const uint32_t branch = pc + 4 + (uint32_t)op.displacement;
        const bool test = direct_test(op.opcode);
        const bool indirect = indirect_branch(op.opcode);
        fprintf(out, "pc_%08" PRIx32 ": {\n", pc);
        char literal[40];
        uint32_t unlowered_pc;
        if (unlowered(body->image, op, pc, &unlowered_pc)) {
            fprintf(out, "    SH2_PUBLISH(SH2_PC(0x%08" PRIx32 ")); return SH2_FUNCTION_UNIMPLEMENTED;\n}\n",
                    unlowered_pc);
            continue;
        }
        if (op.opcode == SH2_OP_SLEEP) {
            fprintf(out, "    cost += 3; ++instructions;\n"
                         "    SH2_PUBLISH(SH2_PC(0x%08" PRIx32 ")); context->sleeping = true;\n"
                         "    if (!context->service || !context->service(context)) return SH2_FUNCTION_EXIT;\n"
                         "    regs = context->registers;\n", next);
            emit_jump(out, body, pc, next);
            fputs("}\n", out);
            continue;
        }
        if (!direct_branch(op.opcode) && !indirect && !returns(op.opcode)) {
            snprintf(literal, sizeof(literal), "SH2_PC(0x%08" PRIx32 ")", pc + 4);
            emit_operation(out, body->image, op, pc, literal);
            emit_jump(out, body, pc, next);
            fputs("}\n", out);
            continue;
        }
        if (test) {
            const bool on_true = op.opcode == SH2_OP_BT || op.opcode == SH2_OP_BT_S;
            fprintf(out, "    const bool taken = (regs.sr & 1) %s 0;\n", on_true ? "!=" : "==");
        }
        if (op.opcode == SH2_OP_RTS)
            fputs("    const uint32_t destination = regs.pr;\n", out);
        else if (op.opcode == SH2_OP_RTE)
            fprintf(out, "    const uint32_t op_pc = SH2_PC(0x%08" PRIx32 ");\n"
                         "    uint32_t destination, restored_sr;\n"
                         "    SH2_READ(regs.r[15], 4, destination);\n"
                         "    SH2_READ(regs.r[15] + 4, 4, restored_sr);\n"
                         "    regs.r[15] += 8; regs.sr = restored_sr & SH2_SR_BITS; cost += 2;\n", pc);
        else if (op.opcode == SH2_OP_BRAF || op.opcode == SH2_OP_BSRF)
            fprintf(out, "    const uint32_t destination = SH2_PC(0x%08" PRIx32 ") + regs.r[%u];\n", pc + 4, (unsigned)op.rm);
        else if (op.opcode == SH2_OP_JMP)
            fprintf(out, "    const uint32_t destination = SH2_BIND_JUMP(0x%08" PRIx32 ", 0x%04x, regs.r[%u]);\n",
                    pc, (unsigned)image_word(body->image, pc), (unsigned)op.rm);
        else if (indirect)
            fprintf(out, "    const uint32_t destination = regs.r[%u];\n", (unsigned)op.rm);
        else if (op.delayed && test)
            fprintf(out, "    const uint32_t destination = SH2_PC(taken ? 0x%08" PRIx32 " : 0x%08" PRIx32 ");\n", branch, next);
        else if (op.delayed)
            fprintf(out, "    const uint32_t destination = SH2_PC(0x%08" PRIx32 ");\n", branch);
        if (call(op.opcode))
            fprintf(out, "    regs.pr = SH2_PC(0x%08" PRIx32 ");\n", next);
        fputs(test ? (op.delayed ? "    cost += taken ? 2 : 1;\n" : "    cost += taken ? 3 : 1;\n") :
                     "    cost += 2;\n", out);
        fputs("    ++instructions;\n", out);
        if (op.delayed)
            emit_operation(out, body->image, decode(body->image, pc + 2), pc + 2, "destination + 2");
        if (indirect && !call(op.opcode) && !has_indirect_targets(body->image, pc)) {
            fprintf(out, "    SH2_UNBOUND(0x%08" PRIx32 ", destination);\n", pc);
        } else if (returns(op.opcode)) {
            fputs("    SH2_PUBLISH(destination); return SH2_FUNCTION_RETURNED;\n", out);
        } else if (call(op.opcode)) {
            const uint32_t branch_offset = branch - body->image->base;
            const bool direct_local = !indirect && branch_offset < body->image->size &&
                                      (body->graph->flags[branch_offset / 2] & SH2_FUNCTION_ENTRY);
            if (direct_local)
                fprintf(out, "    const Sh2Function callee = fn_%08" PRIx32 ";\n", branch);
            else
                fprintf(out, "    SH2_RESOLVE(callee, destination, 0x%08" PRIx32 ", %s);\n", pc, body->name);
            if (local_handoff(body->image, pc)) {
                fprintf(out, "    if (context->restart_transfer && context->restart_transfer(context->user, "
                             "&%s_image, SH2_PC(0x%08" PRIx32 "), destination)) {\n"
                             "        SH2_PUBLISH(destination); return SH2_FUNCTION_RESTART;\n    }\n",
                        body->name, pc);
            } else if (!direct_local) {
                fprintf(out, "    if (((destination & ~UINT32_C(0x%08" PRIx32 ")) - UINT32_C(0x%08" PRIx32 ")) >= %zu &&\n"
                             "        context->restart_transfer && context->restart_transfer(context->user, "
                             "&%s_image, SH2_PC(0x%08" PRIx32 "), destination)) {\n"
                             "        SH2_PUBLISH(destination); return SH2_FUNCTION_RESTART;\n    }\n",
                        body->image->alias_mask, body->image->base, body->image->size, body->name, pc);
            }
            fprintf(out, "    SH2_PUBLISH(destination);\n"
                         "    const Sh2FunctionResult result = sh2_function_finish_tails(context, callee(context));\n"
                         "    if (result != SH2_FUNCTION_RETURNED || context->pc != SH2_PC(0x%08" PRIx32 ")) return result;\n"
                         "    regs = context->registers;\n", next);
            emit_jump(out, body, pc, next);
        } else if (indirect) {
            emit_indirect_jump(out, body, pc);
        } else if (test) {
            fputs("    if (taken) {\n", out);
            emit_idle_poll(out, body, op, pc, branch);
            emit_jump(out, body, pc, branch);
            fputs("    }\n", out);
            emit_jump(out, body, pc, next);
        } else {
            emit_jump(out, body, pc, branch);
        }
        fputs("}\n", out);
    }
    fputs("}\n\n", out);
}

static void emit_support(FILE *out)
{
    fputs("#include \"sh2_recomp/function.h\"\n#include \"sh2_recomp/operations.h\"\n"
          "#include \"sh2_recomp/function_operations.h\"\n\n"
          "#define SH2_PC(at) ((at) + pc_bias)\n"
          "#define SH2_COST() do { context->pending_cost += cost; context->pending_instructions += instructions; context->fuel = cost >= context->fuel ? 0 : context->fuel - cost; cost = 0; instructions = 0; } while (0)\n"
          "#define SH2_PUBLISH(at) do { SH2_COST(); context->pc = (at); context->registers = regs; } while (0)\n"
          "#define SH2_SAFE(at) do { SH2_COST(); if (!context->fuel && context->service) { SH2_PUBLISH(at); if (!context->service(context)) return SH2_FUNCTION_EXIT; regs = context->registers; } } while (0)\n"
          "#define SH2_IDLE(address, size, mask, zero) do { SH2_COST(); if (context->fuel && context->service && context->idle_poll && context->idle_poll(context, (address), (size), (mask), (zero))) { context->pending_cost += context->fuel; context->fuel = 0; } } while (0)\n"
          "#define SH2_BUS_FAULT() do { SH2_PUBLISH(op_pc); return SH2_FUNCTION_FAULT; } while (0)\n"
          "#define SH2_UNBOUND(at, target) do { SH2_PUBLISH(SH2_PC(at)); context->unbound_target = (target); context->has_unbound_target = true; return SH2_FUNCTION_FAULT; } while (0)\n"
          "#define SH2_RESOLVE(callee, target, at, name) const Sh2Function callee = name##_lookup(context, (target)); if (!callee) SH2_UNBOUND(at, target)\n"
          "#define SH2_TAIL(target, at, name) do { SH2_RESOLVE(callee, target, at, name); if (context->tail_transfer) context->tail_transfer(context->user, &name##_image, SH2_PC(at), (target)); SH2_PUBLISH(target); SH2_CHAIN(callee); } while (0)\n"
          "#define sh2_op_register(c, i, v) (regs.r[i] = (v))\n"
          "#define sh2_op_t(c, v) (regs.sr = (regs.sr & ~1u) | ((v) ? 1u : 0u))\n"
          "#define sh2_literal_pc(s) literal_pc\n"
          "#define sh2_op_dt(c, i) sh2_op_t(c, --regs.r[i] == 0)\n"
          "#define sh2_op_control(c, d, v) (*(d) = (v))\n"
          "#define sh2_retire(c, n) ((void)0)\n"
          "#define SH2_READ(a, n, value) do { const uint32_t address = (a); if (address & ((n) - 1)) SH2_BUS_FAULT(); if (!sh2_function_mapped_read(context, address, (n), &(value))) { SH2_COST(); if (!context->memory || !context->memory(context->user, address, (n), false, &(value))) SH2_BUS_FAULT(); } } while (0)\n"
          "#define sh2_op_load(c, i, a, n) do { uint32_t value; SH2_READ(a, n, value); regs.r[i] = sh2_extend_signed(value, (n)); } while (0)\n"
          "#define sh2_op_store(c, a, v, n) do { const uint32_t address = (a); uint32_t value = (v); if (address & ((n) - 1)) SH2_BUS_FAULT(); if (!sh2_function_mapped_write(context, address, (n), value)) { SH2_COST(); if (!context->memory || !context->memory(context->user, address, (n), true, &value)) SH2_BUS_FAULT(); } } while (0)\n"
          "#define sh2_op_mov_l_pc(c, i, d) sh2_op_load(c, i, (literal_pc & ~3u) + (d), 4)\n"
          "#define sh2_op_mov_w_pc(c, i, d) sh2_op_load(c, i, literal_pc + (d), 2)\n"
          "#define sh2_op_load_postinc(c, i, j, n) do { sh2_op_load(c, i, regs.r[j], n); if (i != j) regs.r[j] += n; } while (0)\n"
          "#define sh2_op_mov_l_postinc(c, i, j) sh2_op_load_postinc(c, i, j, 4)\n"
          "#define SH2_PUSH(c, i, v, n) do { const uint32_t dst = regs.r[i] - (n); sh2_op_store(c, dst, v, n); regs.r[i] = dst; } while (0)\n"
          "#define sh2_op_store_predec(c, i, j, n) SH2_PUSH(c, i, regs.r[j], n)\n"
          "#define sh2_op_sts_l(c, i, v) SH2_PUSH(c, i, v, 4)\n"
          "#define sh2_op_sts_l_pr(c, i) SH2_PUSH(c, i, regs.pr, 4)\n"
          "#define sh2_op_stc_l(c, i, v) do { SH2_PUSH(c, i, v, 4); ++cost; } while (0)\n"
          "#define sh2_op_ldc_l(c, i, d, mask) do { uint32_t raw; SH2_READ(regs.r[i], 4, raw); *(d) = raw & (mask); regs.r[i] += 4; cost += 2; } while (0)\n"
          "#define sh2_op_lds_l(c, i, d) do { uint32_t raw; SH2_READ(regs.r[i], 4, raw); *(d) = raw; regs.r[i] += 4; } while (0)\n"
          "#define sh2_op_lds_l_pr(c, i) sh2_op_lds_l(c, i, &regs.pr)\n\n", out);
    fputs("#ifdef SH2_FUNCTION_BINDINGS_HEADER\n"
          "#include SH2_FUNCTION_BINDINGS_HEADER\n"
          "#endif\n"
          "#ifndef SH2_BIND_COMPARE\n"
          "#define SH2_BIND_COMPARE(pc, op, original) original\n"
          "#endif\n"
          "#ifndef SH2_BIND_JUMP\n"
          "#define SH2_BIND_JUMP(pc, op, original) (original)\n"
          "#endif\n"
          "#ifndef SH2_BIND_LINEAR_COPY\n"
          "#define SH2_BIND_LINEAR_COPY(pc) ((void)0)\n"
          "#endif\n\n", out);
}

static void emit_index(FILE *out, const char *name, const Sh2FunctionImage *image, const Sh2FunctionGraph *graph)
{
    size_t first = graph->words, last = 0;
    for (size_t i = 0; i < graph->words; ++i) {
        if (!(graph->flags[i] & SH2_FUNCTION_ENTRY))
            continue;
        if (first == graph->words)
            first = i;
        last = i;
    }
    fprintf(out, "\nstatic const Sh2Function %s_index[] = {\n", name);
    for (size_t i = first; i <= last; ++i)
        if (graph->flags[i] & SH2_FUNCTION_ENTRY)
            fprintf(out, "    [%zu] = fn_%08" PRIx32 ",\n", i - first, image->base + (uint32_t)i * 2);
    fprintf(out, "};\n\nstatic const Sh2FunctionCodeRange %s_code[] = {\n", name);
    size_t range_count = 0;
    uint64_t hash = SH2_FUNCTION_HASH_SEED;
    for (size_t i = 0; i < graph->words;) {
        const unsigned code_flags = SH2_FUNCTION_REACHED | SH2_FUNCTION_DELAY_SLOT;
        if (!(graph->flags[i] & code_flags)) {
            ++i;
            continue;
        }
        const size_t start = i++;
        while (i < graph->words && (graph->flags[i] & code_flags))
            ++i;
        fprintf(out, "    {%zu, %zu},\n", start * 2, (i - start) * 2);
        hash = sh2_function_hash_bytes(hash, image->bytes + start * 2, (i - start) * 2);
        ++range_count;
    }
    fprintf(out, "};\n\nconst Sh2FunctionCode %s_image = {\n"
                 "    .base = 0x%08" PRIx32 ", .size = %zu, .alias_mask = 0x%08" PRIx32 ",\n"
                 "    .hash = UINT64_C(0x%016" PRIx64 "), .code_hash = UINT64_C(0x%016" PRIx64 "),\n"
                 "    .first_entry = 0x%08" PRIx32 ", .index_words = %zu, .index = %s_index,\n"
                 "    .code = %s_code, .code_count = %zu,\n};\n\n"
                 "#define %s_lookup(context, pc) sh2_function_lookup_or_resolve(&%s_image, context, pc)\n\n",
            name, image->base, image->size, image->alias_mask,
            sh2_function_hash_bytes(SH2_FUNCTION_HASH_SEED, image->bytes, image->size), hash,
            image->base + (uint32_t)first * 2, last - first + 1, name, name, range_count, name, name);
}

bool sh2_emit_functions(FILE *out, FILE *errors, const char *name, const Sh2FunctionImage *image)
{
    if (!*name || (*name >= '0' && *name <= '9') ||
        name[strspn(name, "_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")])
        return false;
    if ((image->local_handoff_count && !image->local_handoffs) ||
        (image->follow_static && (!image->observed || image->observed_only)))
        return false;
    Sh2FunctionGraph graph;
    if (!sh2_function_graph_build(image, &graph)) {
        fputs("Invalid SH-2 function image or entry evidence\n", errors);
        return false;
    }
    for (size_t i = 0; i < image->local_handoff_count; ++i) {
        const Sh2ObservedTarget *handoff = &image->local_handoffs[i];
        const uint32_t target = handoff->target & ~image->alias_mask;
        if (((handoff->source | target) & 1) || handoff->source - image->base >= image->size ||
            target - image->base >= image->size ||
            !(graph.flags[(handoff->source - image->base) / 2] & SH2_FUNCTION_REACHED) ||
            !call(decode(image, handoff->source).opcode)) {
            fprintf(errors, "Invalid local SH-2 call handoff at %08" PRIx32 "\n", handoff->source);
            sh2_function_graph_destroy(&graph);
            return false;
        }
    }
    if (image->observed) {
        fprintf(errors, "%s: %zu functions, %zu observed words outside discovered bodies, "
                        "%zu sources with unobserved successors, %zu external transfer sources\n",
                name, graph.functions, graph.uncovered, graph.unobserved_edges, graph.external);
    }
    if (image->follow_static) {
        for (size_t i = 0; i < graph.words; ++i) {
            if (!(graph.flags[i] & SH2_FUNCTION_REACHED))
                continue;
            const uint32_t pc = image->base + (uint32_t)(i * 2);
            const Sh2Instruction op = decode(image, pc);
            uint32_t unlowered_pc;
            if (unlowered(image, op, pc, &unlowered_pc))
                fprintf(errors, "No native instruction at %08" PRIx32 "; execution stops if reached\n", unlowered_pc);
            else if (indirect_branch(op.opcode) && !has_indirect_targets(image, pc)) {
                if (call(op.opcode))
                    fprintf(errors, "Indirect call at %08" PRIx32 " has no recorded target; using bound native entries\n", pc);
                else
                    fprintf(errors, "No verified indirect jump target at %08" PRIx32 "; execution stops if reached\n", pc);
            }
        }
    }
    uint8_t *local = malloc(graph.words);
    uint32_t *queue = malloc(graph.words * sizeof(*queue));
    bool ok = local && queue;
    for (unsigned pass = 0; ok && pass < 2; ++pass) {
        if (pass) {
            emit_support(out);
            for (size_t i = 0; i < graph.words; ++i) {
                if (!(graph.flags[i] & SH2_FUNCTION_ENTRY))
                    continue;
                const uint32_t pc = image->base + (uint32_t)i * 2;
                fprintf(out, "#define fn_%08" PRIx32 " %s_fn_%08" PRIx32 "\n"
                             "static Sh2FunctionResult fn_%08" PRIx32 "(Sh2FunctionContext *context);\n",
                        pc, name, pc, pc);
            }
            emit_index(out, name, image, &graph);
        }
        for (size_t i = 0; ok && i < graph.words; ++i) {
            if (!(graph.flags[i] & SH2_FUNCTION_ENTRY))
                continue;
            memset(local, 0, graph.words);
            Body body = {image, &graph, name, image->base + (uint32_t)i * 2, local, queue, 0};
            ok = build_body(&body, errors);
            if (ok && pass)
                emit_body(out, &body);
        }
    }
    if (ok) {
        fprintf(out, "const Sh2FunctionEntry %s[] = {\n", name);
        for (size_t i = 0; i < graph.words; ++i)
            if (graph.flags[i] & SH2_FUNCTION_ENTRY) {
                const uint32_t pc = image->base + (uint32_t)i * 2;
                fprintf(out, "    {0x%08" PRIx32 ", fn_%08" PRIx32 "},\n", pc, pc);
            }
        fprintf(out, "};\nconst size_t %s_count = %zu;\n", name, graph.functions);
    }
    free(local);
    free(queue);
    sh2_function_graph_destroy(&graph);
    return ok && !ferror(out);
}
