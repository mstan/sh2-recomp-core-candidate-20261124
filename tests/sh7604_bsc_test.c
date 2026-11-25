#include "sh2_recomp/sh7604_bsc.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

int main(void)
{
    const uint32_t base = UINT32_C(0xFFFFFFE0);
    Sh7604Bsc master, slave;
    sh7604_bsc_power_on(&master, false);
    sh7604_bsc_power_on(&slave, true);
    uint32_t value;
    assert(sh7604_bsc_read(&master, base, 4, &value) && value == 0x03F0);
    assert(sh7604_bsc_read(&slave, base + 2, 2, &value) && value == 0x83F0);
    assert(sh7604_bsc_read(&master, base + 8, 4, &value) && value == 0xAAFF);
    assert(!sh7604_bsc_read(&master, base, 2, &value));
    assert(!sh7604_bsc_read(&master, base + 28, 4, &value));

    assert(sh7604_bsc_write(&master, base, 4, UINT32_C(0x00000001)));
    assert(sh7604_bsc_write(&master, base + 2, 2, 0));
    assert(sh7604_bsc_read(&master, base, 4, &value) && value == 0x03F0);
    assert(sh7604_bsc_write(&master, base, 4, UINT32_C(0xA55AFFFF)));
    assert(sh7604_bsc_write(&slave, base, 4, UINT32_C(0xA55A0001)));
    assert(sh7604_bsc_read(&master, base, 4, &value) && value == 0x1FF7);
    assert(sh7604_bsc_read(&slave, base, 4, &value) && value == 0x8001);
    assert(sh7604_bsc_write(&master, base + 4, 4, UINT32_C(0xA55AFFFF)));
    assert(sh7604_bsc_read(&master, base + 6, 2, &value) && value == 0xFC);

    assert(sh7604_bsc_write(&master, base + 12, 4, UINT32_C(0xA55A0008)));
    assert(sh7604_bsc_write(&master, base + 24, 4, UINT32_C(0xA55A0103)));
    assert(sh7604_bsc_read(&master, base + 24, 4, &value) && value == 3);
    sh7604_bsc_advance(&master, 100);
    assert(sh7604_bsc_read(&master, base + 20, 4, &value) && value == 0);

    assert(sh7604_bsc_write(&master, base + 16, 4, UINT32_C(0xA55A0048)));
    sh7604_bsc_advance(&master, 11);
    assert(sh7604_bsc_read(&master, base + 20, 4, &value) && value == 2);
    assert(!master.refresh_requested && !sh7604_bsc_compare_irq(&master));
    Sh7604Bsc batch = master;
    sh7604_bsc_advance(&master, 1);
    assert(sh7604_bsc_read(&master, base + 20, 4, &value) && value == 0);
    assert(master.refresh_requested && sh7604_bsc_compare_irq(&master));
    sh7604_bsc_advance(&master, 24);
    sh7604_bsc_advance(&batch, 25);
    assert(master.rtcnt == batch.rtcnt && master.clock_phase == batch.clock_phase);
    assert(master.rtcsr == batch.rtcsr && master.refresh_requested == batch.refresh_requested);

    assert(sh7604_bsc_write(&master, base + 16, 4, UINT32_C(0xA55A0048)));
    assert(sh7604_bsc_compare_irq(&master));
    assert(sh7604_bsc_read(&master, base + 18, 2, &value) && value == 0xC8);
    assert(sh7604_bsc_write(&master, base + 16, 4, UINT32_C(0xA55A0048)));
    assert(!sh7604_bsc_compare_irq(&master) && master.refresh_requested);
    sh7604_bsc_refresh_serviced(&master);
    assert(!master.refresh_requested);

    uint16_t mode;
    assert(sh7604_sdram_mode_write(UINT32_C(0xFFFF8446), 2, &mode) && mode == 0x0446);
    assert(!sh7604_sdram_mode_write(UINT32_C(0xFFFF8446), 1, &mode));
    assert(!sh7604_sdram_mode_write(UINT32_C(0xFFFFC000), 2, &mode));
    return 0;
}
