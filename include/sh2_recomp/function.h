#ifndef SH2_RECOMP_FUNCTION_H
#define SH2_RECOMP_FUNCTION_H

#include "sh2_recomp/function_graph.h"

#include <stdio.h>

typedef struct {
    uint32_t r[16], sr, gbr, vbr, pr, mach, macl;
} Sh2FunctionRegisters;

typedef enum {
    SH2_FUNCTION_RETURNED, SH2_FUNCTION_EXIT, SH2_FUNCTION_FAULT, SH2_FUNCTION_TAIL, SH2_FUNCTION_RESTART,
    SH2_FUNCTION_UNIMPLEMENTED
} Sh2FunctionResult;

typedef struct Sh2FunctionContext Sh2FunctionContext;
typedef struct Sh2FunctionCode Sh2FunctionCode;
typedef Sh2FunctionResult (*Sh2Function)(Sh2FunctionContext *context);

typedef struct {
    uint32_t clear_bits, base, decoded_size, backing_mask;
    const uint8_t *read;
    uint8_t *write;
    const uint8_t *code_words;
} Sh2FunctionMemory;

struct Sh2FunctionContext {
    Sh2FunctionRegisters registers;
    /* Architectural resume or failure address. Generated bodies publish it
       at safepoints and transfers, with straight-line PCs kept in code. */
    uint32_t pc;
    /* Set on an unbound transfer alongside the source in pc. */
    uint32_t unbound_target;
    bool has_unbound_target, sleeping;
    uint64_t fuel, pending_cost, pending_instructions;
    bool (*service)(Sh2FunctionContext *context);
    bool (*memory)(void *user, uint32_t address, unsigned size, bool write, uint32_t *value);
    Sh2Function (*resolve)(void *user, uint32_t address);
    void (*tail_transfer)(void *user, const Sh2FunctionCode *from, uint32_t source, uint32_t destination);
    bool (*restart_transfer)(void *user, const Sh2FunctionCode *from, uint32_t source, uint32_t destination);
    Sh2FunctionMemory mapped[3];
    Sh2Function next;
    void *user;
    /* Return true when this read-only predicate can only change at a
       scheduler boundary. The emitter will re-read after servicing it. */
    bool (*idle_poll)(Sh2FunctionContext *context, uint32_t address, unsigned size, uint32_t mask,
                      bool wait_while_zero);
};

static inline Sh2FunctionResult sh2_function_finish_tails(Sh2FunctionContext *context, Sh2FunctionResult result)
{
    while (result == SH2_FUNCTION_TAIL)
        result = context->next(context);
    return result;
}

static inline bool sh2_function_mapped_read(const Sh2FunctionContext *context, uint32_t address,
                                             unsigned width, uint32_t *value)
{
    for (size_t i = 0; i < sizeof(context->mapped) / sizeof(*context->mapped); ++i) {
        const Sh2FunctionMemory *range = &context->mapped[i];
        const uint32_t decoded = (address & ~range->clear_bits) - range->base;
        if (decoded >= range->decoded_size)
            continue;
        const uint8_t *bytes = range->read + (decoded & range->backing_mask);
        if (width == 1)
            *value = bytes[0];
        else if (width == 2)
            *value = (uint32_t)bytes[0] << 8 | bytes[1];
        else
            *value = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
        return true;
    }
    return false;
}

static inline bool sh2_function_mapped_write(const Sh2FunctionContext *context, uint32_t address,
                                              unsigned width, uint32_t value)
{
    for (size_t i = 0; i < sizeof(context->mapped) / sizeof(*context->mapped); ++i) {
        const Sh2FunctionMemory *range = &context->mapped[i];
        const uint32_t decoded = (address & ~range->clear_bits) - range->base;
        if (decoded >= range->decoded_size || !range->write)
            continue;
        const uint32_t offset = decoded & range->backing_mask;
        if (range->code_words) {
            /* An aligned width of four or less fits in one byte of this map. */
            const unsigned mask = (width == 4 ? 3u : 1u) << ((offset >> 1) & 7);
            if (range->code_words[offset >> 4] & mask)
                return false;
        }
        uint8_t *bytes = range->write + offset;
        if (width == 4) {
            bytes[0] = (uint8_t)(value >> 24);
            bytes[1] = (uint8_t)(value >> 16);
            bytes[2] = (uint8_t)(value >> 8);
            bytes[3] = (uint8_t)value;
        } else if (width == 2) {
            bytes[0] = (uint8_t)(value >> 8);
            bytes[1] = (uint8_t)value;
        } else {
            bytes[0] = (uint8_t)value;
        }
        return true;
    }
    return false;
}

typedef struct {
    uint32_t pc;
    Sh2Function execute;
} Sh2FunctionEntry;

typedef struct {
    uint32_t offset, size;
} Sh2FunctionCodeRange;

struct Sh2FunctionCode {
    uint32_t base, size, alias_mask;
    uint64_t hash, code_hash;
    uint32_t first_entry, index_words;
    const Sh2Function *index;
    const Sh2FunctionCodeRange *code;
    size_t code_count;
};

static inline Sh2Function sh2_function_lookup(const Sh2FunctionCode *code, uint32_t pc)
{
    const uint32_t offset = (pc & ~code->alias_mask) - code->first_entry;
    return !(offset & 1) && (offset >> 1) < code->index_words ? code->index[offset >> 1] : NULL;
}

static inline Sh2Function sh2_function_lookup_or_resolve(const Sh2FunctionCode *code,
                                                          Sh2FunctionContext *context, uint32_t pc)
{
    const Sh2Function local = sh2_function_lookup(code, pc);
    if (local)
        return local;
    return context->resolve ? context->resolve(context->user, pc) : NULL;
}

static inline uint64_t sh2_function_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

#define SH2_FUNCTION_HASH_SEED UINT64_C(14695981039346656037)

static inline uint64_t sh2_function_code_hash(const Sh2FunctionCode *code, const uint8_t *bytes)
{
    uint64_t hash = SH2_FUNCTION_HASH_SEED;
    for (size_t i = 0; i < code->code_count; ++i) {
        const Sh2FunctionCodeRange range = code->code[i];
        hash = sh2_function_hash_bytes(hash, bytes + range.offset, range.size);
    }
    return hash;
}

#ifdef __cplusplus
extern "C" {
#endif
/* Compile the proven subset of an image. Unsupported operations and
   unresolved transfers produce an address-bearing diagnostic. */
bool sh2_emit_functions(FILE *out, FILE *errors, const char *name, const Sh2FunctionImage *image);
#ifdef __cplusplus
}
#endif

#endif
