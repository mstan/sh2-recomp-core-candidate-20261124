#ifndef SH2_RECOMP_SH7604_SYSTEM_H
#define SH2_RECOMP_SH7604_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

#define SH7604_STANDBY_CONTROL UINT32_C(0xFFFFFE91)
#define SH7604_CACHE_CONTROL UINT32_C(0xFFFFFE92)
#define SH7604_INTERRUPT_CONTROL UINT32_C(0xFFFFFEE0)
#define SH7604_INTERRUPT_PRIORITY_A UINT32_C(0xFFFFFEE2)
#define SH7604_INTERRUPT_PRIORITY_B UINT32_C(0xFFFFFE60)
#define SH7604_SERIAL_MODE UINT32_C(0xFFFFFE00)
#define SH7604_SERIAL_BAUD UINT32_C(0xFFFFFE01)
#define SH7604_SERIAL_CONTROL UINT32_C(0xFFFFFE02)
#define SH7604_SERIAL_TX UINT32_C(0xFFFFFE03)
#define SH7604_SERIAL_STATUS UINT32_C(0xFFFFFE04)
#define SH7604_SERIAL_RX UINT32_C(0xFFFFFE05)
#define SH7604_WATCHDOG_CONTROL UINT32_C(0xFFFFFE80)

enum { SH7604_CACHE_ENABLE = 0x01 };

typedef struct {
    uint8_t standby_control, cache_control;
    uint16_t interrupt_control, interrupt_priority_a, interrupt_priority_b;
    uint16_t interrupt_vectors[5]; /* A, B, C, D, WDT */
    uint8_t serial_mode, serial_baud, serial_control, serial_tx, serial_multiprocessor_bit;
    bool serial_buffered;
    uint64_t serial_clock, serial_deadline;
    uint64_t watchdog_clocks, watchdog_deadline;
    uint8_t watchdog_control, watchdog_count, watchdog_reset, watchdog_read_flags;
} Sh7604System;

typedef enum {
    SH7604_WATCHDOG_NONE, SH7604_WATCHDOG_POWER_RESET, SH7604_WATCHDOG_MANUAL_RESET
} Sh7604WatchdogEvent;

typedef enum {
    SH7604_SYSTEM_UNMAPPED,
    SH7604_SYSTEM_WRITTEN,
    SH7604_SYSTEM_CACHE_PURGE
} Sh7604SystemWrite;

#ifdef __cplusplus
extern "C" {
#endif

void sh7604_system_power_on(Sh7604System *system);
void sh7604_system_advance_serial(Sh7604System *system, uint64_t clocks);
Sh7604WatchdogEvent sh7604_system_advance_watchdog(Sh7604System *system, uint64_t clocks);
bool sh7604_system_read_watchdog(Sh7604System *system, uint32_t address, unsigned size, uint32_t *value);
bool sh7604_system_write_watchdog(Sh7604System *system, uint32_t address, unsigned size, uint32_t value);
bool sh7604_system_read(const Sh7604System *system, uint32_t address,
                        unsigned size, uint32_t *value);

/* Apply operating-mode changes and clear cache validity and replacement
 * state when the result requests a purge. CP itself reads as zero. */
Sh7604SystemWrite sh7604_system_write(Sh7604System *system, uint32_t address,
                                     unsigned size, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
