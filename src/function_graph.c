#include "sh2_recomp/function_graph.h"

#include <stdlib.h>

typedef struct {
    const Sh2FunctionImage *image;
    Sh2FunctionGraph *graph;
    uint32_t *pending;
    size_t used;
} Walk;

static bool in_image(const Sh2FunctionImage *image, uint32_t pc)
{
    pc &= ~image->alias_mask;
    return !(pc & 1) && pc - image->base < image->size;
}

static uint8_t *flag_at(const Walk *walk, uint32_t pc)
{
    return &walk->graph->flags[(pc - walk->image->base) / 2];
}

static bool executed(const Sh2FunctionImage *image, uint32_t canonical_pc)
{
    const size_t word = (canonical_pc - image->base) / 2;
    return (image->observed[word >> 3] & (1u << (word & 7))) != 0;
}

static Sh2Instruction decode_at(const Sh2FunctionImage *image, uint32_t pc)
{
    const uint8_t *bytes = image->bytes + (pc - image->base);
    return sh2_decode((uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]));
}

static void visit(Walk *walk, uint32_t pc, uint32_t target, bool entry)
{
    if (!in_image(walk->image, target)) {
        *flag_at(walk, pc) |= (target & 1) ? SH2_FUNCTION_UNRESOLVED : SH2_FUNCTION_EXTERNAL;
        return;
    }
    target &= ~walk->image->alias_mask;
    if (walk->image->observed_only && !executed(walk->image, target)) {
        *flag_at(walk, pc) |= SH2_FUNCTION_UNOBSERVED_EDGE;
        return;
    }
    uint8_t *flag = flag_at(walk, target);
    if (!(*flag & SH2_FUNCTION_REACHED)) {
        *flag |= SH2_FUNCTION_REACHED;
        walk->pending[walk->used++] = target;
    }
    if (entry)
        *flag |= SH2_FUNCTION_ENTRY;
}

static bool control_transfer(Sh2Opcode opcode)
{
    switch (opcode) {
    case SH2_OP_BRA: case SH2_OP_BSR: case SH2_OP_BT: case SH2_OP_BF:
    case SH2_OP_BT_S: case SH2_OP_BF_S: case SH2_OP_JMP: case SH2_OP_JSR:
    case SH2_OP_RTS: case SH2_OP_RTE: case SH2_OP_BRAF: case SH2_OP_BSRF:
    case SH2_OP_SLEEP:
        return true;
    default:
        return false;
    }
}

size_t sh2_observed_target_begin(const Sh2FunctionImage *image, uint32_t canonical_source)
{
    size_t first = 0, last = image->target_count;
    while (first < last) {
        const size_t mid = first + (last - first) / 2;
        if (image->targets[mid].source < canonical_source)
            first = mid + 1;
        else
            last = mid;
    }
    return first;
}

bool sh2_function_literal_target(const Sh2FunctionImage *image, uint32_t pc, uint32_t *target)
{
    if (!image->follow_static || pc - image->base < 2 || !in_image(image, pc))
        return false;
    const Sh2Instruction branch = decode_at(image, pc);
    if (branch.opcode != SH2_OP_JSR && branch.opcode != SH2_OP_JMP)
        return false;

    const Sh2Instruction load = decode_at(image, pc - 2);
    if (load.opcode != SH2_OP_MOV_L_PC || load.rn != branch.rm)
        return false;
    const size_t offset = (((pc + 2) & ~UINT32_C(3)) + (uint32_t)load.displacement) - image->base;
    if (offset >= image->size || image->size - offset < 4)
        return false;
    const uint8_t *literal = image->bytes + offset;
    const uint32_t address = (uint32_t)literal[0] << 24 | (uint32_t)literal[1] << 16 |
                             (uint32_t)literal[2] << 8 | literal[3];
    if (!in_image(image, address) || decode_at(image, address & ~image->alias_mask).opcode == SH2_OP_UNSUPPORTED)
        return false;

    /* The literal adds one candidate to the native graph. Executed jumps
       still match their actual register destination at run time. */
    *target = address;
    return true;
}

static void visit_indirect(Walk *walk, uint32_t pc, bool entry)
{
    const Sh2FunctionImage *image = walk->image;
    const size_t first = sh2_observed_target_begin(image, pc);
    uint32_t literal;
    const bool recovered = sh2_function_literal_target(image, pc, &literal);
    if (recovered)
        visit(walk, pc, literal, entry);
    if (!recovered && (first == image->target_count || image->targets[first].source != pc)) {
        *flag_at(walk, pc) |= SH2_FUNCTION_UNRESOLVED;
        return;
    }
    for (size_t i = first; i < image->target_count && image->targets[i].source == pc; ++i)
        visit(walk, pc, image->targets[i].target, entry);
}

static void walk_instruction(Walk *walk, uint32_t pc)
{
    const Sh2FunctionImage *image = walk->image;
    const Sh2Instruction op = decode_at(image, pc);
    if (op.opcode == SH2_OP_UNSUPPORTED) {
        *flag_at(walk, pc) |= SH2_FUNCTION_UNSUPPORTED;
        return;
    }
    if (op.delayed) {
        if (!in_image(image, pc + 2)) {
            *flag_at(walk, pc) |= SH2_FUNCTION_UNRESOLVED;
            return;
        }
        const Sh2Opcode slot = decode_at(image, pc + 2).opcode;
        *flag_at(walk, pc + 2) |= SH2_FUNCTION_DELAY_SLOT;
        if (slot == SH2_OP_UNSUPPORTED || control_transfer(slot)) {
            *flag_at(walk, pc + 2) |= SH2_FUNCTION_UNSUPPORTED;
            return;
        }
    }

    const uint32_t fallthrough = pc + (op.delayed ? 4 : 2);
    const uint32_t relative = pc + 4 + (uint32_t)op.displacement;
    switch (op.opcode) {
    case SH2_OP_BRA:
        visit(walk, pc, relative, false);
        return;
    case SH2_OP_BSR:
        visit(walk, pc, relative, true);
        break;
    case SH2_OP_BT: case SH2_OP_BF: case SH2_OP_BT_S: case SH2_OP_BF_S:
        visit(walk, pc, relative, false);
        break;
    case SH2_OP_JMP: case SH2_OP_BRAF:
        visit_indirect(walk, pc, false);
        return;
    case SH2_OP_JSR: case SH2_OP_BSRF:
        visit_indirect(walk, pc, true);
        break;
    case SH2_OP_RTS: case SH2_OP_RTE:
        return;
    default:
        break;
    }
    visit(walk, pc, fallthrough, false);
}

bool sh2_function_graph_build(const Sh2FunctionImage *image, Sh2FunctionGraph *out)
{
    *out = (Sh2FunctionGraph){0};
    if (!image->bytes || !image->size || !image->entry_count || !image->entries ||
        (image->target_count && !image->targets) || (image->observed_only && !image->observed) ||
        ((image->base | image->size) & 1) ||
        image->size - 1 > UINT32_MAX - image->base || image->size / 2 > SIZE_MAX / sizeof(uint32_t) ||
        (image->alias_mask && ((image->alias_mask & (image->alias_mask - 1)) != 0 ||
                              ((image->base | (image->base + (uint32_t)image->size - 1)) & image->alias_mask))))
        return false;
    for (size_t i = 0; i < image->entry_count; ++i) {
        const uint32_t pc = image->entries[i];
        if (!in_image(image, pc) || (image->observed_only && !executed(image, pc & ~image->alias_mask)))
            return false;
    }
    for (size_t i = 1; i < image->target_count; ++i)
        if (image->targets[i - 1].source > image->targets[i].source)
            return false;

    const size_t words = image->size / 2;
    uint32_t *pending = malloc(words * sizeof(*pending));
    uint8_t *flags = calloc(words, sizeof(*flags));
    if (!pending || !flags) {
        free(pending);
        free(flags);
        return false;
    }
    *out = (Sh2FunctionGraph){.flags = flags, .words = words};
    Walk walk = {.image = image, .graph = out, .pending = pending};
    for (size_t i = 0; i < image->entry_count; ++i)
        visit(&walk, image->entries[i] & ~image->alias_mask, image->entries[i], true);
    while (walk.used)
        walk_instruction(&walk, walk.pending[--walk.used]);
    free(pending);

    for (size_t i = 0; i < words; ++i) {
        const unsigned flag = flags[i];
        out->functions += (flag & SH2_FUNCTION_ENTRY) != 0;
        out->unsupported += (flag & SH2_FUNCTION_UNSUPPORTED) != 0;
        out->unresolved += (flag & SH2_FUNCTION_UNRESOLVED) != 0;
        out->external += (flag & SH2_FUNCTION_EXTERNAL) != 0;
        out->unobserved_edges += (flag & SH2_FUNCTION_UNOBSERVED_EDGE) != 0;
        if (image->observed && (image->observed[i >> 3] & (1u << (i & 7))) &&
            !(flag & (SH2_FUNCTION_REACHED | SH2_FUNCTION_DELAY_SLOT)))
            ++out->uncovered;
    }
    return true;
}

void sh2_function_graph_destroy(Sh2FunctionGraph *graph)
{
    free(graph->flags);
    *graph = (Sh2FunctionGraph){0};
}
