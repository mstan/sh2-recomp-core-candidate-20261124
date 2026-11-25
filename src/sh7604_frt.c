#include "sh2_recomp/sh7604_frt.h"

enum { CLEAR_A = 1, OVERFLOW = 2, MATCH_B = 4, MATCH_A = 8, CAPTURE = 0x80, FLAGS = 0x8e };

void sh7604_frt_power_on(Sh7604Frt *timer)
{
    *timer = (Sh7604Frt){.compare = {0xffff, 0xffff}, .enable = 1, .output = 0xe0};
}

/* Match flags are published as the counter leaves the matching value. */
static unsigned next_match(uint16_t counter, uint16_t compare)
{
    return (uint16_t)(compare - counter) + 1u;
}

static void advance(Sh7604Frt *timer, uint64_t cycles)
{
    const unsigned clock = timer->control & 3;
    const unsigned shift = 3 + 2 * clock;
    uint64_t counts = (cycles >> shift) - (timer->cycles >> shift);
    timer->cycles = cycles;
    if (!counts || clock == 3)
        return;

    const uint16_t a = timer->compare[0], b = timer->compare[1];
    const unsigned match_a = next_match(timer->counter, a);
    const unsigned match_b = next_match(timer->counter, b);
    const unsigned overflow = 0x10000u - timer->counter;
    if ((timer->status & CLEAR_A) && counts >= match_a) {
        timer->status |= MATCH_A;
        if (match_b <= match_a || (b <= a && counts - match_a > b))
            timer->status |= MATCH_B;
        if (overflow < match_a)
            timer->status |= OVERFLOW;
        timer->counter = (uint16_t)((counts - match_a) % (a + 1u));
        return;
    }
    if (counts >= match_a)
        timer->status |= MATCH_A;
    if (counts >= match_b)
        timer->status |= MATCH_B;
    if (counts >= overflow)
        timer->status |= OVERFLOW;
    timer->counter = (uint16_t)(timer->counter + counts);
}

void sh7604_frt_capture_pulse(Sh7604Frt *timer, uint64_t cycles)
{
    /* The console supplies both edges of the pulse. Either configured edge
       latches the current counter and asserts the same capture flag. */
    advance(timer, cycles);
    timer->capture = timer->counter;
    timer->status |= CAPTURE;
    timer->observed_flags &= (uint8_t)~CAPTURE;
}

unsigned sh7604_frt_interrupt_flags(Sh7604Frt *timer, uint64_t cycles)
{
    if (!(timer->enable & FLAGS))
        return 0;
    advance(timer, cycles);
    return timer->enable & timer->status & FLAGS;
}

bool sh7604_frt_read(Sh7604Frt *timer, uint32_t address, unsigned size,
                     uint64_t cycles, uint32_t *value)
{
    const uint32_t offset = address - SH7604_FRT_BASE;
    if (size != 1 || offset >= 10)
        return false;
    advance(timer, cycles);
    switch (offset) {
    case 0: *value = timer->enable; break;
    case 1:
        timer->observed_flags = timer->status & FLAGS;
        *value = timer->status;
        break;
    case 2:
    case 8: {
        uint16_t word = offset == 2 ? timer->counter : timer->capture;
        timer->temporary = (uint8_t)word;
        *value = word >> 8;
        break;
    }
    case 3:
    case 9: *value = timer->temporary; break;
    case 4:
    case 5: {
        uint16_t word = timer->compare[(timer->output >> 4) & 1];
        *value = offset == 4 ? word >> 8 : word & 0xffu;
        break;
    }
    case 6: *value = timer->control; break;
    default: *value = timer->output; break;
    }
    return true;
}

bool sh7604_frt_write(Sh7604Frt *timer, uint32_t address, unsigned size,
                      uint64_t cycles, uint32_t value)
{
    const uint32_t offset = address - SH7604_FRT_BASE;
    if (size != 1 || offset >= 8)
        return false;
    advance(timer, cycles);
    const uint16_t latched = (uint16_t)((timer->temporary << 8) | (value & 0xffu));
    switch (offset) {
    case 0: timer->enable = (uint8_t)((value & FLAGS) | 1); break;
    case 1:
        timer->status = (uint8_t)((timer->status & FLAGS & ~(timer->observed_flags & ~value)) |
                                  (value & CLEAR_A));
        timer->observed_flags = 0;
        break;
    case 2:
    case 4: timer->temporary = (uint8_t)value; break;
    case 3: timer->counter = latched; break;
    case 5: timer->compare[(timer->output >> 4) & 1] = latched; break;
    case 6: timer->control = (uint8_t)(value & 0x83); break;
    default: timer->output = (uint8_t)((value & 0x13) | 0xe0); break;
    }
    return true;
}
