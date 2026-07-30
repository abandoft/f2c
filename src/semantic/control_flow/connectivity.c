#include "semantic/control_flow/private.h"

#include <stdlib.h>
#include <string.h>

static int append_connection(F2cControlFlowEdge **items, size_t *count, size_t *capacity,
                             size_t target, F2cControlFlowEdgeKind kind) {
    F2cControlFlowEdge *replacement;
    size_t index;
    size_t next_capacity;
    for (index = 0U; index < *count; ++index)
        if ((*items)[index].target == target && (*items)[index].kind == kind)
            return 1;
    if (*count == *capacity) {
        next_capacity = *capacity == 0U ? 2U : *capacity * 2U;
        if (next_capacity < *capacity || next_capacity > SIZE_MAX / sizeof(**items))
            return 0;
        replacement = (F2cControlFlowEdge *)realloc(*items, next_capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        *items = replacement;
        *capacity = next_capacity;
    }
    (*items)[*count] = (F2cControlFlowEdge){target, kind};
    ++*count;
    return 1;
}

static int build_node_predecessors(F2cControlFlowGraph *graph) {
    size_t source;
    for (source = 0U; source < graph->node_count; ++source) {
        const F2cControlFlowNode *node = &graph->nodes[source];
        size_t edge;
        for (edge = 0U; edge < node->successor_count; ++edge) {
            const F2cControlFlowEdge successor = node->successors[edge];
            F2cControlFlowNode *target;
            if (successor.target >= graph->node_count)
                return 0;
            target = &graph->nodes[successor.target];
            if (!append_connection(&target->predecessors, &target->predecessor_count,
                                   &target->predecessor_capacity, source, successor.kind))
                return 0;
        }
    }
    return 1;
}

static int mark_block_leaders(const F2cControlFlowGraph *graph, unsigned char *leaders) {
    size_t source;
    if (graph->statement_count != 0U)
        leaders[0] = 1U;
    for (source = graph->statement_count; source < graph->node_count; ++source)
        leaders[source] = 1U;
    for (source = 0U; source < graph->statement_count; ++source) {
        const F2cControlFlowNode *node = &graph->nodes[source];
        size_t edge;
        for (edge = 0U; edge < node->successor_count; ++edge) {
            const F2cControlFlowEdge successor = node->successors[edge];
            const size_t target = successor.target;
            if (target >= graph->node_count)
                return 0;
            if (target != source + 1U || successor.kind != F2C_CFG_EDGE_FALLTHROUGH)
                leaders[target] = 1U;
        }
        if (source + 1U < graph->statement_count &&
            (node->successor_count != 1U || node->successors[0].target != source + 1U ||
             node->successors[0].kind != F2C_CFG_EDGE_FALLTHROUGH))
            leaders[source + 1U] = 1U;
    }
    return 1;
}

static int create_blocks(F2cControlFlowGraph *graph, const unsigned char *leaders) {
    size_t node;
    size_t block_count = 0U;
    size_t block_index = SIZE_MAX;
    for (node = 0U; node < graph->node_count; ++node)
        if (leaders[node])
            ++block_count;
    if (block_count == 0U || block_count > SIZE_MAX / sizeof(*graph->blocks))
        return 0;
    graph->blocks = (F2cControlFlowBlock *)calloc(block_count, sizeof(*graph->blocks));
    if (graph->blocks == NULL)
        return 0;
    graph->block_count = block_count;
    for (node = 0U; node < graph->node_count; ++node) {
        F2cControlFlowBlock *block;
        if (leaders[node]) {
            ++block_index;
            graph->blocks[block_index].first_node = node;
        }
        if (block_index == SIZE_MAX || block_index >= graph->block_count)
            return 0;
        block = &graph->blocks[block_index];
        ++block->node_count;
        block->reachable = block->reachable || graph->nodes[node].reachable;
        graph->nodes[node].block_index = block_index;
    }
    return block_index + 1U == graph->block_count;
}

static int connect_blocks(F2cControlFlowGraph *graph) {
    size_t source;
    for (source = 0U; source < graph->node_count; ++source) {
        const F2cControlFlowNode *node = &graph->nodes[source];
        const size_t source_block = node->block_index;
        size_t edge;
        for (edge = 0U; edge < node->successor_count; ++edge) {
            const F2cControlFlowEdge successor = node->successors[edge];
            const size_t target_block = graph->nodes[successor.target].block_index;
            F2cControlFlowBlock *from;
            F2cControlFlowBlock *to;
            if (source_block == target_block)
                continue;
            from = &graph->blocks[source_block];
            to = &graph->blocks[target_block];
            if (!append_connection(&from->successors, &from->successor_count,
                                   &from->successor_capacity, target_block, successor.kind) ||
                !append_connection(&to->predecessors, &to->predecessor_count,
                                   &to->predecessor_capacity, source_block, successor.kind))
                return 0;
        }
    }
    return 1;
}

int f2c_control_flow_finalize(Context *context, const Unit *unit, F2cControlFlowGraph *graph) {
    unsigned char *leaders;
    int result;
    (void)context;
    (void)unit;
    if (graph == NULL || graph->node_count == 0U || graph->procedure_exit >= graph->node_count ||
        graph->image_termination >= graph->node_count)
        return 0;
    leaders = (unsigned char *)calloc(graph->node_count, sizeof(*leaders));
    if (leaders == NULL)
        return 0;
    result = build_node_predecessors(graph) && mark_block_leaders(graph, leaders) &&
             create_blocks(graph, leaders) && connect_blocks(graph);
    free(leaders);
    return result;
}
