#include "semantic/data_flow.h"

#include <stdio.h>
#include <string.h>

static int failures;

typedef struct TestFlow {
    int suppress_second_branch;
} TestFlow;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int transfer(void *user, size_t node, const F2cBitFlowState *input,
                    F2cBitFlowState *output) {
    (void)user;
    (void)input;
    if (node == 1U)
        output->bits[0] |= UINT64_C(1);
    if (node == 2U)
        output->bits[0] |= UINT64_C(2);
    if (node == 3U)
        output->flags |= UINT64_C(4);
    return 1;
}

static int edge_filter(void *user, size_t source, const F2cControlFlowEdge *edge,
                       const F2cBitFlowState *output) {
    const TestFlow *flow = (const TestFlow *)user;
    (void)output;
    return !(flow->suppress_second_branch && source == 0U && edge->target == 2U);
}

static void test_union_fixed_point(void) {
    F2cControlFlowEdge edges0[] = {{1U, F2C_CFG_EDGE_BRANCH}, {2U, F2C_CFG_EDGE_BRANCH}};
    F2cControlFlowEdge edges1[] = {{3U, F2C_CFG_EDGE_FALLTHROUGH}};
    F2cControlFlowEdge edges2[] = {{3U, F2C_CFG_EDGE_FALLTHROUGH}};
    F2cControlFlowEdge edges3[] = {{1U, F2C_CFG_EDGE_LOOP_BACK}};
    F2cControlFlowNode nodes[4];
    F2cControlFlowGraph graph;
    F2cBitFlowResult result;
    TestFlow flow = {0};
    memset(nodes, 0, sizeof(nodes));
    memset(&graph, 0, sizeof(graph));
    nodes[0].successors = edges0;
    nodes[0].successor_count = 2U;
    nodes[1].successors = edges1;
    nodes[1].successor_count = 1U;
    nodes[2].successors = edges2;
    nodes[2].successor_count = 1U;
    nodes[3].successors = edges3;
    nodes[3].successor_count = 1U;
    graph.nodes = nodes;
    graph.node_count = 4U;
    expect(f2c_bit_flow_solve(&graph, 0U, 1U, NULL, UINT64_C(1), transfer, edge_filter, &flow,
                              &result),
           "the bitset worklist converges across a back edge");
    if (result.states != NULL) {
        expect(result.states[3].initialized, "the join node receives a data-flow state");
        expect(result.states[3].bits[0] == UINT64_C(3),
               "the join node contains definitions from both predecessors");
        expect((result.states[1].flags & UINT64_C(4)) != 0U,
               "flags produced after the join converge around the loop");
    }
    f2c_bit_flow_free(&result);
}

static void test_edge_filter(void) {
    F2cControlFlowEdge edges0[] = {{1U, F2C_CFG_EDGE_BRANCH}, {2U, F2C_CFG_EDGE_BRANCH}};
    F2cControlFlowEdge edges1[] = {{3U, F2C_CFG_EDGE_FALLTHROUGH}};
    F2cControlFlowEdge edges2[] = {{3U, F2C_CFG_EDGE_FALLTHROUGH}};
    F2cControlFlowNode nodes[4];
    F2cControlFlowGraph graph;
    F2cBitFlowResult result;
    TestFlow flow = {1};
    memset(nodes, 0, sizeof(nodes));
    memset(&graph, 0, sizeof(graph));
    nodes[0].successors = edges0;
    nodes[0].successor_count = 2U;
    nodes[1].successors = edges1;
    nodes[1].successor_count = 1U;
    nodes[2].successors = edges2;
    nodes[2].successor_count = 1U;
    graph.nodes = nodes;
    graph.node_count = 4U;
    expect(f2c_bit_flow_solve(&graph, 0U, 1U, NULL, 0U, transfer, edge_filter, &flow, &result),
           "a problem-specific edge filter participates in worklist solving");
    if (result.states != NULL) {
        expect(!result.states[2].initialized,
               "a filtered branch does not initialize its target state");
        expect(result.states[3].bits[0] == UINT64_C(1),
               "filtered definitions do not contaminate the join");
    }
    f2c_bit_flow_free(&result);
}

static void test_flag_only_domain(void) {
    F2cControlFlowEdge edge = {1U, F2C_CFG_EDGE_FALLTHROUGH};
    F2cControlFlowNode nodes[2];
    F2cControlFlowGraph graph;
    F2cBitFlowResult result;
    memset(nodes, 0, sizeof(nodes));
    memset(&graph, 0, sizeof(graph));
    nodes[0].successors = &edge;
    nodes[0].successor_count = 1U;
    graph.nodes = nodes;
    graph.node_count = 2U;
    expect(f2c_bit_flow_solve(&graph, 0U, 0U, NULL, UINT64_C(8), NULL, NULL, NULL, &result),
           "a zero-word flag-only data-flow domain is supported");
    if (result.states != NULL)
        expect(result.states[1].initialized && result.states[1].flags == UINT64_C(8),
               "flag-only state propagates across an edge");
    f2c_bit_flow_free(&result);
}

static void test_invalid_successor_rejected(void) {
    F2cControlFlowEdge edge = {2U, F2C_CFG_EDGE_FALLTHROUGH};
    F2cControlFlowNode node;
    F2cControlFlowGraph graph;
    F2cBitFlowResult result;
    memset(&node, 0, sizeof(node));
    memset(&graph, 0, sizeof(graph));
    memset(&result, 0, sizeof(result));
    node.successors = &edge;
    node.successor_count = 1U;
    graph.nodes = &node;
    graph.node_count = 1U;
    expect(!f2c_bit_flow_solve(&graph, 0U, 1U, NULL, 0U, NULL, NULL, NULL, &result),
           "a data-flow problem rejects an out-of-range CFG successor");
    expect(result.states == NULL && result.storage == NULL,
           "a rejected graph does not leak a partial data-flow result");
}

int main(void) {
    test_union_fixed_point();
    test_edge_filter();
    test_flag_only_domain();
    test_invalid_successor_rejected();
    if (failures != 0)
        fprintf(stderr, "%d data-flow test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
