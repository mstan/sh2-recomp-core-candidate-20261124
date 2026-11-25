#include "boot_image.h"
#include "sh2_recomp/step.h"
#include "sh2_recomp/operations.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

extern const Sh2Catalog sh2_catalog_fixture;

typedef struct {
    uint8_t bytes[sizeof(boot_image)];
    unsigned fetches;
    bool wait_write;
    uint32_t wait_address;
    uint32_t wait_read_address;
    unsigned tracked_reads;
    uint32_t tracked_address;
} Memory;

static Sh2BusResult read_data(void *user, uint32_t address, unsigned size, uint32_t *value)
{
    Memory *memory = user;
    if (address > sizeof(memory->bytes) || size > sizeof(memory->bytes) - address)
        return SH2_BUS_FAULT;
    if (memory->wait_read_address && address == memory->wait_read_address) {
        memory->wait_read_address = 0;
        return SH2_BUS_WAIT;
    }
    if (memory->tracked_address && address == memory->tracked_address)
        ++memory->tracked_reads;
    *value = 0;
    for (unsigned i = 0; i < size; ++i)
        *value = (*value << 8) | memory->bytes[address + i];
    return SH2_BUS_OK;
}

static Sh2BusResult fetch_word(void *user, uint32_t address, uint16_t *word)
{
    Memory *memory = user;
    ++memory->fetches;
    uint32_t value;
    Sh2BusResult result = read_data(user, address, 2, &value);
    if (result == SH2_BUS_OK)
        *word = (uint16_t)value;
    return result;
}

static Sh2BusResult write_data(void *user, uint32_t address, unsigned size, uint32_t value)
{
    Memory *memory = user;
    if (address > sizeof(memory->bytes) || size > sizeof(memory->bytes) - address)
        return SH2_BUS_FAULT;
    if (memory->wait_write || (memory->wait_address && address == memory->wait_address)) {
        memory->wait_write = false;
        memory->wait_address = 0;
        return SH2_BUS_WAIT;
    }
    for (unsigned i = 0; i < size; ++i) {
        memory->bytes[address + size - 1 - i] = (uint8_t)value;
        value >>= 8;
    }
    return SH2_BUS_OK;
}

static void start(Sh2Context *context, Memory *memory)
{
    const Sh2Bus bus = {read_data, write_data, fetch_word};
    memset(memory, 0, sizeof(*memory));
    memcpy(memory->bytes, boot_image, sizeof(boot_image));
    sh2_init(context, &bus, memory);
    assert(sh2_power_on(context) == SH2_STEP_OK);
    assert(context->state.pc == 0x10 && context->state.r[15] == 0x3C);
    assert(context->state.sr == 0xF0);
}

static Sh2StepResult step_fixture(Sh2Context *context, bool fallback) {
    (void)fallback;
    return sh2_step_native(context, &sh2_catalog_fixture);
}

static void test_control_transfers(bool fallback)
{
    Sh2Context context;
    Memory memory;
    start(&context, &memory);
    Sh2State *state = &context.state;
    state->pc = 0x150;
    state->r[1] = 0x20;
    state->r[2] = 0x30;
    assert(write_data(&memory, 0x20, 4, UINT32_C(0xfffffc05)) == SH2_BUS_OK);
    assert(write_data(&memory, 0x24, 4, UINT32_C(0x25d00000)) == SH2_BUS_OK);
    assert(write_data(&memory, 0x28, 4, UINT32_C(0x06000000)) == SH2_BUS_OK);
    memory.wait_read_address = 0x20;
    assert(step_fixture(&context, fallback) == SH2_STEP_WAIT);
    assert(state->pc == 0x150 && state->r[1] == 0x20 && state->sr == 0xf0 && state->cycles == 0);
    for (unsigned i = 0; i < 3; ++i) {
        assert(step_fixture(&context, fallback) == SH2_STEP_OK);
        assert(state->cycles == (i + 1) * 3 && state->r[1] == 0x24 + 4 * i);
        assert(!sh2_interrupt_ready(&context, 15));
    }
    assert(state->sr == SH2_SR_T && state->gbr == 0x25d00000 && state->vbr == 0x06000000);
    memory.wait_address = 0x2c;
    assert(step_fixture(&context, fallback) == SH2_STEP_WAIT);
    assert(state->pc == 0x156 && state->r[2] == 0x30 && state->cycles == 9);
    for (unsigned i = 0; i < 3; ++i) {
        assert(step_fixture(&context, fallback) == SH2_STEP_OK);
        assert(state->cycles == 11 + i * 2 && state->r[2] == 0x2c - i * 4);
    }
    uint32_t stored;
    assert(read_data(&memory, 0x2c, 4, &stored) == SH2_BUS_OK && stored == state->vbr);
    assert(read_data(&memory, 0x28, 4, &stored) == SH2_BUS_OK && stored == state->gbr);
    assert(read_data(&memory, 0x24, 4, &stored) == SH2_BUS_OK && stored == state->sr);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK && sh2_interrupt_ready(&context, 15));
}

static void test_gbr_byte_logic(bool fallback)
{
    Sh2Context context;
    Memory memory;
    start(&context, &memory);
    Sh2State *state = &context.state;
    state->pc = 0x160;
    state->gbr = 0x20;
    state->r[0] = 0x16;
    state->sr = SH2_SR_S;
    assert(write_data(&memory, 0x36, 1, 0x17) == SH2_BUS_OK);
    assert(write_data(&memory, 0x26, 2, 0x8076) == SH2_BUS_OK);
    memory.wait_address = memory.tracked_address = 0x36;
    assert(step_fixture(&context, fallback) == SH2_STEP_WAIT);
    assert(memory.tracked_reads == 1 && state->pc == 0x160 && state->cycles == 0);
    assert(!sh2_interrupt_ready(&context, 6));
    assert(write_data(&memory, 0x36, 1, 0x40) == SH2_BUS_OK);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(memory.tracked_reads == 1 && memory.bytes[0x36] == 0x97 && state->cycles == 3);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK && memory.bytes[0x36] == 0x93);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK && memory.bytes[0x36] == 0x10);
    assert(state->sr == SH2_SR_S);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK && memory.bytes[0x36] == 0x10);
    assert(state->sr == (SH2_SR_S | SH2_SR_T) && state->cycles == 12);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->r[0] == UINT32_C(0xffff8076) && state->cycles == 13);
}

static void test_test_and_set(bool fallback)
{
    Sh2Context context;
    Memory memory;
    start(&context, &memory);
    Sh2State *state = &context.state;
    state->pc = 0x16e;
    state->r[3] = 0x36;
    state->sr = SH2_SR_S;
    assert(write_data(&memory, 0x36, 1, 0) == SH2_BUS_OK);
    memory.wait_address = memory.tracked_address = 0x36;
    assert(step_fixture(&context, fallback) == SH2_STEP_WAIT);
    assert(memory.tracked_reads == 1 && state->pc == 0x16e && state->sr == SH2_SR_S);
    assert(!sh2_interrupt_ready(&context, 6));
    assert(write_data(&memory, 0x36, 1, 0x55) == SH2_BUS_OK);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(memory.tracked_reads == 1 && memory.bytes[0x36] == 0x80);
    assert(state->sr == (SH2_SR_S | SH2_SR_T) && state->cycles == 4);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(memory.tracked_reads == 2 && memory.bytes[0x36] == 0x80);
    assert(state->sr == SH2_SR_S && state->cycles == 8);
}

static void test_word_mac(bool fallback)
{
    Sh2Context context;
    Memory memory;
    start(&context, &memory);
    Sh2State *state = &context.state;
    state->pc = 0x130;
    state->sr = SH2_SR_T;
    state->mach = state->macl = UINT32_MAX;
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->mach == 0 && state->macl == 0 && state->sr == SH2_SR_T);

    assert(write_data(&memory, 0x138, 2, 0xfffd) == SH2_BUS_OK);
    assert(write_data(&memory, 0x13a, 2, 2) == SH2_BUS_OK);
    assert(write_data(&memory, 0x13c, 2, 0xfffc) == SH2_BUS_OK);
    assert(write_data(&memory, 0x13e, 2, 5) == SH2_BUS_OK);
    state->r[1] = 0x13a;
    state->r[2] = 0x138;
    memory.wait_read_address = 0x13a;
    memory.tracked_address = 0x138;
    assert(step_fixture(&context, fallback) == SH2_STEP_WAIT);
    assert(state->pc == 0x132 && state->cycles == 1);
    assert(state->r[1] == 0x13a && state->r[2] == 0x138 && state->macl == 0);
    assert(!sh2_interrupt_ready(&context, 6) && memory.tracked_reads == 1);
    assert(write_data(&memory, 0x138, 2, 1) == SH2_BUS_OK);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(memory.fetches == 2 && memory.tracked_reads == 1 && sh2_interrupt_ready(&context, 6));
    assert(state->r[1] == 0x13c && state->r[2] == 0x13a && state->cycles == 4);
    assert(state->mach == UINT32_MAX && state->macl == UINT32_C(0xfffffffa));

    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->r[1] == 0x140 && state->mach == UINT32_MAX);
    assert(state->macl == UINT32_C(0xffffffe6));

    state->pc = 0x132;
    state->sr |= SH2_SR_S;
    state->mach = UINT32_C(0x02713568);
    state->macl = UINT32_C(0x7ffffffe);
    state->r[1] = 0x13a;
    state->r[2] = 0x13e;
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->macl == INT32_MAX && state->mach == UINT32_C(0x02713568));

    state->pc = 0x132;
    state->macl = UINT32_C(0x80000001);
    state->r[1] = 0x13a;
    state->r[2] = 0x13c;
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->macl == UINT32_C(0x80000000) && state->mach == UINT32_C(0x02713568));

    state->pc = 0x132;
    state->sr &= ~(uint32_t)SH2_SR_S;
    state->mach = UINT32_MAX;
    state->macl = UINT32_C(0xfffffffe);
    state->r[1] = 0x13a;
    state->r[2] = 0x138;
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->mach == 0 && state->macl == 0);
    assert(state->cycles == 16 && state->sr == SH2_SR_T);
}

static void test_long_mac(bool fallback)
{
    Sh2Context context;
    Memory memory;
    start(&context, &memory);
    Sh2State *state = &context.state;
    state->pc = 0x14a;
    state->sr = SH2_SR_T;
    state->r[0] = 0x100;
    state->r[11] = 0x104;
    assert(write_data(&memory, 0x100, 4, UINT32_C(0x80001234)) == SH2_BUS_OK);
    assert(write_data(&memory, 0x104, 4, 2) == SH2_BUS_OK);
    memory.wait_read_address = 0x104;
    memory.tracked_address = 0x100;
    assert(step_fixture(&context, fallback) == SH2_STEP_WAIT);
    assert(state->pc == 0x14a && state->cycles == 0 && !sh2_interrupt_ready(&context, 6));
    assert(state->r[0] == 0x100 && state->r[11] == 0x104 && state->mach == 0 && state->macl == 0);
    assert(write_data(&memory, 0x100, 4, 0) == SH2_BUS_OK);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->r[0] == 0x104 && state->r[11] == 0x108 && state->cycles == 3);
    assert(state->mach == UINT32_MAX && state->macl == 0x2468 && state->sr == SH2_SR_T);
    assert(memory.tracked_reads == 1 && memory.fetches == 1 && sh2_interrupt_ready(&context, 6));

    assert(write_data(&memory, 0x108, 4, 3) == SH2_BUS_OK);
    assert(step_fixture(&context, fallback) == SH2_STEP_OK);
    assert(state->r[0] == 0x10c && state->mach == UINT32_MAX && state->macl == 0x246e && state->cycles == 6);

    static const struct {
        uint32_t input_high, input_low, left, right, expected_high, expected_low;
    } sums[] = {
        {0x7fff, UINT32_MAX - 1, 2, 3, 0x7fff, UINT32_MAX},
        {0xffff8000, 1, 2, UINT32_MAX - 2, 0xffff8000, 0},
        {UINT32_MAX, UINT32_MAX - 9, 1, 2, UINT32_MAX, UINT32_MAX - 7},
    };
    state->sr |= SH2_SR_S;
    for (unsigned i = 0; i < sizeof sums / sizeof sums[0]; ++i) {
        state->pc = 0x14a;
        state->r[0] = 0x100;
        state->r[11] = 0x104;
        state->mach = sums[i].input_high;
        state->macl = sums[i].input_low;
        assert(write_data(&memory, 0x100, 4, sums[i].left) == SH2_BUS_OK);
        assert(write_data(&memory, 0x104, 4, sums[i].right) == SH2_BUS_OK);
        assert(step_fixture(&context, fallback) == SH2_STEP_OK);
        assert(state->mach == sums[i].expected_high && state->macl == sums[i].expected_low);
        assert(state->sr == (SH2_SR_S | SH2_SR_T));
    }
}


int main(void) {
    test_control_transfers(false);
    test_gbr_byte_logic(false);
    test_test_and_set(false);
    test_word_mac(false);
    test_long_mac(false);
    Sh2Context a,b; Memory ma,mb;
    start(&a,&ma); start(&b,&mb); ma.wait_write=true;
    unsigned waits=0;
    for(unsigned i=0;i<12;++i) {
      uint64_t old=a.state.cycles;
      Sh2StepResult result=sh2_step_native(&a,&sh2_catalog_fixture);
      if(result==SH2_STEP_WAIT) {assert(a.state.pc==0x1a && a.state.cycles==old);++waits;}
      else assert(result==SH2_STEP_OK);
    }
    assert(waits==1 && a.state.cycles==13 && a.state.pc==0x20);
    assert(a.state.r[0]==UINT32_MAX && a.state.r[2]==0xffffffa2u);
    assert(ma.bytes[0x38]==0xff && ma.bytes[0x39]==0xa2);
    assert(b.state.pc==0x10 && b.state.cycles==0 && mb.bytes[0x38]==0);
    ma.bytes[a.state.pc]^=1;
    assert(sh2_step_native(&a,&sh2_catalog_fixture)==SH2_STEP_NATIVE_MISS);
    return 0;
}
