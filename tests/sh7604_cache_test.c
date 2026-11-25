#include "sh2_recomp/sh7604_cache.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>

int main(void)
{
    Sh7604Cache cache = {0};
    Sh7604System control = {0};
    uint8_t line[SH7604_CACHE_LINE_SIZE] = {0x12, 0x34, 0x56, 0x78};
    uint32_t value;
    assert(!sh7604_cache_read(&cache, &control, 0, 2, true, line, &value));

    control.cache_control = 0x03;
    assert(!sh7604_cache_read(&cache, &control, 0, 2, true, line, &value));
    assert(sh7604_cache_read(&cache, &control, 0, 4, false, line, &value));
    assert(value == UINT32_C(0x12345678));
    line[0] = 0xab;
    assert(sh7604_cache_read(&cache, &control, 0, 2, true, line, &value));
    assert(value == 0x1234);
    assert(sh7604_cache_array_read(&cache, 0xc0000c00u, 4, &value) && value == 0x12345678);
    assert(sh7604_cache_array_write(&cache, 0xc0000c01u, 1, 0x66));
    assert(sh7604_cache_read(&cache, &control, 0, 2, true, NULL, &value) && value == 0x1266);
    sh7604_cache_write(&cache, &control, 0, 2, 0x5678);
    assert(sh7604_cache_read(&cache, &control, 0, 4, true, NULL, &value));
    assert(value == UINT32_C(0x56785678));

    sh7604_cache_purge(&cache);
    assert(!sh7604_cache_read(&cache, &control, 0, 2, true, NULL, &value));
    control.cache_control = 1;
    for (unsigned i = 0; i < 4; ++i) {
        line[0] = (uint8_t)i;
        assert(sh7604_cache_read(&cache, &control, i * 0x400, 1, false, line, &value));
        assert(value == i);
        assert(cache.set[0].tag[3 - i] == i);
    }
    /* Updating the oldest entry makes the next-oldest one the victim. */
    sh7604_cache_write(&cache, &control, 0, 1, 0x55);
    assert(sh7604_cache_read(&cache, &control, 0x1000, 1, false, line, &value));
    assert(!sh7604_cache_read(&cache, &control, 0x400, 1, false, NULL, &value));
    assert(sh7604_cache_read(&cache, &control, 0, 1, true, NULL, &value) && value == 0x55);

    const uint8_t lru = cache.set[0].lru;
    assert(!sh7604_cache_purge_write(&cache, 0x40000000u, 2));
    assert(!sh7604_cache_purge_write(&cache, 0x40000002u, 4));
    assert(!sh7604_cache_purge_write(&cache, 0x48000000u, 4));
    assert(sh7604_cache_purge_write(&cache, 0x40000400u, 4));
    assert(sh7604_cache_purge_write(&cache, 0x40000004u, 4));
    assert(cache.set[0].lru == lru);
    assert(!sh7604_cache_read(&cache, &control, 0, 1, true, NULL, &value));
    assert(sh7604_cache_read(&cache, &control, 0x800, 1, true, NULL, &value));
    assert(sh7604_cache_array_read(&cache, 0xc0000c00u, 1, &value) && value == 0x55);

    sh7604_cache_purge(&cache);
    control.cache_control = 0x09;
    for (unsigned i = 0; i < 3; ++i)
        assert(sh7604_cache_read(&cache, &control, i * 0x400, 1, false, line, &value));
    assert(cache.set[0].valid == 0x0c);
    assert(!sh7604_cache_read(&cache, &control, 0, 1, false, NULL, &value));
    assert(sh7604_cache_array_write(&cache, 0xc0000410u, 4, 0x01020304));
    assert(sh7604_cache_array_read(&cache, 0xc0000412u, 2, &value) && value == 0x0304);
    assert(cache.set[1].line[1][0] == 1 && cache.set[1].valid == 0);
    assert(!sh7604_cache_array_read(&cache, 0xc0000411u, 2, &value));
    assert(!sh7604_cache_array_write(&cache, 0xc0001000u, 1, 0));
    sh7604_cache_purge(&cache);
    assert(sh7604_cache_array_read(&cache, 0xc0000410u, 4, &value) && value == 0x01020304);

    control.cache_control = 0x41;
    assert(!sh7604_cache_address_write(&cache, &control, 0x66003414u, 2, 0));
    assert(sh7604_cache_address_write(&cache, &control, 0x66003414u, 4, 0x210));
    assert(sh7604_cache_address_read(&cache, &control, 0x60000010u, 4, &value));
    assert(value == 0x06003614);
    assert(sh7604_cache_read(&cache, &control, 0x06003410u, 4, true, NULL, &value));
    assert(value == 0x01020304);
    assert(sh7604_cache_address_write(&cache, &control, 0x66003410u, 4, 0));
    assert(!sh7604_cache_read(&cache, &control, 0x06003410u, 4, true, NULL, &value));
    return 0;
}
