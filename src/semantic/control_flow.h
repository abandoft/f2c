#ifndef F2C_SEMANTIC_CONTROL_FLOW_H
#define F2C_SEMANTIC_CONTROL_FLOW_H

#include "semantic/model.h"

typedef enum F2cControlFlowEdgeKind {
    F2C_CFG_EDGE_FALLTHROUGH,
    F2C_CFG_EDGE_BRANCH,
    F2C_CFG_EDGE_LOOP_BACK,
    F2C_CFG_EDGE_LOOP_EXIT,
    F2C_CFG_EDGE_IO_ERROR
} F2cControlFlowEdgeKind;

typedef struct F2cControlFlowEdge {
    size_t target;
    F2cControlFlowEdgeKind kind;
} F2cControlFlowEdge;

typedef struct F2cControlFlowNode {
    F2cControlFlowEdge *successors;
    size_t successor_count;
    size_t successor_capacity;
    int reachable;
} F2cControlFlowNode;

typedef struct F2cControlFlowGraph {
    F2cControlFlowNode *nodes;
    size_t node_count;
} F2cControlFlowGraph;

int f2c_control_flow_build(Context *context, const Unit *unit, F2cControlFlowGraph *graph);
void f2c_control_flow_free(F2cControlFlowGraph *graph);

#endif
