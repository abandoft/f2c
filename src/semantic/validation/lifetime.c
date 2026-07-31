#include "semantic/validation/private.h"

#include "semantic/data_flow.h"

#include <stdlib.h>
#include <string.h>

static int block_scoped_symbol(const Unit *unit, const Symbol *symbol) {
    return symbol->scope_begin_line != 0U && !unit->save_all && !symbol->saved &&
           symbol->initializer == NULL && !symbol->argument && !symbol->module_entity;
}

static size_t node_line(const Unit *unit, const F2cControlFlowGraph *graph, size_t node) {
    const F2cControlFlowNode *item;
    if (node >= graph->node_count)
        return 0U;
    item = &graph->nodes[node];
    return item->statement_index < unit->statement_count
               ? unit->statements[item->statement_index].line
               : 0U;
}

static int graph_has_edge(const F2cControlFlowGraph *graph, size_t source, size_t target) {
    const F2cControlFlowNode *node;
    size_t edge;
    if (source >= graph->node_count || target >= graph->node_count)
        return 0;
    node = &graph->nodes[source];
    for (edge = 0U; edge < node->successor_count; ++edge)
        if (node->successors[edge].target == target)
            return 1;
    return 0;
}

static int build_cleanup_plan(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                              size_t source_node, size_t target_node, int finalize_scopes,
                              F2cScopeCleanupPlan *plan, const F2cSourceSpan *span) {
    const size_t source_line = node_line(unit, graph, source_node);
    const size_t target_line = node_line(unit, graph, target_node);
    size_t count = 0U;
    size_t index;
    free(plan->symbols);
    free(plan->released_temporaries);
    memset(plan, 0, sizeof(*plan));
    if (!graph_has_edge(graph, source_node, target_node)) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_INTERNAL, span, 1,
            "control-flow cleanup planning found no graph edge from node %zu to node %zu",
            source_node, target_node);
        return 0;
    }
    plan->source_node = source_node;
    plan->target_node = target_node;
    plan->control_flow_analyzed = 1;
    plan->variable_liveness_analyzed = unit->variable_flow.analyzed;
    if (!plan->variable_liveness_analyzed) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_INTERNAL, span, 1,
            "control-flow cleanup planning requires ordinary-variable liveness");
        return 0;
    }
    if (!finalize_scopes)
        return 1;
    for (index = unit->symbol_count; index != 0U; --index) {
        Symbol *symbol = &unit->symbols[index - 1U];
        const int source_inside =
            source_line > symbol->scope_begin_line && source_line < symbol->scope_end_line;
        const int target_inside =
            target_line > symbol->scope_begin_line && target_line < symbol->scope_end_line;
        if (block_scoped_symbol(unit, symbol) && source_inside && !target_inside) {
            if (f2c_variable_flow_is_live_in(unit, target_node, symbol)) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                    "control transfer leaves the scope of '%s' but its value remains live "
                    "at the target",
                    symbol->name);
                return 0;
            }
            ++count;
        }
    }
    if (count == 0U)
        return 1;
    if (count > SIZE_MAX / sizeof(*plan->symbols))
        goto failed;
    plan->symbols = (Symbol **)calloc(count, sizeof(*plan->symbols));
    if (plan->symbols == NULL)
        goto failed;
    for (index = unit->symbol_count; index != 0U; --index) {
        Symbol *symbol = &unit->symbols[index - 1U];
        const int source_inside =
            source_line > symbol->scope_begin_line && source_line < symbol->scope_end_line;
        const int target_inside =
            target_line > symbol->scope_begin_line && target_line < symbol->scope_end_line;
        if (block_scoped_symbol(unit, symbol) && source_inside && !target_inside)
            plan->symbols[plan->symbol_count++] = symbol;
    }
    return 1;

failed:
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                             "out of memory while planning control-flow cleanup");
    return 0;
}

static size_t label_target(const Unit *unit, const char *label) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *root = &unit->statements[index];
        if (root->kind == F2C_STMT_LABEL && root->name != NULL &&
            f2c_statement_labels_equal(root->name, label))
            return index;
    }
    return SIZE_MAX;
}

static int build_label_plan(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                            size_t source_node, const char *label, const F2cSourceSpan *span,
                            F2cScopeCleanupPlan *plan) {
    const size_t target = label_target(unit, label);
    return target == SIZE_MAX ||
           build_cleanup_plan(context, unit, graph, source_node, target, 1, plan, span);
}

static int prepare_label_plans(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                               size_t source_node, F2cStatement *statement) {
    size_t label;
    if (statement->label_count == 0U)
        return 1;
    statement->label_cleanups =
        (F2cScopeCleanupPlan *)calloc(statement->label_count, sizeof(*statement->label_cleanups));
    if (statement->label_cleanups == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &statement->span, 1,
                                 "out of memory while planning labeled control flow");
        return 0;
    }
    for (label = 0U; label < statement->label_count; ++label) {
        const F2cSourceSpan *span =
            statement->label_spans != NULL ? &statement->label_spans[label] : &statement->span;
        if (!build_label_plan(context, unit, graph, source_node, statement->labels[label], span,
                              &statement->label_cleanups[label]))
            return 0;
    }
    return 1;
}

static int prepare_assigned_targets(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                                    size_t source_node, F2cStatement *statement) {
    size_t index;
    for (index = 0U; index < statement->resolved_branch_count; ++index) {
        F2cResolvedBranch *branch = &statement->resolved_branches[index];
        if (!build_label_plan(context, unit, graph, source_node, branch->label, &statement->span,
                              &branch->cleanup))
            return 0;
    }
    return 1;
}

static int prepare_io_plans(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                            size_t source_node, F2cStatement *statement) {
    size_t control;
    for (control = 0U; control < statement->control_count; ++control) {
        F2cIoControl *item = &statement->io_controls[control];
        if ((item->kind != F2C_IO_CONTROL_END && item->kind != F2C_IO_CONTROL_EOR &&
             item->kind != F2C_IO_CONTROL_ERR) ||
            item->value == NULL || item->value->text == NULL)
            continue;
        if (!build_label_plan(context, unit, graph, source_node, item->value->text, &item->span,
                              &item->cleanup))
            return 0;
    }
    return 1;
}

static size_t transfer_target(const F2cControlFlowGraph *graph, size_t source_node,
                              F2cControlFlowEdgeKind kind) {
    const F2cControlFlowNode *source;
    size_t edge;
    if (source_node >= graph->node_count)
        return SIZE_MAX;
    source = &graph->nodes[source_node];
    for (edge = 0U; edge < source->successor_count; ++edge)
        if (source->successors[edge].kind == kind)
            return source->successors[edge].target;
    return SIZE_MAX;
}

static int prepare_statement_plans(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                                   size_t source_node, F2cStatement *statement) {
    size_t target;
    if (statement == NULL)
        return 1;
    if ((statement->kind == F2C_STMT_ARITHMETIC_IF || statement->kind == F2C_STMT_GOTO ||
         (statement->kind == F2C_STMT_CALL && statement->label_count != 0U)) &&
        !prepare_label_plans(context, unit, graph, source_node, statement))
        return 0;
    if (statement->kind == F2C_STMT_GOTO && statement->name != NULL &&
        !build_label_plan(context, unit, graph, source_node, statement->name,
                          &statement->label_span, &statement->transfer_cleanup))
        return 0;
    if (statement->kind == F2C_STMT_CYCLE || statement->kind == F2C_STMT_EXIT) {
        target = transfer_target(graph, source_node,
                                 statement->kind == F2C_STMT_CYCLE ? F2C_CFG_EDGE_LOOP_BACK
                                                                   : F2C_CFG_EDGE_LOOP_EXIT);
        if (target != SIZE_MAX &&
            !build_cleanup_plan(context, unit, graph, source_node, target, 1,
                                &statement->transfer_cleanup, &statement->span))
            return 0;
    }
    if (statement->kind == F2C_STMT_RETURN &&
        !build_cleanup_plan(context, unit, graph, source_node, graph->procedure_exit, 1,
                            &statement->transfer_cleanup, &statement->span))
        return 0;
    if (statement->kind == F2C_STMT_STOP &&
        !build_cleanup_plan(context, unit, graph, source_node, graph->image_termination, 0,
                            &statement->transfer_cleanup, &statement->span))
        return 0;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO &&
        !prepare_assigned_targets(context, unit, graph, source_node, statement))
        return 0;
    if (!prepare_io_plans(context, unit, graph, source_node, statement))
        return 0;
    return statement->nested == NULL ||
           prepare_statement_plans(context, unit, graph, source_node, statement->nested);
}

void f2c_validation_lifetimes(Context *context, Unit *unit) {
    F2cControlFlowGraph graph;
    size_t statement;
    if (!f2c_control_flow_build(context, unit, &graph))
        return;
    if (!f2c_variable_flow_analyze(context, unit, &graph)) {
        f2c_control_flow_free(&graph);
        return;
    }
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!prepare_statement_plans(context, unit, &graph, statement,
                                     &unit->statements[statement]))
            break;
    f2c_control_flow_free(&graph);
}

static int attach_temporary_cleanup_plan(Context *context, Unit *unit,
                                         const F2cControlFlowGraph *graph,
                                         F2cScopeCleanupPlan *plan, const F2cSourceSpan *span) {
    const F2cStatementTemporaryPlan *statement_plan;
    size_t item;
    free(plan->released_temporaries);
    plan->released_temporaries = NULL;
    plan->released_temporary_count = 0U;
    plan->temporary_liveness_analyzed = 0;
    if (!plan->control_flow_analyzed)
        return 1;
    if (!unit->temporary_flow.analyzed || plan->source_node >= graph->node_count ||
        plan->target_node >= graph->node_count ||
        !graph_has_edge(graph, plan->source_node, plan->target_node)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, span, 1,
                                 "control-flow cleanup planning requires owned-temporary liveness");
        return 0;
    }
    if (graph->nodes[plan->source_node].statement_index >= unit->statement_count) {
        plan->temporary_liveness_analyzed = 1;
        return 1;
    }
    statement_plan =
        &unit->statements[graph->nodes[plan->source_node].statement_index].temporary_plan;
    if (!statement_plan->ownership_analyzed) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, span, 1,
                                 "control-flow cleanup has no statement ownership plan");
        return 0;
    }
    if (statement_plan->owned_temporary_count != 0U) {
        if (statement_plan->owned_temporary_count > SIZE_MAX / sizeof(*plan->released_temporaries))
            goto failed;
        plan->released_temporaries = (size_t *)malloc(statement_plan->owned_temporary_count *
                                                      sizeof(*plan->released_temporaries));
        if (plan->released_temporaries == NULL)
            goto failed;
    }
    for (item = 0U; item < statement_plan->owned_temporary_count; ++item) {
        const size_t temporary = statement_plan->owned_temporaries[item];
        if (!f2c_temporary_flow_is_created(unit, plan->source_node, temporary) ||
            !f2c_temporary_flow_is_released(unit, plan->source_node, temporary) ||
            f2c_temporary_flow_is_live_out(unit, plan->source_node, temporary)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, span, 1,
                                     "owned temporary %zu is not released on control-flow edge",
                                     temporary);
            return 0;
        }
        plan->released_temporaries[plan->released_temporary_count++] = temporary;
    }
    plan->temporary_liveness_analyzed = 1;
    return 1;

failed:
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                             "out of memory while attaching temporary cleanup proof");
    return 0;
}

static int attach_statement_temporary_plans(Context *context, Unit *unit,
                                            const F2cControlFlowGraph *graph,
                                            F2cStatement *statement) {
    size_t item;
    if (statement == NULL)
        return 1;
    if (statement->transfer_cleanup.control_flow_analyzed &&
        !attach_temporary_cleanup_plan(context, unit, graph, &statement->transfer_cleanup,
                                       &statement->span))
        return 0;
    for (item = 0U; item < statement->label_count; ++item)
        if (statement->label_cleanups != NULL &&
            statement->label_cleanups[item].control_flow_analyzed &&
            !attach_temporary_cleanup_plan(
                context, unit, graph, &statement->label_cleanups[item],
                statement->label_spans != NULL ? &statement->label_spans[item] : &statement->span))
            return 0;
    for (item = 0U; item < statement->resolved_branch_count; ++item)
        if (statement->resolved_branches[item].cleanup.control_flow_analyzed &&
            !attach_temporary_cleanup_plan(context, unit, graph,
                                           &statement->resolved_branches[item].cleanup,
                                           &statement->span))
            return 0;
    for (item = 0U; item < statement->control_count; ++item)
        if (statement->io_controls[item].cleanup.control_flow_analyzed &&
            !attach_temporary_cleanup_plan(context, unit, graph,
                                           &statement->io_controls[item].cleanup,
                                           &statement->io_controls[item].span))
            return 0;
    return attach_statement_temporary_plans(context, unit, graph, statement->nested);
}

int f2c_analyze_temporary_lifetimes(Context *context, Unit *unit) {
    F2cControlFlowGraph graph;
    size_t statement;
    if (context == NULL || unit == NULL || !unit->expression_lifetimes_analyzed)
        return 0;
    if (!f2c_control_flow_build(context, unit, &graph))
        return 0;
    if (!f2c_temporary_flow_analyze(context, unit, &graph)) {
        f2c_control_flow_free(&graph);
        return 0;
    }
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!attach_statement_temporary_plans(context, unit, &graph,
                                              &unit->statements[statement])) {
            f2c_control_flow_free(&graph);
            return 0;
        }
    f2c_control_flow_free(&graph);
    return 1;
}
