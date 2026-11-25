#include "sh2_recomp/decode.h"
#include "sh2_recomp/interpreter.h"
#include "sh2_recomp/operations.h"

Sh2StepResult sh2_step_with_fallback(Sh2Context *context, const Sh2Catalog *catalog,
                                      bool *used_interpreter)
{
    uint16_t word;
    *used_interpreter = false;
    if (context->state.sleeping)
        return SH2_STEP_WAIT;
    Sh2StepResult result = sh2_fetch16(context, &word);
    if (result != SH2_STEP_OK)
        return result;
    Sh2NativeOp execute = sh2_native_lookup(catalog, context->state.pc, word);
    if (execute)
        return execute(context);

    *used_interpreter = true;
    const Sh2Instruction instruction = sh2_decode(word);
    const unsigned rn = instruction.rn;
    const unsigned rm = instruction.rm;
    const int immediate = instruction.immediate;
    const int displacement = instruction.displacement;
    Sh2State *state = &context->state;
    switch (instruction.opcode) {
#define SH2_OPERATION(name, operation) case SH2_OP_##name: return operation;
#include "operation_cases.inc"
#undef SH2_OPERATION
    default: return SH2_STEP_UNSUPPORTED;
    }
}
