#include "sh2_recomp/sh7604_bsc.h"

#define BSC_BASE UINT32_C(0xFFFFFFE0)
#define SDRAM_MODE_BASE UINT32_C(0xFFFF8000)

enum {
    BCR1_SLAVE = 0x8000,
    BCR1_WRITABLE = 0x1FF7,
    BCR2_WRITABLE = 0x00FC,
    MCR_WRITABLE = 0xFEFC,
    MCR_REFRESH = 0x08,
    RTCSR_COMPARE = 0x80,
    RTCSR_IRQ_ENABLE = 0x40,
    RTCSR_CLOCK_SELECT = 0x38,
    WRITE_KEY = 0xA55A
};

typedef enum { BCR1, BCR2, WCR, MCR, RTCSR, RTCNT, RTCOR, REGISTER_COUNT } Register;

void sh7604_bsc_power_on(Sh7604Bsc *bsc, bool slave)
{
    *bsc = (Sh7604Bsc){
        .bcr1 = (uint16_t)(0x03F0 | (slave ? BCR1_SLAVE : 0)),
        .bcr2 = 0x00FC,
        .wcr = 0xAAFF
    };
}

static Register register_at(uint32_t address, unsigned size)
{
    if (address < BSC_BASE || (size != 2 && size != 4))
        return REGISTER_COUNT;
    const uint32_t offset = address - BSC_BASE;
    if (offset / 4 >= REGISTER_COUNT || (offset & 3) != 4 - size)
        return REGISTER_COUNT;
    return (Register)(offset / 4);
}

bool sh7604_bsc_read(Sh7604Bsc *bsc, uint32_t address, unsigned size, uint32_t *value)
{
    switch (register_at(address, size)) {
    case BCR1: *value = bsc->bcr1; break;
    case BCR2: *value = bsc->bcr2; break;
    case WCR: *value = bsc->wcr; break;
    case MCR: *value = bsc->mcr; break;
    case RTCSR:
        *value = bsc->rtcsr;
        bsc->compare_read = (bsc->rtcsr & RTCSR_COMPARE) != 0;
        break;
    case RTCNT: *value = bsc->rtcnt; break;
    case RTCOR: *value = bsc->rtcor; break;
    default: return false;
    }
    return true;
}

bool sh7604_bsc_write(Sh7604Bsc *bsc, uint32_t address, unsigned size, uint32_t value)
{
    const Register reg = register_at(address, size);
    if (reg == REGISTER_COUNT)
        return false;
    if (size != 4 || (value >> 16) != WRITE_KEY)
        return true;

    switch (reg) {
    case BCR1:
        bsc->bcr1 = (uint16_t)((bsc->bcr1 & BCR1_SLAVE) | (value & BCR1_WRITABLE));
        break;
    case BCR2: bsc->bcr2 = (uint16_t)(value & BCR2_WRITABLE); break;
    case WCR: bsc->wcr = (uint16_t)value; break;
    case MCR: bsc->mcr = (uint16_t)(value & MCR_WRITABLE); break;
    case RTCSR: {
        const unsigned control = value & (RTCSR_IRQ_ENABLE | RTCSR_CLOCK_SELECT);
        unsigned compare = bsc->rtcsr & RTCSR_COMPARE;
        if (((bsc->rtcsr ^ control) & RTCSR_CLOCK_SELECT) != 0)
            bsc->clock_phase = 0;
        if (bsc->compare_read && !(value & RTCSR_COMPARE)) {
            compare = 0;
            bsc->compare_read = false;
        }
        bsc->rtcsr = (uint8_t)(control | compare);
        break;
    }
    case RTCNT: bsc->rtcnt = (uint8_t)value; break;
    case RTCOR: bsc->rtcor = (uint8_t)value; break;
    default: return false;
    }
    return true;
}

void sh7604_bsc_advance(Sh7604Bsc *bsc, uint64_t clocks)
{
    static const unsigned divisors[] = {0, 4, 16, 64, 256, 1024, 2048, 4096};
    const unsigned divider = divisors[(bsc->rtcsr & RTCSR_CLOCK_SELECT) >> 3];
    if (!divider)
        return;

    const unsigned phase = bsc->clock_phase + (unsigned)(clocks % divider);
    const uint64_t ticks = clocks / divider + phase / divider;
    bsc->clock_phase = (uint16_t)(phase % divider);

    const unsigned difference = (uint8_t)(bsc->rtcor - bsc->rtcnt);
    const unsigned until_match = difference ? difference : 256;
    if (ticks < until_match) {
        bsc->rtcnt = (uint8_t)(bsc->rtcnt + ticks);
        return;
    }

    /* A match clears the count. Subsequent periods all start at zero. */
    const unsigned period = bsc->rtcor ? bsc->rtcor : 256;
    bsc->rtcnt = (uint8_t)((ticks - until_match) % period);
    bsc->rtcsr |= RTCSR_COMPARE;
    if (bsc->mcr & MCR_REFRESH)
        bsc->refresh_requested = true;
}

bool sh7604_bsc_compare_irq(const Sh7604Bsc *bsc)
{
    const unsigned bits = RTCSR_COMPARE | RTCSR_IRQ_ENABLE;
    return (bsc->rtcsr & bits) == bits;
}

void sh7604_bsc_refresh_serviced(Sh7604Bsc *bsc)
{
    bsc->refresh_requested = false;
}

bool sh7604_sdram_mode_write(uint32_t address, unsigned size, uint16_t *mode_address)
{
    if (size != 2 || (address & 1) || address < SDRAM_MODE_BASE ||
        address - SDRAM_MODE_BASE >= UINT32_C(0x4000))
        return false;
    *mode_address = (uint16_t)(address - SDRAM_MODE_BASE);
    return true;
}
