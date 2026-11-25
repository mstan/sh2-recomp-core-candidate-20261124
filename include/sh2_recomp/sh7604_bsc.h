#ifndef SH2_RECOMP_SH7604_BSC_H
#define SH2_RECOMP_SH7604_BSC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t bcr1, bcr2, wcr, mcr;
    uint8_t rtcsr, rtcnt, rtcor;
    uint16_t clock_phase;
    bool compare_read;
    bool refresh_requested;
} Sh7604Bsc;

#ifdef __cplusplus
extern "C" {
#endif

void sh7604_bsc_power_on(Sh7604Bsc *bsc, bool slave);

/* Values are in host order. False indicates an address or width outside
 * the documented register interface. Unkeyed writes have no effect. */
bool sh7604_bsc_read(Sh7604Bsc *bsc, uint32_t address, unsigned size, uint32_t *value);
bool sh7604_bsc_write(Sh7604Bsc *bsc, uint32_t address, unsigned size, uint32_t value);

/* The caller's timeline supplies elapsed SH7604 clock ticks and schedules
 * external bus service. Refresh requests persist until serviced. */
void sh7604_bsc_advance(Sh7604Bsc *bsc, uint64_t clocks);
bool sh7604_bsc_compare_irq(const Sh7604Bsc *bsc);
void sh7604_bsc_refresh_serviced(Sh7604Bsc *bsc);

/* A word write in this window presents the address bits to SDRAM. The
 * board owns the SDRAM mode and timing of the external command. */
bool sh7604_sdram_mode_write(uint32_t address, unsigned size, uint16_t *mode_address);

#ifdef __cplusplus
}
#endif
#endif
