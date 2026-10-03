#ifndef F2C_CODEGEN_ALLOCATION_CONTEXT_H
#define F2C_CODEGEN_ALLOCATION_CONTEXT_H

#include "codegen/allocation/private.h"
#include "codegen/result/retention.h"

/* One executable action owns all evaluations, including designator indices.
 * Cleanup records outlive the per-object C blocks that create their values. */
typedef struct F2cAllocationContext {
    Context *context;
    Unit *unit;
    const F2cStatement *statement;
    F2cExpr **arguments;
    F2cExpr *character_length;
    F2cResultRetentionScope retention;
    char *name;
    char *status_address;
    char *status_type;
    char *status_suffix;
    char *message_pointer;
    char *message_length;
    char *length_value;
    size_t identifier;
    size_t temporary;
    unsigned int message_qualifiers;
    int outer_depth;
} F2cAllocationContext;

int f2c_allocation_context_begin(F2cAllocationContext *allocation, Context *context, Unit *unit,
                                 const F2cStatement *statement, int depth);
F2cExpr *f2c_allocation_keyword(const F2cAllocationContext *allocation, const char *name);
int f2c_allocation_prepare_value(F2cAllocationContext *allocation, F2cExpr *expression,
                                 int designator, int depth);
int f2c_allocation_capture(F2cAllocationContext *allocation, F2cArrayCleanupList *cleanup,
                           int depth);
int f2c_allocation_controls_prepare(F2cAllocationContext *allocation, int depth);
int f2c_allocation_controls_finish(F2cAllocationContext *allocation, int depth);
void f2c_allocation_failure(F2cAllocationContext *allocation, const char *success,
                            const char *message, int depth);
int f2c_allocation_context_finish(F2cAllocationContext *allocation);
void f2c_allocation_context_clear(F2cAllocationContext *allocation);

#endif
