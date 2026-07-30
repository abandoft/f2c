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
void f2c_bit_flow_free(F2cBitFlowResult *result);

#endif
