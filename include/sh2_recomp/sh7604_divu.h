#ifndef SH2_RECOMP_SH7604_DIVU_H
#define SH2_RECOMP_SH7604_DIVU_H

#include <stdbool.h>
#include <stdint.h>

#define SH7604_DIVU_BASE UINT32_C(0xffffff00)

typedef struct {
    uint32_t divisor, dividend_high, dividend_low;
    uint32_t quotient, remainder;
    uint64_t ready_cycle, last_write_cycle;
    uint8_t control, vector;
    bool pending, overflow, has_written;
} Sh7604Divu;

#ifdef __cplusplus
extern "C" {
#endif

/* Accesses use elapsed SH-2 clocks. A handled bus cycle can require extra
 * clocks while the divider completes. The caller must charge that delay. */
bool sh7604_divu_read(Sh7604Divu *divu, uint32_t address, unsigned size,
                     uint64_t cycle, uint32_t *value, unsigned *extra_cycles);
bool sh7604_divu_write(Sh7604Divu *divu, uint32_t address, unsigned size,
                      uint64_t cycle, uint32_t value, unsigned *extra_cycles);
bool sh7604_divu_interrupt(Sh7604Divu *divu, uint64_t cycle, uint8_t *vector);

#ifdef __cplusplus
}
#endif
#endif
