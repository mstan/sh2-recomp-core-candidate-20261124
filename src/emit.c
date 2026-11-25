#include "sh2_recomp/decode.h"
#include "sh2_recomp/emit.h"
#include "operation_source.h"

#include <inttypes.h>

static bool identifier_character(unsigned char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_';
}

const char *sh2_operation_source(Sh2Opcode opcode)
{
    switch (opcode) {
#define SH2_OPERATION(name, operation) case SH2_OP_##name: return #operation;
#include "operation_cases.inc"
#undef SH2_OPERATION
    default: return NULL;
    }
}

static uint16_t word_at(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)(((uint16_t)bytes[offset] << 8) | bytes[offset + 1]);
}

bool sh2_emit_catalog(FILE *out, const char *name, const uint8_t *bytes,
                      size_t size, uint32_t base)
{
    if (!*name || (base & 1) || (size & 1) || (size && size - 1 > UINT32_MAX - base))
        return false;
    for (const char *p = name; *p; ++p) {
        if (!identifier_character((unsigned char)*p))
            return false;
    }

    fputs("#include \"sh2_recomp/operations.h\"\n\n", out);
    size_t count = 0;
    for (size_t offset = 0; offset < size; offset += 2) {
        Sh2Instruction decoded = sh2_decode(word_at(bytes, offset));
        const char *source = sh2_operation_source(decoded.opcode);
        if (!source)
            continue;

        fprintf(out, "static Sh2StepResult op_%08" PRIx32 "(Sh2Context *context)\n"
                     "{\n"
                     "    enum { rn = %u, rm = %u, immediate = %d, displacement = %d };\n"
                     "    Sh2State *state = &context->state;\n"
                     "    (void)state;\n"
                     "    return %s;\n"
                     "}\n\n",
                base + (uint32_t)offset, (unsigned)decoded.rn, (unsigned)decoded.rm,
                decoded.immediate, decoded.displacement, source);
        ++count;
    }

    if (!count) {
        fprintf(out, "const Sh2Catalog sh2_catalog_%s = {NULL, 0};\n", name);
        return !ferror(out);
    }

    fputs("static const Sh2NativeEntry entries[] = {\n", out);
    for (size_t offset = 0; offset < size; offset += 2) {
        uint16_t word = word_at(bytes, offset);
        if (!sh2_operation_source(sh2_decode(word).opcode))
            continue;
        const uint32_t pc = base + (uint32_t)offset;
        fprintf(out, "    {0x%08" PRIx32 ", 0x%04x, op_%08" PRIx32 "},\n",
                pc, (unsigned)word, pc);
    }
    fprintf(out, "};\n\nconst Sh2Catalog sh2_catalog_%s = "
                 "{entries, sizeof(entries) / sizeof(entries[0])};\n", name);
    return !ferror(out);
}
