#include "sh2_recomp/sh7604_cache.h"

#include <string.h>

enum { CACHE_ID = 0x02, CACHE_OD = 0x04, CACHE_TWO_WAY = 0x08, NO_WAY = 4 };

void sh7604_cache_purge(Sh7604Cache *cache)
{
    for (unsigned i = 0; i < 64; ++i) {
        cache->set[i].valid = 0;
        cache->set[i].lru = 0;
    }
}

bool sh7604_cache_purge_write(Sh7604Cache *cache, uint32_t address, unsigned size)
{
    const uint32_t physical = address - UINT32_C(0x40000000);
    if (physical >= UINT32_C(0x08000000) || size != 4 || (address & 3))
        return false;
    Sh7604CacheSet *set = &cache->set[(physical >> 4) & 63];
    const uint32_t tag = physical >> 10;
    for (unsigned way = 0; way < 4; ++way)
        if (set->tag[way] == tag)
            set->valid &= (uint8_t)~(1u << way);
    return true;
}

static unsigned match(const Sh7604CacheSet *set, uint32_t address, bool two_way)
{
    uint32_t tag = address >> 10;
    for (unsigned way = two_way ? 2 : 0; way < 4; ++way)
        if ((set->valid & (1u << way)) && set->tag[way] == tag)
            return way;
    return NO_WAY;
}

static void accessed(Sh7604CacheSet *set, unsigned way)
{
    /* One bit per pair of ways. See the SH7604 pseudo-LRU update table. */
    static const uint8_t masks[] = {0x38, 0x26, 0x15, 0x0b};
    static const uint8_t values[] = {0x00, 0x20, 0x14, 0x0b};
    set->lru = (uint8_t)((set->lru & ~masks[way]) | values[way]);
}

static unsigned replacement(const Sh7604CacheSet *set, bool two_way)
{
    if (two_way)
        return set->lru & 1 ? 2 : 3;
    if ((set->lru & 0x38) == 0x38)
        return 0;
    if ((set->lru & 0x26) == 0x06)
        return 1;
    if ((set->lru & 0x15) == 0x01)
        return 2;
    return 3;
}

static bool fits(unsigned offset, unsigned size)
{
    return (size == 1 || size == 2 || size == 4) && offset + size <= SH7604_CACHE_LINE_SIZE;
}

static uint32_t read_bytes(const uint8_t *bytes, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

static void write_bytes(uint8_t *bytes, unsigned size, uint32_t value)
{
    for (unsigned i = size; i != 0; --i) {
        bytes[i - 1] = (uint8_t)value;
        value >>= 8;
    }
}

bool sh7604_cache_read(Sh7604Cache *cache, const Sh7604System *system,
                       uint32_t address, unsigned size, bool instruction,
                       const uint8_t *backing, uint32_t *value)
{
    const unsigned offset = address & 15;
    const unsigned control = system->cache_control;
    if (!(control & SH7604_CACHE_ENABLE) || !fits(offset, size))
        return false;
    Sh7604CacheSet *set = &cache->set[(address >> 4) & 63];
    const bool two_way = (control & CACHE_TWO_WAY) != 0;
    unsigned way = match(set, address, two_way);
    if (way == NO_WAY) {
        if (!backing || (control & (instruction ? CACHE_ID : CACHE_OD)))
            return false;
        way = replacement(set, two_way);
        memcpy(set->line[way], backing, SH7604_CACHE_LINE_SIZE);
        set->tag[way] = address >> 10;
        set->valid |= (uint8_t)(1u << way);
    }
    accessed(set, way);
    *value = read_bytes(set->line[way] + offset, size);
    return true;
}

void sh7604_cache_write(Sh7604Cache *cache, const Sh7604System *system,
                        uint32_t address, unsigned size, uint32_t value)
{
    const unsigned offset = address & 15;
    const unsigned control = system->cache_control;
    if (!(control & SH7604_CACHE_ENABLE) || !fits(offset, size))
        return;
    Sh7604CacheSet *set = &cache->set[(address >> 4) & 63];
    unsigned way = match(set, address, (control & CACHE_TWO_WAY) != 0);
    if (way == NO_WAY)
        return;
    write_bytes(set->line[way] + offset, size, value);
    accessed(set, way);
}

#define SH7604_CACHE_DATA_BASE UINT32_C(0xc0000000)

static bool array_access(uint32_t offset, unsigned size)
{
    return offset < 0x1000 && fits(offset & 15, size) && !(offset & (size - 1));
}

bool sh7604_cache_array_read(const Sh7604Cache *cache, uint32_t address,
                             unsigned size, uint32_t *value)
{
    const uint32_t offset = address - SH7604_CACHE_DATA_BASE;
    if (!array_access(offset, size))
        return false;
    const Sh7604CacheSet *set = &cache->set[(offset >> 4) & 63];
    *value = read_bytes(set->line[offset >> 10] + (offset & 15), size);
    return true;
}

bool sh7604_cache_array_write(Sh7604Cache *cache, uint32_t address,
                              unsigned size, uint32_t value)
{
    const uint32_t offset = address - SH7604_CACHE_DATA_BASE;
    if (!array_access(offset, size))
        return false;
    Sh7604CacheSet *set = &cache->set[(offset >> 4) & 63];
    write_bytes(set->line[offset >> 10] + (offset & 15), size, value);
    return true;
}

static bool address_array_access(uint32_t address, unsigned size)
{
    return (address >> 29) == 3 && size == 4 && !(address & 3);
}

bool sh7604_cache_address_read(const Sh7604Cache *cache, const Sh7604System *system,
                               uint32_t address, unsigned size, uint32_t *value)
{
    if (!address_array_access(address, size))
        return false;
    const Sh7604CacheSet *set = &cache->set[(address >> 4) & 63];
    const unsigned way = system->cache_control >> 6;
    *value = (set->tag[way] << 10) | ((uint32_t)set->lru << 4) |
             ((set->valid & (1u << way)) ? 4u : 0u);
    return true;
}

bool sh7604_cache_address_write(Sh7604Cache *cache, const Sh7604System *system,
                                uint32_t address, unsigned size, uint32_t value)
{
    if (!address_array_access(address, size))
        return false;
    Sh7604CacheSet *set = &cache->set[(address >> 4) & 63];
    const unsigned way = system->cache_control >> 6;
    const unsigned bit = 1u << way;
    set->tag[way] = (address >> 10) & 0x7ffff;
    set->valid = (uint8_t)((set->valid & ~bit) | ((address & 4) ? bit : 0));
    set->lru = (uint8_t)((value >> 4) & 63);
    return true;
}
