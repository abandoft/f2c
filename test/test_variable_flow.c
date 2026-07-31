#include "semantic/data_flow.h"

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

static F2cExpr name_expression(Symbol *symbol) {
    F2cExpr expression;
    memset(&expression, 0, sizeof(expression));
    expression.kind = F2C_EXPR_NAME;
    expression.symbol = symbol;
    expression.definable = 1;
    return expression;
}

static void test_linear_def_use_and_kill(void) {
    Symbol symbols[3];
    F2cExpr x_left;
    F2cExpr y_value;
    F2cExpr z_left;
    F2cExpr x_value;
    F2cStatement statements[3];
    F2cControlFlowGraph graph;
    Unit unit;
    memset(symbols, 0, sizeof(symbols));
    memset(statements, 0, sizeof(statements));
    memset(&unit, 0, sizeof(unit));
    symbols[0].name = (char *)"x";
    symbols[1].name = (char *)"y";
    symbols[2].name = (char *)"z";
    x_left = name_expression(&symbols[0]);
    y_value = name_expression(&symbols[1]);
    z_left = name_expression(&symbols[2]);
    x_value = name_expression(&symbols[0]);
    statements[0].kind = F2C_STMT_ASSIGNMENT;
    statements[0].left = &x_left;
    statements[0].right = &y_value;
    statements[1].kind = F2C_STMT_ASSIGNMENT;
    statements[1].left = &z_left;
    statements[1].right = &x_value;
    statements[2].kind = F2C_STMT_RETURN;
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    unit.statements = statements;
    unit.statement_count = 3U;
    expect(f2c_control_flow_build(NULL, &unit, &graph), "a linear variable-flow CFG is built");
    expect(f2c_variable_flow_analyze(NULL, &unit, &graph),
           "ordinary-variable flow is analyzed on a linear CFG");
    expect(f2c_variable_flow_is_defined(&unit, 0U, &symbols[0]),
           "assignment records its whole-variable definition");
    expect(f2c_variable_flow_is_used(&unit, 0U, &symbols[1]),
           "assignment records its right-hand-side use");
    expect(!f2c_variable_flow_is_live_in(&unit, 0U, &symbols[0]),
           "a whole-variable definition kills the prior value");
    expect(f2c_variable_flow_is_live_out(&unit, 0U, &symbols[0]),
           "a value consumed by the next statement is live out");
    expect(f2c_variable_flow_is_live_in(&unit, 0U, &symbols[1]),
           "a right-hand-side value is live in");
    expect(!f2c_variable_flow_is_live_out(&unit, 1U, &symbols[2]),
           "an unused assignment result is dead immediately");
    f2c_variable_flow_clear(&unit);
    f2c_control_flow_free(&graph);
}

static void test_call_intents(void) {
    char *arguments[4] = {(char *)"input", (char *)"output", (char *)"update", (char *)"copy"};
    Symbol dummies[4];
    Symbol actuals[4];
    F2cExpr expressions[4];
    F2cExpr *argument_expressions[4];
    F2cStatement statement;
    F2cControlFlowGraph graph;
    Unit definition;
    Unit unit;
    size_t index;
    memset(dummies, 0, sizeof(dummies));
    memset(actuals, 0, sizeof(actuals));
    memset(&statement, 0, sizeof(statement));
    memset(&definition, 0, sizeof(definition));
    memset(&unit, 0, sizeof(unit));
    dummies[0].name = arguments[0];
    dummies[0].intent = F2C_INTENT_IN;
    dummies[1].name = arguments[1];
    dummies[1].intent = F2C_INTENT_OUT;
    dummies[2].name = arguments[2];
    dummies[2].intent = F2C_INTENT_INOUT;
    dummies[3].name = arguments[3];
    dummies[3].value = 1;
    definition.arguments = arguments;
    definition.argument_count = 4U;
    definition.symbols = dummies;
    definition.symbol_count = 4U;
    for (index = 0U; index < 4U; ++index) {
        actuals[index].name = arguments[index];
        expressions[index] = name_expression(&actuals[index]);
        argument_expressions[index] = &expressions[index];
    }
    statement.kind = F2C_STMT_CALL;
    statement.arguments = argument_expressions;
    statement.item_count = 4U;
    statement.resolved_procedure = &definition;
    unit.symbols = actuals;
    unit.symbol_count = 4U;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_control_flow_build(NULL, &unit, &graph), "a call variable-flow CFG is built");
    expect(f2c_variable_flow_analyze(NULL, &unit, &graph),
           "procedure intents drive ordinary-variable effects");
    expect(f2c_variable_flow_is_used(&unit, 0U, &actuals[0]) &&
               !f2c_variable_flow_is_defined(&unit, 0U, &actuals[0]),
           "INTENT(IN) is a use without a definition");
    expect(!f2c_variable_flow_is_used(&unit, 0U, &actuals[1]) &&
               f2c_variable_flow_is_defined(&unit, 0U, &actuals[1]),
           "INTENT(OUT) is a whole-variable definition");
    expect(f2c_variable_flow_is_used(&unit, 0U, &actuals[2]) &&
               f2c_variable_flow_is_defined(&unit, 0U, &actuals[2]),
           "INTENT(INOUT) is both a use and a definition");
    expect(f2c_variable_flow_is_used(&unit, 0U, &actuals[3]) &&
               !f2c_variable_flow_is_defined(&unit, 0U, &actuals[3]),
           "VALUE copies from the actual argument without defining it");
    f2c_variable_flow_clear(&unit);
    f2c_control_flow_free(&graph);
}

static void test_counted_loop_latch(void) {
    Symbol symbols[3];
    F2cExpr iterator;
    F2cExpr initial;
    F2cExpr limit;
    F2cExpr sum_left;
    F2cExpr sum_value;
    F2cExpr iterator_value;
    F2cExpr add;
    F2cExpr *add_children[2];
    F2cStatement statements[4];
    F2cControlFlowGraph graph;
    Unit unit;
    size_t latch = SIZE_MAX;
    size_t node;
    memset(symbols, 0, sizeof(symbols));
    memset(statements, 0, sizeof(statements));
    memset(&add, 0, sizeof(add));
    memset(&unit, 0, sizeof(unit));
    symbols[0].name = (char *)"i";
    symbols[1].name = (char *)"first";
    symbols[2].name = (char *)"sum";
    iterator = name_expression(&symbols[0]);
    initial = name_expression(&symbols[1]);
    limit = name_expression(&symbols[1]);
    sum_left = name_expression(&symbols[2]);
    sum_value = name_expression(&symbols[2]);
    iterator_value = name_expression(&symbols[0]);
    add.kind = F2C_EXPR_BINARY;
    add.children = add_children;
    add.child_count = 2U;
    add_children[0] = &sum_value;
    add_children[1] = &iterator_value;
    statements[0].kind = F2C_STMT_DO;
    statements[0].left = &iterator;
    statements[0].right = &initial;
    statements[0].limit = &limit;
    statements[1].kind = F2C_STMT_ASSIGNMENT;
    statements[1].left = &sum_left;
    statements[1].right = &add;
    statements[2].kind = F2C_STMT_END_DO;
    statements[2].construct_owner = &statements[0];
    statements[3].kind = F2C_STMT_RETURN;
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    unit.statements = statements;
    unit.statement_count = 4U;
    expect(f2c_control_flow_build(NULL, &unit, &graph), "a counted-loop CFG is built");
    for (node = 0U; node < graph.node_count; ++node)
        if (graph.nodes[node].kind == F2C_CFG_NODE_LOOP_LATCH)
            latch = node;
    expect(latch != SIZE_MAX, "a counted loop exposes a synthetic latch");
    expect(f2c_variable_flow_analyze(NULL, &unit, &graph),
           "ordinary-variable flow includes synthetic loop nodes");
    if (latch != SIZE_MAX) {
        expect(f2c_variable_flow_is_used(&unit, latch, &symbols[0]),
               "the loop latch consumes the current iterator");
        expect(f2c_variable_flow_is_defined(&unit, latch, &symbols[0]),
               "the loop latch defines the next iterator");
        expect(f2c_variable_flow_is_live_in(&unit, latch, &symbols[0]),
               "the current iterator is live into the loop latch");
    }
    f2c_variable_flow_clear(&unit);
    f2c_control_flow_free(&graph);
}

static void test_block_reentry_kills_prior_incarnation(void) {
    Symbol value;
    Symbol component_symbol;
    F2cExpr value_name;
    F2cExpr component;
    F2cExpr literal;
    F2cExpr *component_children[1];
    F2cStatement statements[3];
    F2cControlFlowEdge successor0 = {1U, F2C_CFG_EDGE_FALLTHROUGH};
    F2cControlFlowEdge successor1 = {2U, F2C_CFG_EDGE_FALLTHROUGH};
    F2cControlFlowEdge successor2 = {0U, F2C_CFG_EDGE_LOOP_BACK};
    F2cControlFlowEdge predecessor0 = {2U, F2C_CFG_EDGE_LOOP_BACK};
    F2cControlFlowEdge predecessor1 = {0U, F2C_CFG_EDGE_FALLTHROUGH};
    F2cControlFlowEdge predecessor2 = {1U, F2C_CFG_EDGE_FALLTHROUGH};
    F2cControlFlowNode nodes[3];
    F2cControlFlowGraph graph;
    Unit unit;
    memset(&value, 0, sizeof(value));
    memset(&component_symbol, 0, sizeof(component_symbol));
    memset(&component, 0, sizeof(component));
    memset(&literal, 0, sizeof(literal));
    memset(statements, 0, sizeof(statements));
    memset(nodes, 0, sizeof(nodes));
    memset(&graph, 0, sizeof(graph));
    memset(&unit, 0, sizeof(unit));
    value.name = (char *)"value";
    value.scope_begin_line = 10U;
    value.scope_end_line = 30U;
    value_name = name_expression(&value);
    component.kind = F2C_EXPR_COMPONENT;
    component.symbol = &component_symbol;
    component.children = component_children;
    component.child_count = 1U;
    component.definable = 1;
    component_children[0] = &value_name;
    literal.kind = F2C_EXPR_INTEGER_LITERAL;
    statements[0].kind = F2C_STMT_BLOCK_SCOPE;
    statements[0].line = 10U;
    statements[1].kind = F2C_STMT_ASSIGNMENT;
    statements[1].line = 20U;
    statements[1].left = &component;
    statements[1].right = &literal;
    statements[2].kind = F2C_STMT_CYCLE;
    statements[2].line = 25U;
    nodes[0].successors = &successor0;
    nodes[0].successor_count = 1U;
    nodes[0].predecessors = &predecessor0;
    nodes[0].predecessor_count = 1U;
    nodes[0].kind = F2C_CFG_NODE_STATEMENT;
    nodes[0].statement_index = 0U;
    nodes[1].successors = &successor1;
    nodes[1].successor_count = 1U;
    nodes[1].predecessors = &predecessor1;
    nodes[1].predecessor_count = 1U;
    nodes[1].kind = F2C_CFG_NODE_STATEMENT;
    nodes[1].statement_index = 1U;
    nodes[2].successors = &successor2;
    nodes[2].successor_count = 1U;
    nodes[2].predecessors = &predecessor2;
    nodes[2].predecessor_count = 1U;
    nodes[2].kind = F2C_CFG_NODE_STATEMENT;
    nodes[2].statement_index = 2U;
    graph.nodes = nodes;
    graph.node_count = 3U;
    graph.statement_count = 3U;
    unit.symbols = &value;
    unit.symbol_count = 1U;
    unit.statements = statements;
    unit.statement_count = 3U;
    expect(f2c_variable_flow_analyze(NULL, &unit, &graph),
           "ordinary-variable flow converges across block reentry");
    expect(f2c_variable_flow_is_live_in(&unit, 1U, &value),
           "a component definition consumes the current block object");
    expect(!f2c_variable_flow_is_live_in(&unit, 0U, &value),
           "entering a block kills the prior object incarnation");
    expect(!f2c_variable_flow_is_live_out(&unit, 2U, &value),
           "a CYCLE target does not keep the destroyed incarnation live");
    f2c_variable_flow_clear(&unit);
}

int main(void) {
    test_linear_def_use_and_kill();
    test_call_intents();
    test_counted_loop_latch();
    test_block_reentry_kills_prior_incarnation();
    if (failures != 0)
        fprintf(stderr, "%d variable-flow test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
