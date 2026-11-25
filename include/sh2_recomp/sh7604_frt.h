#ifndef SH2_RECOMP_SH7604_FRT_H
#define SH2_RECOMP_SH7604_FRT_H

#include <stdbool.h>
#include <stdint.h>

#define SH7604_FRT_BASE UINT32_C(0xfffffe10)

typedef struct {
    uint64_t cycles;
    uint16_t counter, compare[2], capture;
    uint8_t enable, status, control, output, temporary, observed_flags;
} Sh7604Frt;

#ifdef __cplusplus
extern "C" {
#endif

/* Cycle arguments are absolute system cycles since this CPU reset. */
void sh7604_frt_power_on(Sh7604Frt *timer);
void sh7604_frt_capture_pulse(Sh7604Frt *timer, uint64_t cycles);
unsigned sh7604_frt_interrupt_flags(Sh7604Frt *timer, uint64_t cycles);
bool sh7604_frt_read(Sh7604Frt *timer, uint32_t address, unsigned size,
                     uint64_t cycles, uint32_t *value);
bool sh7604_frt_write(Sh7604Frt *timer, uint32_t address, unsigned size,
                      uint64_t cycles, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
