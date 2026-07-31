#ifndef F2C_SEMANTIC_DATA_FLOW_H
#define F2C_SEMANTIC_DATA_FLOW_H

#include "semantic/control_flow.h"

typedef struct F2cBitFlowState {
    uint64_t *bits;
    uint64_t flags;
    int initialized;
} F2cBitFlowState;

typedef struct F2cBitFlowResult {
    F2cBitFlowState *states;
    uint64_t *storage;
    size_t state_count;
    size_t word_count;
} F2cBitFlowResult;

typedef int (*F2cBitFlowTransfer)(void *user, size_t node, const F2cBitFlowState *input,
                                  F2cBitFlowState *output);
typedef int (*F2cBitFlowEdgeFilter)(void *user, size_t source, const F2cControlFlowEdge *edge,
                                    const F2cBitFlowState *output);

int f2c_bit_flow_solve(const F2cControlFlowGraph *graph, size_t entry_node, size_t word_count,
                       const uint64_t *entry_bits, uint64_t entry_flags,
                       F2cBitFlowTransfer transfer, F2cBitFlowEdgeFilter edge_filter, void *user,
                       F2cBitFlowResult *result);
int f2c_bit_flow_solve_backward(const F2cControlFlowGraph *graph, size_t word_count,
                                const uint64_t *exit_bits, uint64_t exit_flags,
                                F2cBitFlowTransfer transfer, F2cBitFlowEdgeFilter edge_filter,
                                void *user, F2cBitFlowResult *result);
void f2c_bit_flow_free(F2cBitFlowResult *result);

int f2c_variable_flow_analyze(Context *context, Unit *unit, const F2cControlFlowGraph *graph);
void f2c_variable_flow_clear(Unit *unit);
int f2c_variable_flow_is_used(const Unit *unit, size_t node, const Symbol *symbol);
int f2c_variable_flow_is_defined(const Unit *unit, size_t node, const Symbol *symbol);
int f2c_variable_flow_is_live_in(const Unit *unit, size_t node, const Symbol *symbol);
int f2c_variable_flow_is_live_out(const Unit *unit, size_t node, const Symbol *symbol);

#endif
