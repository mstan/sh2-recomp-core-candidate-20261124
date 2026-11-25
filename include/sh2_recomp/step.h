#ifndef SH2_RECOMP_STEP_H
#define SH2_RECOMP_STEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SH2_BUS_OK,
    SH2_BUS_WAIT,
    SH2_BUS_FAULT
} Sh2BusResult;

typedef struct {
    /* Addresses are guest addresses and values are in host order. Data sizes
     * are 1, 2, or 4 bytes. WAIT must leave the transaction unapplied. */
    Sh2BusResult (*read)(void *user, uint32_t address, unsigned size, uint32_t *value);
    Sh2BusResult (*write)(void *user, uint32_t address, unsigned size, uint32_t value);
    Sh2BusResult (*fetch16)(void *user, uint32_t address, uint16_t *word);
} Sh2Bus;

typedef enum {
    SH2_STEP_OK,
    SH2_STEP_WAIT,
    SH2_STEP_BUS_FAULT,
    SH2_STEP_ADDRESS_ERROR,
    SH2_STEP_SLOT_ILLEGAL,
    SH2_STEP_UNSUPPORTED,
    SH2_STEP_NATIVE_MISS
} Sh2StepResult;

typedef struct {
    uint32_t r[16];
    uint32_t pc, pr, sr, gbr, vbr, mach, macl;
    uint32_t delay_target;
    bool delay_pending, irq_inhibit, sleeping;
    uint64_t cycles; /* nominal retired CPU cycles */
} Sh2State;

typedef struct {
    Sh2State state;
    Sh2Bus bus;
    void *user;
    uint8_t interrupt_stage, interrupt_level, interrupt_vector;
    uint32_t mac_pc;
    uint16_t mac_opcode;
    uint32_t mac_first_operand;
    uint32_t byte_write_pc, byte_write_address;
    uint16_t byte_write_opcode;
    uint8_t byte_write_value; /* Original byte saved for the pending write. */
} Sh2Context;

typedef Sh2StepResult (*Sh2NativeOp)(Sh2Context *context);

typedef struct {
    uint32_t pc;
    uint16_t word;
    Sh2NativeOp execute;
} Sh2NativeEntry;

typedef struct {
    const Sh2NativeEntry *entries; /* sorted by ascending PC, one entry per PC */
    size_t count;
} Sh2Catalog;

#ifdef __cplusplus
extern "C" {
#endif

void sh2_init(Sh2Context *context, const Sh2Bus *bus, void *user);
Sh2StepResult sh2_power_on(Sh2Context *context);
/* Once accepted, resume entry even if the external request has changed. */
bool sh2_interrupt_ready(const Sh2Context *context, unsigned level);
Sh2StepResult sh2_interrupt(Sh2Context *context, unsigned level, uint8_t vector);
Sh2StepResult sh2_fetch16(Sh2Context *context, uint16_t *word);
Sh2NativeOp sh2_native_lookup(const Sh2Catalog *catalog, uint32_t pc, uint16_t word);
Sh2StepResult sh2_step_native(Sh2Context *context, const Sh2Catalog *catalog);

#ifdef __cplusplus
}
#endif
#endif
