#ifndef SH2_RECOMP_FUNCTION_GRAPH_H
#define SH2_RECOMP_FUNCTION_GRAPH_H

#include "sh2_recomp/decode.h"

#include <stddef.h>

enum {
    SH2_FUNCTION_REACHED = 1,
    SH2_FUNCTION_ENTRY = 2,
    SH2_FUNCTION_DELAY_SLOT = 4,
    SH2_FUNCTION_UNSUPPORTED = 8,
    SH2_FUNCTION_UNRESOLVED = 16,
    SH2_FUNCTION_EXTERNAL = 32,
    SH2_FUNCTION_UNOBSERVED_EDGE = 64,
};

typedef struct {
    uint32_t source, target;
} Sh2ObservedTarget;

typedef struct {
    uint32_t base;
    const uint8_t *bytes;
    size_t size;
    const uint32_t *entries;
    size_t entry_count;
    /* Sorted by canonical source, then actual target. The target retains
       its address view so an indirect branch can cross a mirror. */
    const Sh2ObservedTarget *targets;
    size_t target_count;
    /* Optional executed-word bitmap, in the same layout as Saturn's map. */
    const uint8_t *observed;
    /* Restrict discovery to this captured code when other image versions
       or uncovered branches are worked on independently. */
    bool observed_only;
    /* Compile statically reachable successors and report unverified exits. */
    bool follow_static;
    uint32_t alias_mask;
    /* Qualified loader calls whose target lives in the current image. */
    const Sh2ObservedTarget *local_handoffs;
    size_t local_handoff_count;
} Sh2FunctionImage;

typedef struct {
    uint8_t *flags; /* One entry per halfword, relative to image.base. */
    size_t words, functions, unsupported, unresolved, external, uncovered, unobserved_edges;
} Sh2FunctionGraph;

#ifdef __cplusplus
extern "C" {
#endif

/* Recursive descent starts at explicit entries and follows only decoded
   successors. Missing indirect-target evidence remains a diagnostic. */
bool sh2_function_graph_build(const Sh2FunctionImage *image, Sh2FunctionGraph *out);
void sh2_function_graph_destroy(Sh2FunctionGraph *graph);
size_t sh2_observed_target_begin(const Sh2FunctionImage *image, uint32_t canonical_source);
bool sh2_function_literal_target(const Sh2FunctionImage *image, uint32_t canonical_source, uint32_t *target);

#ifdef __cplusplus
}
#endif

#endif
