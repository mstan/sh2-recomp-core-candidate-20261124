#include "sh2_recomp/sh7604_divu.h"

enum {
    DVSR = 0, DVDNT = 4, DVCR = 8, VCRDIV = 12, DVDNTH = 16, DVDNTL = 20,
    DVDNTH_READ_ALIAS = 24, DVDNTL_READ_ALIAS = 28,
    OVERFLOW = 1, OVERFLOW_IRQ = 2, NORMAL_CLOCKS = 39, OVERFLOW_CLOCKS = 6
};

static bool mapped(uint32_t offset, unsigned size)
{
    if (offset >= DVDNTL + 4)
        return false;
    if (size == 4)
        return (offset & 3) == 0;
    return size == 2 && (offset & 1) == 0 &&
           ((offset & ~3u) == DVCR || (offset & ~3u) == VCRDIV);
}

static void complete(Sh7604Divu *divu, uint64_t cycle)
{
    if (!divu->pending || cycle < divu->ready_cycle)
        return;
    divu->dividend_high = divu->remainder;
    divu->dividend_low = divu->quotient;
    if (divu->overflow)
        divu->control |= OVERFLOW;
    divu->pending = false;
}

static uint64_t access(Sh7604Divu *divu, uint64_t cycle, bool read,
                       unsigned *extra_cycles)
{
    uint64_t now = cycle;
    if (divu->pending && now < divu->ready_cycle)
        now = divu->ready_cycle;
    else if (read && divu->has_written && now - divu->last_write_cycle == 1)
        ++now;
    complete(divu, now);
    *extra_cycles = (unsigned)(now - cycle);
    return now;
}

static uint64_t overflow_result(uint64_t dividend, uint32_t divisor)
{
    /* The first half of the overflow operation sets flags. The remaining
     * three clocks retain three non-restoring division digits. */
    const uint64_t aligned_divisor = (uint64_t)divisor << 32;
    const bool negative_divisor = (divisor >> 31) != 0;
    for (unsigned digit = 0; digit < 3; ++digit) {
        const bool subtract = ((dividend >> 63) != 0) == negative_divisor;
        const uint64_t partial = dividend + (subtract ? 0 - aligned_divisor : aligned_divisor);
        const bool quotient_bit = ((partial >> 63) != 0) == negative_divisor;
        dividend = (partial << 1) | (uint64_t)quotient_bit;
    }
    return dividend;
}

static void start(Sh7604Divu *divu, uint64_t cycle)
{
    const uint64_t dividend = ((uint64_t)divu->dividend_high << 32) | divu->dividend_low;
    const bool negative_dividend = (dividend >> 63) != 0;
    const bool negative_divisor = (divu->divisor >> 31) != 0;
    const bool negative_quotient = negative_dividend != negative_divisor;
    const uint64_t numerator = negative_dividend ? 0 - dividend : dividend;
    const uint32_t denominator = negative_divisor ? 0u - divu->divisor : divu->divisor;
    const uint32_t limit = negative_quotient ? UINT32_C(0x80000000) : UINT32_C(0x7fffffff);
    const uint64_t quotient = denominator ? numerator / denominator : 0;
    const uint32_t remainder = denominator ? (uint32_t)(numerator % denominator) : 0;

    /* A negative remainder also overflows the positive endpoint when both
     * inputs are negative. Use unsigned magnitudes to handle every input. */
    divu->overflow = !denominator || quotient > limit ||
        (negative_dividend && negative_divisor && quotient == limit && remainder != 0);
    if (divu->overflow) {
        const uint64_t partial = overflow_result(dividend, divu->divisor);
        divu->remainder = (uint32_t)(partial >> 32);
        divu->quotient = (divu->control & OVERFLOW_IRQ) ? (uint32_t)partial : limit;
    } else {
        divu->remainder = negative_dividend ? 0u - remainder : remainder;
        divu->quotient = negative_quotient ? 0u - (uint32_t)quotient : (uint32_t)quotient;
    }
    divu->ready_cycle = cycle + (divu->overflow ? OVERFLOW_CLOCKS : NORMAL_CLOCKS);
    divu->pending = true;
}

bool sh7604_divu_read(Sh7604Divu *divu, uint32_t address, unsigned size,
                     uint64_t cycle, uint32_t *value, unsigned *extra_cycles)
{
    uint32_t offset = address - SH7604_DIVU_BASE;
    /* The SH7604 also exposes both result registers through read-only ports. */
    if (offset == DVDNTH_READ_ALIAS || offset == DVDNTL_READ_ALIAS)
        offset -= DVDNTH_READ_ALIAS - DVDNTH;
    if (!mapped(offset, size))
        return false;
    access(divu, cycle, true, extra_cycles);
    if (size == 2 && !(offset & 2)) {
        *value = 0;
        return true;
    }
    switch (offset & ~3u) {
    case DVSR: *value = divu->divisor; break;
    case DVCR: *value = divu->control; break;
    case VCRDIV: *value = divu->vector; break;
    case DVDNTH: *value = divu->dividend_high; break;
    default: *value = divu->dividend_low; break;
    }
    return true;
}

bool sh7604_divu_write(Sh7604Divu *divu, uint32_t address, unsigned size,
                      uint64_t cycle, uint32_t value, unsigned *extra_cycles)
{
    const uint32_t offset = address - SH7604_DIVU_BASE;
    if (!mapped(offset, size))
        return false;
    const uint64_t now = access(divu, cycle, false, extra_cycles);
    divu->has_written = true;
    divu->last_write_cycle = now;
    if (size == 2 && !(offset & 2))
        return true;
    switch (offset & ~3u) {
    case DVSR: divu->divisor = value; break;
    case DVCR: divu->control = (uint8_t)(value & (OVERFLOW | OVERFLOW_IRQ)); break;
    case VCRDIV: divu->vector = (uint8_t)(value & 0x7f); break;
    case DVDNTH: divu->dividend_high = value; break;
    case DVDNT:
        divu->dividend_high = 0u - (value >> 31);
        /* Both dividend ports load the same low word and start the divide. */
        /* fall through */
    case DVDNTL:
        divu->dividend_low = value;
        start(divu, now);
        break;
    }
    return true;
}

bool sh7604_divu_interrupt(Sh7604Divu *divu, uint64_t cycle, uint8_t *vector)
{
    complete(divu, cycle);
    if ((divu->control & (OVERFLOW | OVERFLOW_IRQ)) != (OVERFLOW | OVERFLOW_IRQ))
        return false;
    *vector = divu->vector;
    return true;
}
