#ifndef F2C_SEMANTIC_CONTROL_FLOW_H
#define F2C_SEMANTIC_CONTROL_FLOW_H

#include "semantic/model.h"

typedef enum F2cControlFlowEdgeKind {
    F2C_CFG_EDGE_FALLTHROUGH,
    F2C_CFG_EDGE_BRANCH,
    F2C_CFG_EDGE_LOOP_BACK,
    F2C_CFG_EDGE_LOOP_EXIT,
    F2C_CFG_EDGE_ALTERNATE_RETURN,
    F2C_CFG_EDGE_IO_END,
    F2C_CFG_EDGE_IO_EOR,
    F2C_CFG_EDGE_IO_ERROR,
    F2C_CFG_EDGE_RETURN,
    F2C_CFG_EDGE_STOP,
    F2C_CFG_EDGE_PROCEDURE_EXIT
} F2cControlFlowEdgeKind;

typedef struct F2cControlFlowEdge {
    size_t target;
    F2cControlFlowEdgeKind kind;
} F2cControlFlowEdge;

typedef enum F2cControlFlowNodeKind {
    F2C_CFG_NODE_STATEMENT,
    F2C_CFG_NODE_LOOP_LATCH,
    F2C_CFG_NODE_PROCEDURE_EXIT,
    F2C_CFG_NODE_IMAGE_TERMINATION
} F2cControlFlowNodeKind;

typedef struct F2cControlFlowNode {
    F2cControlFlowEdge *successors;
    size_t successor_count;
    size_t successor_capacity;
    F2cControlFlowEdge *predecessors;
    size_t predecessor_count;
    size_t predecessor_capacity;
    size_t statement_index;
    size_t block_index;
    F2cControlFlowNodeKind kind;
    int reachable;
} F2cControlFlowNode;

typedef struct F2cControlFlowBlock {
    F2cControlFlowEdge *successors;
    size_t successor_count;
    size_t successor_capacity;
    F2cControlFlowEdge *predecessors;
    size_t predecessor_count;
    size_t predecessor_capacity;
    size_t first_node;
    size_t node_count;
    int reachable;
} F2cControlFlowBlock;

typedef struct F2cControlFlowGraph {
    F2cControlFlowNode *nodes;
    size_t node_count;
    size_t statement_count;
    size_t procedure_exit;
    size_t image_termination;
    F2cControlFlowBlock *blocks;
    size_t block_count;
} F2cControlFlowGraph;

int f2c_control_flow_build(Context *context, const Unit *unit, F2cControlFlowGraph *graph);
void f2c_control_flow_free(F2cControlFlowGraph *graph);

#endif
