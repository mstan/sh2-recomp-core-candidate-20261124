#ifndef SH2_RECOMP_EMIT_H
#define SH2_RECOMP_EMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generate sh2_catalog_<name> from an even-length big-endian image.
 * Unsupported words are skipped. The caller supplies a C identifier suffix. */
bool sh2_emit_catalog(FILE *out, const char *name, const uint8_t *bytes,
                      size_t size, uint32_t base);

#ifdef __cplusplus
}
#endif
#endif
