#include "sh2_recomp/sh7604_divu.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

int main(void)
{
    const uint32_t base = SH7604_DIVU_BASE;
    Sh7604Divu divu = {0};
    uint32_t value;
    uint8_t vector;
    unsigned extra;

    assert(!sh7604_divu_read(&divu, base + 0x18, 2, 0, &value, &extra));
    assert(!sh7604_divu_write(&divu, base + 0x1c, 4, 0, 8, &extra));
    assert(!sh7604_divu_write(&divu, base, 2, 0, 8, &extra));
    assert(sh7604_divu_write(&divu, base, 4, 0, 3, &extra) && extra == 0);
    assert(sh7604_divu_read(&divu, base, 4, 1, &value, &extra) && value == 3 && extra == 1);
    assert(sh7604_divu_write(&divu, base + 4, 4, 3, UINT32_C(0xfffffff3), &extra) && extra == 0);
    assert(sh7604_divu_read(&divu, base + 0x1c, 4, 10, &value, &extra));
    assert(value == UINT32_C(0xfffffffc) && extra == 32);
    assert(sh7604_divu_read(&divu, base + 0x18, 4, 42, &value, &extra));
    assert(value == UINT32_MAX && extra == 0);
    assert(sh7604_divu_read(&divu, base + 0x10, 4, 43, &value, &extra));
    assert(value == UINT32_MAX && extra == 0);

    assert(sh7604_divu_write(&divu, base, 4, 50, 8, &extra));
    assert(sh7604_divu_write(&divu, base + 0x10, 4, 51, 1, &extra));
    assert(sh7604_divu_write(&divu, base + 0x14, 4, 52, 5, &extra));
    assert(sh7604_divu_write(&divu, base, 4, 55, 0, &extra) && extra == 36);
    assert(sh7604_divu_read(&divu, base + 0x14, 4, 93, &value, &extra));
    assert(value == UINT32_C(0x20000000) && extra == 0);
    assert(sh7604_divu_read(&divu, base + 0x10, 4, 94, &value, &extra) && value == 5);
    assert(sh7604_divu_read(&divu, base + 4, 4, 95, &value, &extra));
    assert(value == UINT32_C(0x20000000));

    assert(sh7604_divu_write(&divu, base + 4, 4, 100, 7, &extra) && extra == 0);
    assert(!sh7604_divu_interrupt(&divu, 104, &vector));
    assert(sh7604_divu_read(&divu, base + 4, 4, 104, &value, &extra));
    assert(value == UINT32_C(0x7fffffff) && extra == 2);
    assert(sh7604_divu_read(&divu, base + 8, 4, 107, &value, &extra) && value == 1);

    assert(sh7604_divu_write(&divu, base + 8, 2, 108, 0xffff, &extra));
    assert(sh7604_divu_read(&divu, base + 8, 2, 110, &value, &extra) && value == 0);
    assert(sh7604_divu_read(&divu, base + 10, 2, 111, &value, &extra) && value == 1);
    assert(sh7604_divu_write(&divu, base + 10, 2, 112, 2, &extra));
    assert(sh7604_divu_write(&divu, base + 14, 2, 113, 0xd9, &extra));
    assert(sh7604_divu_write(&divu, base + 4, 4, 114, 7, &extra) && extra == 0);
    assert(!sh7604_divu_interrupt(&divu, 119, &vector));
    assert(sh7604_divu_interrupt(&divu, 120, &vector) && vector == 0x59);
    assert(sh7604_divu_read(&divu, base + 4, 4, 121, &value, &extra) && value == 63);
    assert(sh7604_divu_read(&divu, base + 8, 4, 122, &value, &extra) && value == 3);

    assert(sh7604_divu_write(&divu, base, 4, 130, UINT32_MAX, &extra));
    assert(sh7604_divu_write(&divu, base + 4, 4, 131, 2, &extra));
    assert(sh7604_divu_read(&divu, base + 4, 4, 140, &value, &extra));
    assert(value == UINT32_C(0xfffffffe) && extra == 30);
    assert(sh7604_divu_interrupt(&divu, 171, &vector));
    assert(sh7604_divu_write(&divu, base + 8, 4, 172, 0, &extra));
    assert(!sh7604_divu_interrupt(&divu, 173, &vector));

    assert(sh7604_divu_write(&divu, base + 0x10, 4, 180, UINT32_C(0x80000000), &extra));
    assert(sh7604_divu_write(&divu, base + 0x14, 4, 181, 0, &extra));
    assert(sh7604_divu_read(&divu, base + 0x14, 4, 186, &value, &extra));
    assert(value == UINT32_C(0x7fffffff) && extra == 1);
    return 0;
}
