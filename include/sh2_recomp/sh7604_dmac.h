#ifndef SH2_RECOMP_SH7604_DMAC_H
#define SH2_RECOMP_SH7604_DMAC_H

#include "sh2_recomp/step.h"

enum { SH7604_DMAC_CHANNELS = 2, SH7604_DMAC_NONE = SH7604_DMAC_CHANNELS };

typedef struct {
    uint32_t source, destination, count, read_latch;
    uint16_t control;
    uint8_t vector, request_source;
    bool end_read, write_pending;
} Sh7604DmaChannel;

typedef struct {
    Sh7604DmaChannel channels[SH7604_DMAC_CHANNELS];
    uint8_t operation, operation_read, last_channel;
} Sh7604Dmac;

#ifdef __cplusplus
extern "C" {
#endif
bool sh7604_dmac_read(Sh7604Dmac *dma, uint32_t address, unsigned size, uint32_t *value);
bool sh7604_dmac_write(Sh7604Dmac *dma, uint32_t address, unsigned size, uint32_t value);
/* Each bit in requests represents a pending request on that channel's
 * selected peripheral input. Auto-request channels need no external bit. */
unsigned sh7604_dmac_next(const Sh7604Dmac *dma, unsigned requests);
/* Transfer one dual-address byte, word or long. A waiting write retains
 * its source latch for retry. The board owns bus ordering and timing. */
Sh2BusResult sh7604_dmac_step(Sh7604Dmac *dma, unsigned channel, const Sh2Bus *bus, void *user);
#ifdef __cplusplus
}
#endif

#endif
