#include "semantic/control_flow.h"

#include "ir/statement.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int has_edge(const F2cControlFlowNode *node, size_t target, F2cControlFlowEdgeKind kind) {
    size_t index;
    for (index = 0U; index < node->successor_count; ++index)
        if (node->successors[index].target == target && node->successors[index].kind == kind)
            return 1;
    return 0;
}

static int has_predecessor(const F2cControlFlowNode *node, size_t source,
                           F2cControlFlowEdgeKind kind) {
    size_t index;
    for (index = 0U; index < node->predecessor_count; ++index)
        if (node->predecessors[index].target == source && node->predecessors[index].kind == kind)
            return 1;
    return 0;
}

static void verify_connectivity(const F2cControlFlowGraph *graph) {
    size_t source;
    size_t block;
    size_t covered = 0U;
    for (source = 0U; source < graph->node_count; ++source) {
        const F2cControlFlowNode *node = &graph->nodes[source];
        size_t edge;
        expect(node->block_index < graph->block_count, "every CFG node belongs to a basic block");
        for (edge = 0U; edge < node->successor_count; ++edge) {
            const F2cControlFlowEdge successor = node->successors[edge];
            expect(successor.target < graph->node_count, "every CFG successor names a valid node");
            if (successor.target < graph->node_count)
                expect(has_predecessor(&graph->nodes[successor.target], source, successor.kind),
                       "node predecessor sets mirror successor edges");
        }
    }
    for (block = 0U; block < graph->block_count; ++block) {
        const F2cControlFlowBlock *item = &graph->blocks[block];
        size_t offset;
        expect(item->node_count != 0U, "a basic block is never empty");
        expect(item->first_node == covered, "basic blocks form an ordered partition of CFG nodes");
        for (offset = 0U; offset < item->node_count; ++offset)
            expect(graph->nodes[item->first_node + offset].block_index == block,
                   "every node in a basic-block range has the matching block index");
        covered += item->node_count;
    }
    expect(covered == graph->node_count, "basic blocks cover every CFG node exactly once");
}

static void test_empty_unit(void) {
    Unit unit;
    F2cControlFlowGraph graph;
    memset(&unit, 0, sizeof(unit));
    expect(f2c_control_flow_build(NULL, &unit, &graph), "an empty program unit has a valid CFG");
    if (graph.node_count != 0U) {
        expect(graph.node_count == 2U, "an empty unit has explicit procedure and image exits");
        expect(graph.nodes[graph.procedure_exit].reachable,
               "the procedure exit is the entry of an empty unit");
        expect(!graph.nodes[graph.image_termination].reachable,
               "image termination is unreachable without a terminating statement");
        verify_connectivity(&graph);
    }
    f2c_control_flow_free(&graph);
}

static void test_explicit_terminal_edges(void) {
    F2cStatement statements[4];
    Unit unit;
    F2cControlFlowGraph graph;
    memset(statements, 0, sizeof(statements));
    memset(&unit, 0, sizeof(unit));
    statements[0].kind = F2C_STMT_ASSIGNMENT;
    statements[1].kind = F2C_STMT_RETURN;
    statements[2].kind = F2C_STMT_STOP;
    statements[3].kind = F2C_STMT_ASSIGNMENT;
    unit.statements = statements;
    unit.statement_count = 4U;
    expect(f2c_control_flow_build(NULL, &unit, &graph), "RETURN and STOP produce a valid CFG");
    if (graph.node_count != 0U) {
        expect(has_edge(&graph.nodes[1], graph.procedure_exit, F2C_CFG_EDGE_RETURN),
               "RETURN has an explicit procedure-exit edge");
        expect(has_edge(&graph.nodes[2], graph.image_termination, F2C_CFG_EDGE_STOP),
               "STOP has an explicit image-termination edge");
        expect(!graph.nodes[2].reachable && !graph.nodes[3].reachable,
               "statements after an unconditional RETURN are unreachable");
        expect(graph.nodes[graph.procedure_exit].reachable,
               "the explicit RETURN reaches the procedure exit");
        expect(!graph.nodes[graph.image_termination].reachable,
               "an unreachable STOP does not make image termination reachable");
        verify_connectivity(&graph);
    }
    f2c_control_flow_free(&graph);
}

static void test_linear_basic_block(void) {
    F2cStatement statements[4];
    Unit unit;
    F2cControlFlowGraph graph;
    memset(statements, 0, sizeof(statements));
    memset(&unit, 0, sizeof(unit));
    statements[0].kind = F2C_STMT_ASSIGNMENT;
    statements[1].kind = F2C_STMT_ASSIGNMENT;
    statements[2].kind = F2C_STMT_ASSIGNMENT;
    statements[3].kind = F2C_STMT_RETURN;
    unit.statements = statements;
    unit.statement_count = 4U;
    expect(f2c_control_flow_build(NULL, &unit, &graph),
           "a linear statement sequence produces a valid CFG");
    if (graph.node_count != 0U) {
        expect(graph.nodes[0].block_index == graph.nodes[1].block_index &&
                   graph.nodes[1].block_index == graph.nodes[2].block_index &&
                   graph.nodes[2].block_index == graph.nodes[3].block_index,
               "a maximal linear statement sequence forms one basic block");
        expect(graph.blocks[graph.nodes[0].block_index].node_count == 4U,
               "the linear basic block contains every sequential statement");
        expect(graph.nodes[graph.procedure_exit].block_index != graph.nodes[0].block_index,
               "the synthetic procedure exit starts a separate basic block");
        verify_connectivity(&graph);
    }
    f2c_control_flow_free(&graph);
}

static void test_conditional_loop_transfer(void) {
    F2cStatement statements[5];
    F2cStatement nested_exit;
    F2cExpr loop_control;
    Unit unit;
    F2cControlFlowGraph graph;
    size_t latch;
    memset(statements, 0, sizeof(statements));
    memset(&nested_exit, 0, sizeof(nested_exit));
    memset(&loop_control, 0, sizeof(loop_control));
    memset(&unit, 0, sizeof(unit));
    statements[0].kind = F2C_STMT_DO;
    statements[0].right = &loop_control;
    statements[1].kind = F2C_STMT_IF;
    statements[1].nested = &nested_exit;
    nested_exit.kind = F2C_STMT_EXIT;
    nested_exit.control_target = &statements[0];
    statements[2].kind = F2C_STMT_ASSIGNMENT;
    statements[3].kind = F2C_STMT_END_DO;
    statements[3].construct_owner = &statements[0];
    statements[4].kind = F2C_STMT_RETURN;
    unit.statements = statements;
    unit.statement_count = 5U;
    expect(f2c_control_flow_build(NULL, &unit, &graph),
           "a single-line conditional EXIT produces a valid loop CFG");
    latch = unit.statement_count + 2U;
    if (graph.node_count > latch) {
        expect(graph.nodes[latch].kind == F2C_CFG_NODE_LOOP_LATCH,
               "a loop has a distinct synthetic latch node");
        expect(has_edge(&graph.nodes[1], 2U, F2C_CFG_EDGE_FALLTHROUGH),
               "the false path of a single-line IF falls through");
        expect(has_edge(&graph.nodes[1], 4U, F2C_CFG_EDGE_LOOP_EXIT),
               "the true path of a single-line IF reaches the loop exit");
        expect(has_edge(&graph.nodes[3], latch, F2C_CFG_EDGE_FALLTHROUGH),
               "END DO transfers to the synthetic latch");
        expect(has_edge(&graph.nodes[latch], 0U, F2C_CFG_EDGE_LOOP_BACK),
               "the latch owns the loop-back edge");
        expect(has_edge(&graph.nodes[latch], 4U, F2C_CFG_EDGE_LOOP_EXIT),
               "a counted-loop latch owns its exit edge");
        verify_connectivity(&graph);
    }
    f2c_control_flow_free(&graph);
}

static void test_shared_terminal_loop_chain(void) {
    F2cStatement statements[5];
    F2cStatement terminal_action;
    F2cStatement *terminal_loops[2];
    F2cExpr outer_control;
    F2cExpr inner_control;
    Unit unit;
    F2cControlFlowGraph graph;
    size_t outer_latch;
    size_t inner_latch;
    memset(statements, 0, sizeof(statements));
    memset(&terminal_action, 0, sizeof(terminal_action));
    memset(&outer_control, 0, sizeof(outer_control));
    memset(&inner_control, 0, sizeof(inner_control));
    memset(&unit, 0, sizeof(unit));
    statements[0].kind = F2C_STMT_DO;
    statements[0].right = &outer_control;
    statements[1].kind = F2C_STMT_DO;
    statements[1].right = &inner_control;
    statements[2].kind = F2C_STMT_ASSIGNMENT;
    statements[3].kind = F2C_STMT_LABEL;
    statements[3].nested = &terminal_action;
    terminal_action.kind = F2C_STMT_CONTINUE;
    terminal_loops[0] = &statements[1];
    terminal_loops[1] = &statements[0];
    statements[3].terminal_loops = terminal_loops;
    statements[3].terminal_loop_count = 2U;
    statements[4].kind = F2C_STMT_RETURN;
    unit.statements = statements;
    unit.statement_count = 5U;
    expect(f2c_control_flow_build(NULL, &unit, &graph),
           "shared labeled DO termination produces a valid CFG");
    outer_latch = unit.statement_count + 2U;
    inner_latch = outer_latch + 1U;
    if (graph.node_count > inner_latch) {
        expect(has_edge(&graph.nodes[3], inner_latch, F2C_CFG_EDGE_FALLTHROUGH),
               "the shared terminal action enters only the innermost latch");
        expect(!has_edge(&graph.nodes[3], outer_latch, F2C_CFG_EDGE_FALLTHROUGH),
               "the shared terminal action does not bypass the inner latch");
        expect(has_edge(&graph.nodes[inner_latch], outer_latch, F2C_CFG_EDGE_LOOP_EXIT),
               "the inner-loop exit continues through the outer latch");
        expect(has_edge(&graph.nodes[outer_latch], 4U, F2C_CFG_EDGE_LOOP_EXIT),
               "the outer-loop exit continues after the shared terminal statement");
        expect(has_edge(&graph.nodes[inner_latch], 1U, F2C_CFG_EDGE_LOOP_BACK) &&
                   has_edge(&graph.nodes[outer_latch], 0U, F2C_CFG_EDGE_LOOP_BACK),
               "each shared loop has its own precise back edge");
        verify_connectivity(&graph);
    }
    f2c_control_flow_free(&graph);
}

static void test_io_and_alternate_return_edges(void) {
    F2cStatement statements[4];
    F2cStatement label_action;
    F2cIoControl controls[1];
    F2cExpr end_label;
    char *alternate_labels[1];
    Unit unit;
    F2cControlFlowGraph graph;
    memset(statements, 0, sizeof(statements));
    memset(&label_action, 0, sizeof(label_action));
    memset(controls, 0, sizeof(controls));
    memset(&end_label, 0, sizeof(end_label));
    memset(&unit, 0, sizeof(unit));
    statements[0].kind = F2C_STMT_READ;
    statements[0].io_controls = controls;
    statements[0].control_count = 1U;
    controls[0].kind = F2C_IO_CONTROL_END;
    controls[0].value = &end_label;
    end_label.text = (char *)"100";
    statements[1].kind = F2C_STMT_CALL;
    statements[1].labels = alternate_labels;
    statements[1].label_count = 1U;
    alternate_labels[0] = (char *)"100";
    statements[2].kind = F2C_STMT_RETURN;
    statements[3].kind = F2C_STMT_LABEL;
    statements[3].name = (char *)"100";
    statements[3].nested = &label_action;
    label_action.kind = F2C_STMT_CONTINUE;
    unit.statements = statements;
    unit.statement_count = 4U;
    expect(f2c_control_flow_build(NULL, &unit, &graph),
           "I/O and alternate-return transfers produce a valid CFG");
    if (graph.node_count != 0U) {
        expect(has_edge(&graph.nodes[0], 3U, F2C_CFG_EDGE_IO_END),
               "READ END= has a distinct end-of-file edge");
        expect(has_edge(&graph.nodes[0], graph.image_termination, F2C_CFG_EDGE_IO_ERROR),
               "an unhandled READ error has an implicit termination edge");
        expect(has_edge(&graph.nodes[1], 3U, F2C_CFG_EDGE_ALTERNATE_RETURN),
               "alternate return has a distinct call edge");
        expect(graph.nodes[graph.image_termination].reachable,
               "an unhandled I/O error makes image termination reachable");
        verify_connectivity(&graph);
    }
    f2c_control_flow_free(&graph);
}

int main(void) {
    test_empty_unit();
    test_explicit_terminal_edges();
    test_linear_basic_block();
    test_conditional_loop_transfer();
    test_shared_terminal_loop_chain();
    test_io_and_alternate_return_edges();
    if (failures != 0)
        fprintf(stderr, "%d control-flow graph test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
