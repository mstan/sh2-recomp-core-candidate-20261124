#include "sh2_recomp/sh7604_frt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static uint32_t read_register(Sh7604Frt *timer, unsigned offset, uint64_t cycles)
{
    uint32_t value;
    assert(sh7604_frt_read(timer, SH7604_FRT_BASE + offset, 1, cycles, &value));
    return value;
}

static void write_register(Sh7604Frt *timer, unsigned offset, uint64_t cycles, unsigned value)
{
    assert(sh7604_frt_write(timer, SH7604_FRT_BASE + offset, 1, cycles, value));
}

int main(void)
{
    Sh7604Frt timer;
    sh7604_frt_power_on(&timer);
    assert(read_register(&timer, 0, 0) == 1 && read_register(&timer, 7, 0) == 0xe0);
    assert(read_register(&timer, 4, 0) == 0xff && read_register(&timer, 5, 0) == 0xff);
    uint32_t value;
    assert(!sh7604_frt_read(&timer, SH7604_FRT_BASE + 2, 2, 0, &value));
    assert(!sh7604_frt_write(&timer, SH7604_FRT_BASE + 8, 1, 0, 0));

    write_register(&timer, 0, 0, 0xff);
    assert(read_register(&timer, 0, 0) == 0x8f);
    write_register(&timer, 7, 0, 0x10);
    write_register(&timer, 4, 0, 0x12);
    assert(read_register(&timer, 4, 0) == 0xff);
    write_register(&timer, 5, 0, 0x34);
    assert(read_register(&timer, 4, 0) == 0x12 && read_register(&timer, 5, 0) == 0x34);
    write_register(&timer, 7, 0, 0x02);
    assert(read_register(&timer, 7, 0) == 0xe2);
    assert(read_register(&timer, 4, 0) == 0xff && read_register(&timer, 5, 0) == 0xff);

    write_register(&timer, 2, 0, 0xff);
    write_register(&timer, 3, 0, 0xff);
    assert(read_register(&timer, 2, 7) == 0xff);
    assert(read_register(&timer, 3, 8) == 0xff);
    assert(timer.counter == 0);
    write_register(&timer, 1, 8, 0);
    assert(read_register(&timer, 1, 8) == 0x0a);
    write_register(&timer, 1, 8, 0);
    assert(read_register(&timer, 1, 8) == 0);
    write_register(&timer, 6, 8, 1);
    assert(read_register(&timer, 2, 31) == 0 && read_register(&timer, 3, 31) == 0);
    assert(read_register(&timer, 2, 32) == 0 && read_register(&timer, 3, 32) == 1);

    sh7604_frt_power_on(&timer);
    write_register(&timer, 4, 0, 0);
    write_register(&timer, 5, 0, 3);
    write_register(&timer, 7, 0, 0x10);
    write_register(&timer, 4, 0, 0);
    write_register(&timer, 5, 0, 1);
    write_register(&timer, 1, 0, 1);
    assert(read_register(&timer, 1, 8 * 3) == 5 && timer.counter == 3);
    assert(read_register(&timer, 1, 8 * 4) == 13 && timer.counter == 0);
    assert(read_register(&timer, 1, 8 * 102) == 13 && timer.counter == 2);
    sh7604_frt_power_on(&timer);
    write_register(&timer, 0, 0, 0x80);
    sh7604_frt_capture_pulse(&timer, 16);
    assert(sh7604_frt_interrupt_flags(&timer, 16) == 0x80);
    assert(read_register(&timer, 8, 16) == 0 && read_register(&timer, 9, 16) == 2);
    assert(read_register(&timer, 1, 16) == 0x80);
    sh7604_frt_capture_pulse(&timer, 24);
    write_register(&timer, 1, 24, 0);
    assert(read_register(&timer, 1, 24) == 0x80);
    assert(read_register(&timer, 8, 24) == 0 && read_register(&timer, 9, 24) == 3);
    write_register(&timer, 1, 24, 0);
    assert(sh7604_frt_interrupt_flags(&timer, 24) == 0);
    return 0;
}
