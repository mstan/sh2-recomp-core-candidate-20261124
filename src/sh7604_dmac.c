#include "sh2_recomp/sh7604_dmac.h"

#define CHANNEL_BASE UINT32_C(0xffffff80)
#define VECTOR_BASE UINT32_C(0xffffffa0)
#define DMAOR UINT32_C(0xffffffb0)
#define DRCR_BASE UINT32_C(0xfffffe71)
#define COUNT_MASK UINT32_C(0xffffff)

enum {
    CHANNEL_ENABLE = 1, TRANSFER_END = 2, SINGLE_ADDRESS = 8, AUTO_REQUEST = 0x200,
    MASTER_ENABLE = 1, NMI_FLAG = 2, ADDRESS_ERROR = 4, ROUND_ROBIN = 8
};

bool sh7604_dmac_read(Sh7604Dmac *dma, uint32_t address, unsigned size, uint32_t *value)
{
    const uint32_t request = address - DRCR_BASE;
    if (size == 1 && request < SH7604_DMAC_CHANNELS) {
        *value = dma->channels[request].request_source;
        return true;
    }
    if (size != 4 || (address & 3))
        return false;

    const uint32_t offset = address - CHANNEL_BASE;
    if (offset < 32) {
        Sh7604DmaChannel *channel = &dma->channels[offset / 16];
        switch (offset & 15) {
        case 0: *value = channel->source; break;
        case 4: *value = channel->destination; break;
        case 8: *value = channel->count; break;
        default:
            *value = channel->control;
            channel->end_read = (channel->control & TRANSFER_END) != 0;
            break;
        }
        return true;
    }
    if (address == VECTOR_BASE || address == VECTOR_BASE + 8) {
        *value = dma->channels[(address - VECTOR_BASE) / 8].vector;
        return true;
    }
    if (address == DMAOR) {
        *value = dma->operation;
        dma->operation_read = dma->operation & (NMI_FLAG | ADDRESS_ERROR);
        return true;
    }
    return false;
}

bool sh7604_dmac_write(Sh7604Dmac *dma, uint32_t address, unsigned size, uint32_t value)
{
    const uint32_t request = address - DRCR_BASE;
    if (size == 1 && request < SH7604_DMAC_CHANNELS) {
        Sh7604DmaChannel *channel = &dma->channels[request];
        if (!(channel->control & CHANNEL_ENABLE))
            channel->request_source = (uint8_t)(value & 3u);
        return true;
    }
    if (size != 4 || (address & 3))
        return false;

    const uint32_t offset = address - CHANNEL_BASE;
    if (offset < 32) {
        Sh7604DmaChannel *channel = &dma->channels[offset / 16];
        switch (offset & 15) {
        case 0: channel->source = value; break;
        case 4: channel->destination = value; break;
        case 8: channel->count = value & COUNT_MASK; break;
        default: {
            unsigned end = channel->control & TRANSFER_END;
            if (channel->end_read && !(value & TRANSFER_END)) {
                end = 0;
                channel->end_read = false;
            }
            channel->control = (uint16_t)((value & ~(uint32_t)TRANSFER_END) | end);
            if (!(channel->control & CHANNEL_ENABLE))
                channel->write_pending = false;
            break;
        }
        }
        return true;
    }
    if (address == VECTOR_BASE || address == VECTOR_BASE + 8) {
        dma->channels[(address - VECTOR_BASE) / 8].vector = (uint8_t)value;
        return true;
    }
    if (address == DMAOR) {
        const unsigned keep = dma->operation & (NMI_FLAG | ADDRESS_ERROR);
        const unsigned clear = dma->operation_read & ~value;
        dma->operation = (uint8_t)((keep & ~clear) | (value & (MASTER_ENABLE | ROUND_ROBIN)));
        dma->operation_read &= dma->operation;
        if (!(dma->operation & MASTER_ENABLE))
            for (unsigned i = 0; i < SH7604_DMAC_CHANNELS; ++i)
                dma->channels[i].write_pending = false;
        return true;
    }
    if (address == DMAOR + 8)
        return true; /* No register is decoded at the reserved longword. */
    return false;
}

unsigned sh7604_dmac_next(const Sh7604Dmac *dma, unsigned requests)
{
    if ((dma->operation & (MASTER_ENABLE | NMI_FLAG | ADDRESS_ERROR)) != MASTER_ENABLE)
        return SH7604_DMAC_NONE;
    const unsigned first = (dma->operation & ROUND_ROBIN) ? (dma->last_channel ^ 1u) : 0;
    for (unsigned i = 0; i < SH7604_DMAC_CHANNELS; ++i) {
        unsigned index = first ^ i;
        const Sh7604DmaChannel *channel = &dma->channels[index];
        if ((channel->control & (TRANSFER_END | CHANNEL_ENABLE)) == CHANNEL_ENABLE &&
            (channel->write_pending || (channel->control & AUTO_REQUEST) || (requests & (1u << index))))
            return index;
    }
    return SH7604_DMAC_NONE;
}

static void advance_address(uint32_t *address, unsigned mode, unsigned size)
{
    if (mode == 1)
        *address += size;
    else if (mode == 2)
        *address -= size;
}

static Sh2BusResult report_fault(Sh7604Dmac *dma, Sh7604DmaChannel *channel, Sh2BusResult result)
{
    if (result == SH2_BUS_FAULT) {
        dma->operation |= ADDRESS_ERROR;
        channel->write_pending = false;
    }
    return result;
}

Sh2BusResult sh7604_dmac_step(Sh7604Dmac *dma, unsigned index, const Sh2Bus *bus, void *user)
{
    if (index >= SH7604_DMAC_CHANNELS)
        return SH2_BUS_FAULT;
    Sh7604DmaChannel *channel = &dma->channels[index];
    const unsigned size = 1u << ((channel->control >> 10) & 3u);
    const unsigned source_mode = (channel->control >> 12) & 3u;
    const unsigned destination_mode = (channel->control >> 14) & 3u;
    if (size > 4 || source_mode == 3 || destination_mode == 3 ||
        (channel->control & SINGLE_ADDRESS) ||
        ((channel->source | channel->destination) & (size - 1)))
        return report_fault(dma, channel, SH2_BUS_FAULT);

    if (!channel->write_pending) {
        Sh2BusResult result = bus->read(user, channel->source, size, &channel->read_latch);
        if (result != SH2_BUS_OK)
            return report_fault(dma, channel, result);
        channel->write_pending = true;
    }
    Sh2BusResult result = bus->write(user, channel->destination, size, channel->read_latch);
    if (result != SH2_BUS_OK)
        return report_fault(dma, channel, result);

    channel->write_pending = false;
    advance_address(&channel->source, source_mode, size);
    advance_address(&channel->destination, destination_mode, size);
    channel->count = (channel->count - 1) & COUNT_MASK;
    if (!channel->count)
        channel->control |= TRANSFER_END;
    dma->last_channel = (uint8_t)index;
    return SH2_BUS_OK;
}
