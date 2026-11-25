#include "sh2_recomp/sh7604_dmac.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

typedef struct {
    uint16_t words[2];
    unsigned reads, writes;
    bool stall;
} TestMemory;

static Sh2BusResult read_word(void *opaque, uint32_t address, unsigned size, uint32_t *value)
{
    TestMemory *memory = opaque;
    assert(address == 0x4012 && size == 2 && memory->reads < 2);
    *value = 0x1234u + memory->reads++;
    return SH2_BUS_OK;
}

static Sh2BusResult write_word(void *opaque, uint32_t address, unsigned size, uint32_t value)
{
    TestMemory *memory = opaque;
    assert(size == 2 && address == 0x8000 + memory->writes * 2 && memory->writes < 2);
    if (memory->stall)
        return SH2_BUS_WAIT;
    memory->words[memory->writes++] = (uint16_t)value;
    return SH2_BUS_OK;
}

int main(void)
{
    const uint32_t sar0 = UINT32_C(0xffffff80), dar0 = sar0 + 4, tcr0 = sar0 + 8;
    const uint32_t chcr0 = sar0 + 12, dmaor = UINT32_C(0xffffffb0);
    Sh7604Dmac dma = {0};
    TestMemory memory = {.stall = true};
    Sh2Bus bus = {.read = read_word, .write = write_word};
    uint32_t value;

    assert(!sh7604_dmac_write(&dma, sar0, 2, 0x4012));
    assert(sh7604_dmac_write(&dma, sar0, 4, 0x4012));
    assert(sh7604_dmac_write(&dma, dar0, 4, 0x8000));
    assert(sh7604_dmac_write(&dma, tcr0, 4, UINT32_C(0xff000002)));
    assert(sh7604_dmac_write(&dma, chcr0, 4, 0x44e1));
    assert(sh7604_dmac_next(&dma, 1) == SH7604_DMAC_NONE);
    assert(sh7604_dmac_write(&dma, dmaor, 4, 1));
    assert(sh7604_dmac_next(&dma, 0) == SH7604_DMAC_NONE);
    assert(sh7604_dmac_next(&dma, 1) == 0);
    assert(sh7604_dmac_step(&dma, 0, &bus, &memory) == SH2_BUS_WAIT);
    assert(memory.reads == 1 && memory.writes == 0);
    assert(sh7604_dmac_read(&dma, tcr0, 4, &value) && value == 2);
    assert(sh7604_dmac_next(&dma, 0) == 0);
    memory.stall = false;
    assert(sh7604_dmac_step(&dma, 0, &bus, &memory) == SH2_BUS_OK);
    assert(memory.reads == 1 && memory.writes == 1);
    assert(sh7604_dmac_next(&dma, 0) == SH7604_DMAC_NONE);
    assert(sh7604_dmac_step(&dma, 0, &bus, &memory) == SH2_BUS_OK);
    assert(memory.words[0] == 0x1234 && memory.words[1] == 0x1235);
    assert(sh7604_dmac_read(&dma, dar0, 4, &value) && value == 0x8004);
    assert(sh7604_dmac_read(&dma, sar0, 4, &value) && value == 0x4012);
    assert(sh7604_dmac_read(&dma, tcr0, 4, &value) && value == 0);
    assert(sh7604_dmac_next(&dma, 1) == SH7604_DMAC_NONE);
    assert(sh7604_dmac_write(&dma, chcr0, 4, 0x44e1)); /* TE still set. */
    assert(sh7604_dmac_read(&dma, chcr0, 4, &value) && value == 0x44e3);
    assert(sh7604_dmac_write(&dma, chcr0, 4, 0x44e1));
    assert(sh7604_dmac_next(&dma, 1) == 0);

    assert(sh7604_dmac_write(&dma, sar0, 4, 0x4013));
    assert(sh7604_dmac_step(&dma, 0, &bus, &memory) == SH2_BUS_FAULT);
    assert(sh7604_dmac_next(&dma, 1) == SH7604_DMAC_NONE);
    assert(sh7604_dmac_write(&dma, dmaor, 4, 1));
    assert(sh7604_dmac_read(&dma, dmaor, 4, &value) && value == 5);
    assert(sh7604_dmac_write(&dma, dmaor, 4, 1));
    assert(sh7604_dmac_read(&dma, dmaor, 4, &value) && value == 1);
    assert(sh7604_dmac_write(&dma, dmaor + 8, 4, 0));
    assert(sh7604_dmac_read(&dma, dmaor, 4, &value) && value == 1);
    return 0;
}
