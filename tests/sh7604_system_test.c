#include "sh2_recomp/sh7604_system.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

int main(void)
{
    Sh7604System system;
    uint32_t value;
    sh7604_system_power_on(&system);
    assert(sh7604_system_read(&system, SH7604_SERIAL_BAUD, 1, &value) && value == 0xff);
    assert(sh7604_system_read(&system, SH7604_SERIAL_TX, 1, &value) && value == 0xff);
    assert(sh7604_system_read(&system, SH7604_SERIAL_STATUS, 1, &value) && value == 0x84);
    assert(sh7604_system_write(&system, SH7604_SERIAL_CONTROL, 1, 0) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_write(&system, SH7604_SERIAL_MODE, 1, 0x04) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_write(&system, SH7604_SERIAL_BAUD, 1, 0x10) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_SERIAL_MODE, 1, &value) && value == 0x04);
    assert(sh7604_system_read(&system, SH7604_SERIAL_BAUD, 1, &value) && value == 0x10);
    assert(!sh7604_system_read(&system, SH7604_SERIAL_MODE, 2, &value));
    assert(sh7604_system_write(&system, SH7604_SERIAL_MODE, 1, 0) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_write(&system, SH7604_SERIAL_BAUD, 1, 0) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_write(&system, SH7604_SERIAL_CONTROL, 1, 0x20) == SH7604_SYSTEM_WRITTEN);
    sh7604_system_advance_serial(&system, 100);
    assert(sh7604_system_write(&system, SH7604_SERIAL_TX, 1, 0xa5) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_write(&system, SH7604_SERIAL_STATUS, 1, 4) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_SERIAL_STATUS, 1, &value) && value == 0x80);
    assert(sh7604_system_write(&system, SH7604_SERIAL_TX, 1, 0x5a) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_write(&system, SH7604_SERIAL_STATUS, 1, 0) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_SERIAL_STATUS, 1, &value) && value == 0);
    sh7604_system_advance_serial(&system, 419);
    assert(sh7604_system_read(&system, SH7604_SERIAL_STATUS, 1, &value) && value == 0);
    sh7604_system_advance_serial(&system, 420);
    assert(sh7604_system_read(&system, SH7604_SERIAL_STATUS, 1, &value) && value == 0x80);
    sh7604_system_advance_serial(&system, 740);
    assert(sh7604_system_read(&system, SH7604_SERIAL_STATUS, 1, &value) && value == 0x84);
    assert(sh7604_system_read(&system, SH7604_STANDBY_CONTROL, 1, &value) && value == 0);
    assert(sh7604_system_read(&system, SH7604_CACHE_CONTROL, 1, &value) && value == 0);
    assert(!sh7604_system_read(&system, SH7604_CACHE_CONTROL, 2, &value));
    assert(sh7604_system_write(&system, SH7604_CACHE_CONTROL, 4, 0x15) == SH7604_SYSTEM_UNMAPPED);
    assert(sh7604_system_write(&system, SH7604_CACHE_CONTROL, 1, 0xdf) == SH7604_SYSTEM_CACHE_PURGE);
    assert(sh7604_system_read(&system, SH7604_CACHE_CONTROL, 1, &value) && value == 0xcf);
    assert(sh7604_system_write(&system, SH7604_STANDBY_CONTROL, 1, 0x41) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_STANDBY_CONTROL, 1, &value) && value == 0x41);
    assert(sh7604_system_read(&system, SH7604_CACHE_CONTROL, 1, &value) && value == 0xcf);
    assert(sh7604_system_write(&system, SH7604_INTERRUPT_PRIORITY_A, 2, 0xfa9b) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_INTERRUPT_PRIORITY_A, 2, &value) && value == 0xfa90);
    assert(sh7604_system_read(&system, SH7604_INTERRUPT_PRIORITY_A, 1, &value) && value == 0xfa);
    assert(sh7604_system_write(&system, SH7604_INTERRUPT_PRIORITY_A + 1, 1, 0x57) ==
           SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_INTERRUPT_PRIORITY_A, 2, &value) && value == 0xfa50);
    assert(!sh7604_system_read(&system, SH7604_INTERRUPT_PRIORITY_A + 1, 2, &value));
    assert(sh7604_system_read(&system, SH7604_INTERRUPT_CONTROL, 2, &value) && value == 0x8000);
    assert(sh7604_system_write(&system, SH7604_INTERRUPT_CONTROL, 2, 0xffff) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, SH7604_INTERRUPT_CONTROL, 2, &value) && value == 0x8101);
    assert(sh7604_system_write(&system, 0xfffffe68, 2, 0xffff) == SH7604_SYSTEM_WRITTEN);
    assert(sh7604_system_read(&system, 0xfffffe68, 2, &value) && value == 0x7f00);
    sh7604_system_power_on(&system);
    assert(sh7604_system_read(&system, SH7604_INTERRUPT_PRIORITY_A, 2, &value) && value == 0);
    assert(sh7604_system_read(&system, SH7604_CACHE_CONTROL, 1, &value) && value == 0);
    assert(sh7604_system_read(&system, SH7604_STANDBY_CONTROL, 1, &value) && value == 0);
    const uint32_t watchdog = SH7604_WATCHDOG_CONTROL;
    assert(sh7604_system_read_watchdog(&system, watchdog, 1, &value) && value == 0x18);
    assert(sh7604_system_write_watchdog(&system, watchdog, 2, 0xa520));
    assert(sh7604_system_write_watchdog(&system, watchdog, 2, 0x5afe));
    assert(sh7604_system_advance_watchdog(&system, 3) == SH7604_WATCHDOG_NONE);
    assert(sh7604_system_read_watchdog(&system, watchdog + 1, 1, &value) && value == 0xff);
    assert(sh7604_system_advance_watchdog(&system, 4) == SH7604_WATCHDOG_NONE);
    assert(sh7604_system_read_watchdog(&system, watchdog, 1, &value) && value == 0xb8);
    assert(sh7604_system_write_watchdog(&system, watchdog, 2, 0xa520));
    assert(sh7604_system_read_watchdog(&system, watchdog, 1, &value) && value == 0x38);
    assert(sh7604_system_write_watchdog(&system, watchdog + 2, 2, 0x5a60));
    assert(sh7604_system_write_watchdog(&system, watchdog, 2, 0xa560));
    assert(sh7604_system_write_watchdog(&system, watchdog, 2, 0x5aff));
    assert(sh7604_system_advance_watchdog(&system, 6) == SH7604_WATCHDOG_MANUAL_RESET);
    return 0;
}
