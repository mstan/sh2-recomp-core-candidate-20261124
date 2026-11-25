#include "sh2_recomp/operations.h"

#include <string.h>

static Sh2StepResult bus_result(Sh2BusResult result)
{
    switch (result) {
    case SH2_BUS_OK: return SH2_STEP_OK;
    case SH2_BUS_WAIT: return SH2_STEP_WAIT;
    default: return SH2_STEP_BUS_FAULT;
    }
}

static Sh2StepResult read_data(Sh2Context *context, uint32_t address,
                               unsigned size, uint32_t *value)
{
    if (address & (size - 1))
        return SH2_STEP_ADDRESS_ERROR;
    if (!context->bus.read)
        return SH2_STEP_BUS_FAULT;
    return bus_result(context->bus.read(context->user, address, size, value));
}

void sh2_init(Sh2Context *context, const Sh2Bus *bus, void *user)
{
    memset(context, 0, sizeof(*context));
    context->bus = *bus;
    context->user = user;
}

Sh2StepResult sh2_power_on(Sh2Context *context)
{
    Sh2State next = {0};
    Sh2StepResult result = read_data(context, 0, 4, &next.pc);
    if (result != SH2_STEP_OK)
        return result;
    result = read_data(context, 4, 4, &next.r[15]);
    if (result != SH2_STEP_OK)
        return result;
    next.sr = 0xF0;
    context->state = next;
    context->interrupt_stage = 0;
    context->mac_opcode = 0;
    context->byte_write_opcode = 0;
    return SH2_STEP_OK;
}

Sh2StepResult sh2_fetch16(Sh2Context *context, uint16_t *word)
{
    const uint32_t pc = context->state.pc;
    if (pc & 1)
        return SH2_STEP_ADDRESS_ERROR;
    if (context->mac_opcode) {
        if (context->mac_pc == pc) {
            *word = context->mac_opcode;
            return SH2_STEP_OK;
        }
        context->mac_opcode = 0;
    }
    if (context->byte_write_opcode) {
        if (context->byte_write_pc == pc) {
            *word = context->byte_write_opcode;
            return SH2_STEP_OK;
        }
        context->byte_write_opcode = 0;
    }
    if (!context->bus.fetch16)
        return SH2_STEP_BUS_FAULT;
    return bus_result(context->bus.fetch16(context->user, pc, word));
}

Sh2NativeOp sh2_native_lookup(const Sh2Catalog *catalog, uint32_t pc, uint16_t word)
{
    if (!catalog)
        return NULL;
    size_t first = 0;
    size_t last = catalog->count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        const Sh2NativeEntry *entry = &catalog->entries[middle];
        if (entry->pc < pc)
            first = middle + 1;
        else if (entry->pc > pc)
            last = middle;
        else
            return entry->word == word ? entry->execute : NULL;
    }
    return NULL;
}

Sh2StepResult sh2_step_native(Sh2Context *context, const Sh2Catalog *catalog)
{
    if (context->state.sleeping)
        return SH2_STEP_WAIT;
    uint16_t word;
    Sh2StepResult result = sh2_fetch16(context, &word);
    if (result != SH2_STEP_OK)
        return result;
    Sh2NativeOp execute = sh2_native_lookup(catalog, context->state.pc, word);
    return execute ? execute(context) : SH2_STEP_NATIVE_MISS;
}

Sh2StepResult sh2_op_load(Sh2Context *context, unsigned rn, uint32_t address,
                          unsigned size)
{
    uint32_t value;
    Sh2StepResult result = read_data(context, address, size, &value);
    if (result != SH2_STEP_OK)
        return result;
    return sh2_op_register(context, rn, sh2_extend_signed(value, size));
}

Sh2StepResult sh2_op_mov_l_pc(Sh2Context *context, unsigned rn, uint32_t displacement)
{
    const uint32_t base = sh2_literal_pc(&context->state) & ~UINT32_C(3);
    return sh2_op_load(context, rn, base + displacement, 4);
}

Sh2StepResult sh2_op_mov_w_pc(Sh2Context *context, unsigned rn, uint32_t displacement)
{
    return sh2_op_load(context, rn, sh2_literal_pc(&context->state) + displacement, 2);
}

Sh2StepResult sh2_op_load_postinc(Sh2Context *context, unsigned rn, unsigned rm, unsigned size)
{
    uint32_t value;
    Sh2StepResult result = read_data(context, context->state.r[rm], size, &value);
    if (result != SH2_STEP_OK)
        return result;
    if (rn != rm)
        context->state.r[rm] += size;
    return sh2_op_register(context, rn, sh2_extend_signed(value, size));
}

Sh2StepResult sh2_op_mov_l_postinc(Sh2Context *context, unsigned rn, unsigned rm)
{
    return sh2_op_load_postinc(context, rn, rm, 4);
}

Sh2StepResult sh2_op_mac(Sh2Context *context, unsigned rn, unsigned rm, unsigned size)
{
    Sh2State *state = &context->state;
    const bool wide = size == 4;
    const uint16_t opcode = (uint16_t)((wide ? 0x000fu : 0x400fu) | (rn << 8) | (rm << 4));
    uint32_t first = context->mac_first_operand;
    if (context->mac_opcode != opcode || context->mac_pc != state->pc) {
        context->mac_opcode = 0;
        const Sh2StepResult result = read_data(context, state->r[rn], size, &first);
        if (result != SH2_STEP_OK)
            return result;
    }

    /* The second read sees the first post-increment when the registers alias.
     * Retain the first bus transaction if the second device has to wait. */
    uint32_t second;
    const uint32_t address = state->r[rm] + (rn == rm ? size : 0u);
    const Sh2StepResult result = read_data(context, address, size, &second);
    if (result == SH2_STEP_WAIT) {
        context->mac_pc = state->pc;
        context->mac_opcode = opcode;
        context->mac_first_operand = first;
        return result;
    }
    context->mac_opcode = 0;
    if (result != SH2_STEP_OK)
        return result;

    state->r[rn] += size;
    state->r[rm] += size;
    sh2_accumulate_product(&state->mach, &state->macl, state->sr, first, second, size);
    return sh2_retire(context, 3);
}

static Sh2StepResult load_control_postinc(Sh2Context *context, unsigned rm, uint32_t *destination,
                                         uint32_t mask, unsigned cycles)
{
    uint32_t value;
    Sh2StepResult result = read_data(context, context->state.r[rm], 4, &value);
    if (result != SH2_STEP_OK)
        return result;
    context->state.r[rm] += 4;
    *destination = value & mask;
    sh2_retire(context, cycles);
    context->state.irq_inhibit = true;
    return SH2_STEP_OK;
}

Sh2StepResult sh2_op_lds_l(Sh2Context *context, unsigned rm, uint32_t *destination)
{
    return load_control_postinc(context, rm, destination, UINT32_MAX, 1);
}

Sh2StepResult sh2_op_ldc_l(Sh2Context *context, unsigned rm, uint32_t *destination, uint32_t mask)
{
    return load_control_postinc(context, rm, destination, mask, 3);
}

Sh2StepResult sh2_op_lds_l_pr(Sh2Context *context, unsigned rm)
{
    return sh2_op_lds_l(context, rm, &context->state.pr);
}

static Sh2StepResult write_data(Sh2Context *context, uint32_t address, unsigned size,
                                uint32_t value)
{
    if (address & (size - 1))
        return SH2_STEP_ADDRESS_ERROR;
    if (!context->bus.write)
        return SH2_STEP_BUS_FAULT;
    if (size < 4)
        value &= (UINT32_C(1) << (8 * size)) - 1;
    return bus_result(context->bus.write(context->user, address, size, value));
}

Sh2StepResult sh2_op_store(Sh2Context *context, uint32_t address, uint32_t value,
                           unsigned size)
{
    Sh2StepResult result = write_data(context, address, size, value);
    return result == SH2_STEP_OK ? sh2_retire(context, 1) : result;
}

static Sh2StepResult store_predec(Sh2Context *context, unsigned rn, uint32_t value,
                                  unsigned size, unsigned cycles)
{
    const uint32_t address = context->state.r[rn] - size;
    Sh2StepResult result = write_data(context, address, size, value);
    if (result != SH2_STEP_OK)
        return result;
    context->state.r[rn] = address;
    return sh2_retire(context, cycles);
}

Sh2StepResult sh2_op_store_predec(Sh2Context *context, unsigned rn, unsigned rm, unsigned size)
{
    return store_predec(context, rn, context->state.r[rm], size, 1);
}

static Sh2StepResult store_control_predec(Sh2Context *context, unsigned rn, uint32_t value,
                                         unsigned cycles)
{
    Sh2StepResult result = store_predec(context, rn, value, 4, cycles);
    if (result == SH2_STEP_OK)
        context->state.irq_inhibit = true;
    return result;
}

Sh2StepResult sh2_op_sts_l(Sh2Context *context, unsigned rn, uint32_t value)
{
    return store_control_predec(context, rn, value, 1);
}

Sh2StepResult sh2_op_stc_l(Sh2Context *context, unsigned rn, uint32_t value)
{
    return store_control_predec(context, rn, value, 2);
}

Sh2StepResult sh2_op_sts_l_pr(Sh2Context *context, unsigned rn)
{
    return sh2_op_sts_l(context, rn, context->state.pr);
}

Sh2StepResult sh2_op_test_byte(Sh2Context *context, uint32_t address, uint8_t mask)
{
    uint32_t value;
    Sh2StepResult result = read_data(context, address, 1, &value);
    if (result != SH2_STEP_OK)
        return result;
    Sh2State *state = &context->state;
    state->sr = (state->sr & ~(uint32_t)SH2_SR_T) | ((value & mask) == 0);
    return sh2_retire(context, 3);
}

static Sh2StepResult modify_byte(Sh2Context *context, uint32_t address, uint16_t opcode,
                                 uint8_t keep, uint8_t toggle, bool test_and_set)
{
    const uint32_t pc = context->state.pc;
    uint32_t input;
    if (context->byte_write_opcode == opcode && context->byte_write_pc == pc) {
        address = context->byte_write_address;
        input = context->byte_write_value;
    } else {
        context->byte_write_opcode = 0;
        const Sh2StepResult result = read_data(context, address, 1, &input);
        if (result != SH2_STEP_OK)
            return result;
    }

    const Sh2StepResult result = write_data(context, address, 1, (input & keep) ^ toggle);
    if (result == SH2_STEP_WAIT) {
        /* The read may have had a device side effect. Retry only the write. */
        context->byte_write_pc = pc;
        context->byte_write_address = address;
        context->byte_write_opcode = opcode;
        context->byte_write_value = (uint8_t)input;
        return result;
    }
    context->byte_write_opcode = 0;
    if (result != SH2_STEP_OK)
        return result;
    if (test_and_set) {
        Sh2State *state = &context->state;
        state->sr = (state->sr & ~(uint32_t)SH2_SR_T) | ((uint8_t)input == 0);
    }
    return sh2_retire(context, test_and_set ? 4 : 3);
}

Sh2StepResult sh2_op_modify_byte(Sh2Context *context, uint32_t address, uint16_t opcode,
                                 uint8_t keep, uint8_t toggle)
{
    return modify_byte(context, address, opcode, keep, toggle, false);
}

Sh2StepResult sh2_op_tas_byte(Sh2Context *context, unsigned rn)
{
    return modify_byte(context, context->state.r[rn], (uint16_t)(0x401b | rn << 8), 0x7f, 0x80, true);
}

bool sh2_interrupt_ready(const Sh2Context *context, unsigned level)
{
    const Sh2State *state = &context->state;
    return context->interrupt_stage ||
           (level > ((state->sr >> 4) & 15) && level <= 16 &&
            !state->delay_pending && !state->irq_inhibit && !context->mac_opcode &&
            !context->byte_write_opcode);
}

Sh2StepResult sh2_interrupt(Sh2Context *context, unsigned level, uint8_t vector)
{
    Sh2State *state = &context->state;
    if (!context->interrupt_stage) {
        if (!sh2_interrupt_ready(context, level))
            return SH2_STEP_UNSUPPORTED;
        context->interrupt_stage = 1;
        context->interrupt_level = (uint8_t)level;
        context->interrupt_vector = vector;
    }

    while (context->interrupt_stage <= 2) {
        uint32_t stack = state->r[15] - 4;
        uint32_t value = context->interrupt_stage == 1 ? state->sr : state->pc;
        Sh2StepResult result = write_data(context, stack, 4, value);
        if (result != SH2_STEP_OK)
            return result;
        state->r[15] = stack;
        ++context->interrupt_stage;
    }

    uint32_t target;
    Sh2StepResult result = read_data(context, state->vbr + 4u * context->interrupt_vector,
                                    4, &target);
    if (result != SH2_STEP_OK)
        return result;
    const uint32_t mask = context->interrupt_level == 16 ? 15 : context->interrupt_level;
    state->sr = (state->sr & ~UINT32_C(0xf0)) | (mask << 4);
    state->pc = target;
    state->sleeping = false;
    /* Eight nominal states from acceptance through vector fetch, before bus waits. */
    state->cycles += 8;
    context->interrupt_stage = 0;
    return SH2_STEP_OK;
}

Sh2StepResult sh2_op_rte(Sh2Context *context)
{
    Sh2State *state = &context->state;
    if (state->delay_pending)
        return SH2_STEP_SLOT_ILLEGAL;

    uint32_t target, sr;
    Sh2StepResult result = read_data(context, state->r[15], 4, &target);
    if (result != SH2_STEP_OK)
        return result;
    result = read_data(context, state->r[15] + 4, 4, &sr);
    if (result != SH2_STEP_OK)
        return result;
    state->r[15] += 8;
    state->sr = sr & SH2_SR_BITS;
    sh2_op_branch(context, target, true, true, false);
    state->cycles += 2;
    return SH2_STEP_OK;
}
