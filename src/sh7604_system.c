#include "sh2_recomp/sh7604_system.h"

#include <stddef.h>

enum {
    SERIAL_TE = 0x20, SERIAL_TIE = 0x80, SERIAL_TEIE = 4,
    SERIAL_TDRE = 0x80, SERIAL_TEND = 4, SERIAL_MPBT = 1,
    SERIAL_SYNC = 0x80, SERIAL_SEVEN_BITS = 0x40, SERIAL_PARITY = 0x20,
    SERIAL_TWO_STOP_BITS = 8, SERIAL_MULTIPROCESSOR = 4, SERIAL_EXTERNAL_CLOCK = 2,
    WATCHDOG_ENABLE = 0x20, WATCHDOG_MODE = 0x40, WATCHDOG_OVERFLOW = 0x80,
    WATCHDOG_RESET_ENABLE = 0x40, WATCHDOG_MANUAL = 0x20
};

typedef struct {
    size_t offset;
    uint16_t writable;
} InterruptRegister;

static InterruptRegister interrupt_register(uint32_t address)
{
    switch (address & ~UINT32_C(1)) {
    case SH7604_INTERRUPT_CONTROL:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_control), 0x0101};
    case SH7604_INTERRUPT_PRIORITY_A:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_priority_a), 0xfff0};
    case SH7604_INTERRUPT_PRIORITY_B:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_priority_b), 0xff00};
    case 0xfffffe62:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_vectors[0]), 0x7f7f};
    case 0xfffffe64:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_vectors[1]), 0x7f7f};
    case 0xfffffe66:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_vectors[2]), 0x7f7f};
    case 0xfffffe68:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_vectors[3]), 0x7f00};
    case 0xfffffee4:
        return (InterruptRegister){offsetof(Sh7604System, interrupt_vectors[4]), 0x7f7f};
    default:
        return (InterruptRegister){0};
    }
}

void sh7604_system_power_on(Sh7604System *system)
{
    *system = (Sh7604System){.interrupt_control = 0x8000, .serial_baud = 0xff, .serial_tx = 0xff};
}

static void serial_start(Sh7604System *system, uint64_t clocks)
{
    system->serial_buffered = false;
    if (system->serial_control & SERIAL_EXTERNAL_CLOCK) {
        system->serial_deadline = UINT64_MAX; /* No external clock input is attached. */
        return;
    }
    const unsigned mode = system->serial_mode;
    const bool synchronous = (mode & SERIAL_SYNC) != 0;
    unsigned bits = 8;
    if (!synchronous) {
        bits += 2; /* Start and stop. */
        bits -= (mode & SERIAL_SEVEN_BITS) != 0;
        bits += (mode & (SERIAL_PARITY | SERIAL_MULTIPROCESSOR)) != 0;
        bits += (mode & SERIAL_TWO_STOP_BITS) != 0;
    }
    const unsigned clocks_per_bit = (synchronous ? 4u : 32u) *
                                    (1u << (2 * (mode & 3))) * (system->serial_baud + 1);
    system->serial_deadline = clocks + (uint64_t)bits * clocks_per_bit;
}

void sh7604_system_advance_serial(Sh7604System *system, uint64_t clocks)
{
    while (system->serial_deadline && system->serial_deadline != UINT64_MAX &&
           system->serial_deadline <= clocks) {
        if (!system->serial_buffered) {
            system->serial_deadline = 0;
            break;
        }
        serial_start(system, system->serial_deadline);
    }
    system->serial_clock = clocks;
}

static unsigned watchdog_shift(const Sh7604System *system)
{
    static const uint8_t shifts[8] = {1, 6, 7, 8, 9, 10, 12, 13};
    return shifts[system->watchdog_control & 7];
}

static void watchdog_reschedule(Sh7604System *system)
{
    const unsigned shift = watchdog_shift(system);
    system->watchdog_deadline = (system->watchdog_control & WATCHDOG_ENABLE) ?
        ((system->watchdog_clocks >> shift) + 256 - system->watchdog_count) << shift : 0;
}

Sh7604WatchdogEvent sh7604_system_advance_watchdog(Sh7604System *system, uint64_t clocks)
{
    Sh7604WatchdogEvent event = SH7604_WATCHDOG_NONE;
    if (system->watchdog_control & WATCHDOG_ENABLE) {
        const unsigned shift = watchdog_shift(system);
        const uint64_t count = system->watchdog_count + (clocks >> shift) - (system->watchdog_clocks >> shift);
        system->watchdog_count = (uint8_t)count;
        if (count >= 256) {
            if (system->watchdog_control & WATCHDOG_MODE) {
                system->watchdog_reset |= WATCHDOG_OVERFLOW;
                system->watchdog_read_flags &= (uint8_t)~2u;
                system->watchdog_count = system->watchdog_control = 0;
                if (system->watchdog_reset & WATCHDOG_RESET_ENABLE)
                    event = system->watchdog_reset & WATCHDOG_MANUAL ?
                            SH7604_WATCHDOG_MANUAL_RESET : SH7604_WATCHDOG_POWER_RESET;
            } else if (!(system->watchdog_control & WATCHDOG_OVERFLOW)) {
                system->watchdog_control |= WATCHDOG_OVERFLOW;
                system->watchdog_read_flags &= (uint8_t)~1u;
            }
        }
    }
    system->watchdog_clocks = clocks;
    watchdog_reschedule(system);
    return event;
}

bool sh7604_system_read_watchdog(Sh7604System *system, uint32_t address, unsigned size, uint32_t *value)
{
    const uint32_t offset = (address - SH7604_WATCHDOG_CONTROL) & ~UINT32_C(8);
    if (size != 1)
        return false;
    switch (offset) {
    case 0:
        *value = system->watchdog_control | 0x18;
        if (system->watchdog_control & WATCHDOG_OVERFLOW)
            system->watchdog_read_flags |= 1;
        return true;
    case 1: *value = system->watchdog_count; return true;
    case 2: *value = 0xff; return true;
    case 3:
        *value = system->watchdog_reset | 0x1f;
        if (system->watchdog_reset & WATCHDOG_OVERFLOW)
            system->watchdog_read_flags |= 2;
        return true;
    default: return false;
    }
}

bool sh7604_system_write_watchdog(Sh7604System *system, uint32_t address, unsigned size, uint32_t value)
{
    const uint32_t offset = (address - SH7604_WATCHDOG_CONTROL) & ~UINT32_C(8);
    if (size != 2 || (offset != 0 && offset != 2))
        return false;
    const unsigned key = (value >> 8) & 0xff;
    if (offset == 0) {
        if (key == 0x5a && (system->watchdog_control & WATCHDOG_ENABLE)) {
            system->watchdog_count = (uint8_t)value;
        } else if (key == 0xa5) {
            unsigned overflow = system->watchdog_control & WATCHDOG_OVERFLOW;
            if ((system->watchdog_read_flags & 1) && !(value & WATCHDOG_OVERFLOW))
                overflow = 0;
            system->watchdog_control = (uint8_t)((value & 0x67) | overflow);
            if (!(system->watchdog_control & WATCHDOG_ENABLE)) {
                system->watchdog_count = 0;
                system->watchdog_control &= (uint8_t)~WATCHDOG_OVERFLOW;
            } else {
                system->standby_control &= (uint8_t)~0x80;
            }
            system->watchdog_read_flags &= (uint8_t)~1u;
        }
        watchdog_reschedule(system);
    } else {
        if (key == 0x5a)
            system->watchdog_reset = (uint8_t)((system->watchdog_reset & WATCHDOG_OVERFLOW) | (value & 0x60));
        else if (key == 0xa5 && !(value & WATCHDOG_OVERFLOW) && (system->watchdog_read_flags & 2))
            system->watchdog_reset &= (uint8_t)~WATCHDOG_OVERFLOW;
        system->watchdog_read_flags &= (uint8_t)~2u;
    }
    return true;
}

bool sh7604_system_read(const Sh7604System *system, uint32_t address,
                        unsigned size, uint32_t *value)
{
    const InterruptRegister reg = interrupt_register(address);
    if (reg.writable && (size == 1 || (size == 2 && !(address & 1)))) {
        const uint16_t word = *(const uint16_t *)((const uint8_t *)system + reg.offset);
        *value = size == 2 ? word : (address & 1) ? word & 0xff : word >> 8;
        return true;
    }
    if (size != 1)
        return false;
    switch (address) {
    case SH7604_SERIAL_MODE: *value = system->serial_mode; return true;
    case SH7604_SERIAL_BAUD: *value = system->serial_baud; return true;
    case SH7604_SERIAL_CONTROL: *value = system->serial_control; return true;
    case SH7604_SERIAL_TX: *value = system->serial_tx; return true;
    case SH7604_SERIAL_STATUS:
        *value = (system->serial_buffered ? 0 : SERIAL_TDRE) |
                 (system->serial_deadline ? 0 : SERIAL_TEND) | system->serial_multiprocessor_bit;
        return true;
    case SH7604_SERIAL_RX: *value = 0; return true; /* No serial peer has sent data. */
    case SH7604_STANDBY_CONTROL: *value = system->standby_control; return true;
    case SH7604_CACHE_CONTROL: *value = system->cache_control; return true;
    default: return false;
    }
}

Sh7604SystemWrite sh7604_system_write(Sh7604System *system, uint32_t address,
                                     unsigned size, uint32_t value)
{
    const InterruptRegister reg = interrupt_register(address);
    if (reg.writable && (size == 1 || (size == 2 && !(address & 1)))) {
        uint16_t *word = (uint16_t *)((uint8_t *)system + reg.offset);
        uint16_t mask = reg.writable;
        if (size == 1) {
            const unsigned shift = (address & 1) ? 0 : 8;
            mask &= (uint16_t)(0xffu << shift);
            value <<= shift;
        }
        *word = (uint16_t)((*word & ~mask) | (value & mask));
        return SH7604_SYSTEM_WRITTEN;
    }
    if (size != 1)
        return SH7604_SYSTEM_UNMAPPED;
    switch (address) {
    case SH7604_SERIAL_MODE:
        system->serial_mode = (uint8_t)value;
        return SH7604_SYSTEM_WRITTEN;
    case SH7604_SERIAL_BAUD:
        system->serial_baud = (uint8_t)value;
        return SH7604_SYSTEM_WRITTEN;
    case SH7604_SERIAL_CONTROL:
        /* The machine has no serial interrupt source wired yet. */
        if ((value & SERIAL_TE) && (value & (SERIAL_TIE | SERIAL_TEIE)))
            return SH7604_SYSTEM_UNMAPPED;
        system->serial_control = (uint8_t)value;
        if (!(value & SERIAL_TE)) {
            system->serial_deadline = 0;
            system->serial_buffered = false;
        }
        return SH7604_SYSTEM_WRITTEN;
    case SH7604_SERIAL_TX:
        system->serial_tx = (uint8_t)value;
        return SH7604_SYSTEM_WRITTEN;
    case SH7604_SERIAL_STATUS:
        system->serial_multiprocessor_bit = (uint8_t)(value & SERIAL_MPBT);
        if ((system->serial_control & SERIAL_TE) && !(value & SERIAL_TDRE)) {
            if (system->serial_deadline)
                system->serial_buffered = true;
            else
                serial_start(system, system->serial_clock);
        }
        return SH7604_SYSTEM_WRITTEN;
    case SH7604_STANDBY_CONTROL:
        system->standby_control = (uint8_t)(value & 0xdf);
        return SH7604_SYSTEM_WRITTEN;
    case SH7604_CACHE_CONTROL:
        system->cache_control = (uint8_t)(value & 0xcf);
        return value & 0x10 ? SH7604_SYSTEM_CACHE_PURGE : SH7604_SYSTEM_WRITTEN;
    default:
        return SH7604_SYSTEM_UNMAPPED;
    }
}
