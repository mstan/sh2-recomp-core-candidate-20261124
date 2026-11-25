#ifndef SH2_RECOMP_SH7604_CACHE_H
#define SH2_RECOMP_SH7604_CACHE_H

#include "sh7604_system.h"

enum { SH7604_CACHE_LINE_SIZE = 16 };

typedef struct {
    uint32_t tag[4];
    uint8_t line[4][SH7604_CACHE_LINE_SIZE];
    uint8_t valid, lru;
} Sh7604CacheSet;

typedef struct {
    Sh7604CacheSet set[64];
} Sh7604Cache;

#ifdef __cplusplus
extern "C" {
#endif

void sh7604_cache_purge(Sh7604Cache *cache);

/* A longword write to the associative purge area invalidates the addressed
 * line, if present. A handled write consumes two cycles. */
bool sh7604_cache_purge_write(Sh7604Cache *cache, uint32_t address, unsigned size);

/* Use only for ordinary memory in the cacheable address space. `backing`
 * is the complete, side-effect-free external cache line, or NULL if the
 * caller cannot safely fill the line. False means use the external bus. */
bool sh7604_cache_read(Sh7604Cache *cache, const Sh7604System *system,
                       uint32_t address, unsigned size, bool instruction,
                       const uint8_t *backing, uint32_t *value);

/* SH7604 stores are write-through. Update a matching local line only after
 * the external write succeeds. Cache-through aliases bypass these helpers. */
void sh7604_cache_write(Sh7604Cache *cache, const Sh7604System *system,
                        uint32_t address, unsigned size, uint32_t value);

/* Direct way/set access uses the same bytes as automatic cache access. */
bool sh7604_cache_array_read(const Sh7604Cache *cache, uint32_t address,
                             unsigned size, uint32_t *value);
bool sh7604_cache_array_write(Sh7604Cache *cache, uint32_t address,
                              unsigned size, uint32_t value);

/* Longword address-array access uses the way selected by CCR. */
bool sh7604_cache_address_read(const Sh7604Cache *cache, const Sh7604System *system,
                               uint32_t address, unsigned size, uint32_t *value);
bool sh7604_cache_address_write(Sh7604Cache *cache, const Sh7604System *system,
                                uint32_t address, unsigned size, uint32_t value);

#ifdef __cplusplus
}
#endif
#endif
