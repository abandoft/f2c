#include "semantic/control_flow.h"

#include "ir/statement.h"

#include <stdlib.h>
#include <string.h>

static const F2cStatement *statement_body(const F2cStatement *statement) {
    return statement != NULL && statement->kind == F2C_STMT_LABEL && statement->nested != NULL
               ? statement->nested
               : statement;
}

static size_t label_index(const Unit *unit, const char *label) {
    size_t index;
    if (unit == NULL || label == NULL)
        return SIZE_MAX;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = &unit->statements[index];
        if (statement->kind == F2C_STMT_LABEL && statement->name != NULL &&
            f2c_statement_labels_equal(statement->name, label))
            return index;
    }
    return SIZE_MAX;
}

static int append_edge(F2cControlFlowGraph *graph, size_t source, size_t target,
                       F2cControlFlowEdgeKind kind) {
    F2cControlFlowNode *node;
    F2cControlFlowEdge *replacement;
    size_t index;
    size_t capacity;
    if (graph == NULL || source >= graph->node_count || target >= graph->node_count)
        return 1;
    node = &graph->nodes[source];
    for (index = 0U; index < node->successor_count; ++index) {
        if (node->successors[index].target == target && node->successors[index].kind == kind)
            return 1;
    }
    if (node->successor_count == node->successor_capacity) {
        capacity = node->successor_capacity == 0U ? 2U : node->successor_capacity * 2U;
        if (capacity < node->successor_capacity || capacity > SIZE_MAX / sizeof(*node->successors))
            return 0;
        replacement =
            (F2cControlFlowEdge *)realloc(node->successors, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        node->successors = replacement;
        node->successor_capacity = capacity;
    }
    node->successors[node->successor_count++] = (F2cControlFlowEdge){target, kind};
    return 1;
}

static void remove_fallthrough_edges(F2cControlFlowGraph *graph, size_t source) {
    F2cControlFlowNode *node;
    size_t read_index;
    size_t write_index = 0U;
    if (graph == NULL || source >= graph->node_count)
        return;
    node = &graph->nodes[source];
    for (read_index = 0U; read_index < node->successor_count; ++read_index)
        if (node->successors[read_index].kind != F2C_CFG_EDGE_FALLTHROUGH)
            node->successors[write_index++] = node->successors[read_index];
    node->successor_count = write_index;
}

static int is_terminal_transfer(const F2cStatement *statement) {
    const F2cStatement *body = statement_body(statement);
    if (body == NULL)
        return 0;
    return body->kind == F2C_STMT_RETURN || body->kind == F2C_STMT_STOP ||
           body->kind == F2C_STMT_ARITHMETIC_IF || body->kind == F2C_STMT_ASSIGNED_GOTO ||
           body->kind == F2C_STMT_CYCLE || body->kind == F2C_STMT_EXIT ||
           (body->kind == F2C_STMT_GOTO && body->label_count == 0U);
}

static int append_label_edge(const Unit *unit, F2cControlFlowGraph *graph, size_t source,
                             const char *label, F2cControlFlowEdgeKind kind) {
    const size_t target = label_index(unit, label);
    return target == SIZE_MAX || append_edge(graph, source, target, kind);
}

static int append_statement_label_edges(const Unit *unit, F2cControlFlowGraph *graph, size_t source,
                                        const F2cStatement *statement) {
    size_t label;
    if (statement == NULL)
        return 1;
    if (statement->kind == F2C_STMT_GOTO && statement->name != NULL &&
        !append_label_edge(unit, graph, source, statement->name, F2C_CFG_EDGE_BRANCH))
        return 0;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO && statement->assigned_labels_resolved) {
        for (label = 0U; label < statement->resolved_branch_count; ++label)
            if (!append_label_edge(unit, graph, source, statement->resolved_branches[label].label,
                                   F2C_CFG_EDGE_BRANCH))
                return 0;
    } else if (statement->kind == F2C_STMT_GOTO || statement->kind == F2C_STMT_ARITHMETIC_IF ||
               statement->kind == F2C_STMT_ASSIGNED_GOTO ||
               (statement->kind == F2C_STMT_CALL && statement->label_count != 0U)) {
        for (label = 0U; label < statement->label_count; ++label)
            if (!append_label_edge(unit, graph, source, statement->labels[label],
                                   F2C_CFG_EDGE_BRANCH))
                return 0;
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_BACKSPACE || statement->kind == F2C_STMT_ENDFILE ||
        statement->kind == F2C_STMT_INQUIRE || statement->kind == F2C_STMT_CLOSE) {
        for (label = 0U; label < statement->control_count; ++label) {
            const F2cIoControl *control = &statement->io_controls[label];
            if ((control->kind == F2C_IO_CONTROL_END || control->kind == F2C_IO_CONTROL_EOR ||
                 control->kind == F2C_IO_CONTROL_ERR) &&
                control->value != NULL && control->value->text != NULL &&
                !append_label_edge(unit, graph, source, control->value->text,
                                   F2C_CFG_EDGE_IO_ERROR))
                return 0;
        }
    }
    return statement->nested == NULL ||
           append_statement_label_edges(unit, graph, source, statement->nested);
}

static int same_symbol(const F2cExpr *left, const F2cExpr *right) {
    if (left == NULL || right == NULL)
        return 0;
    if (left->symbol != NULL && right->symbol != NULL)
        return left->symbol == right->symbol;
    return left->text != NULL && right->text != NULL && strcmp(left->text, right->text) == 0;
}

static int append_bare_assigned_edges(const Unit *unit, F2cControlFlowGraph *graph, size_t source,
                                      const F2cStatement *statement) {
    size_t index;
    if (statement == NULL)
        return 1;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO && !statement->assigned_labels_resolved &&
        statement->label_count == 0U && statement->expression != NULL) {
        for (index = 0U; index < unit->statement_count; ++index) {
            const F2cStatement *candidate = statement_body(&unit->statements[index]);
            if (candidate != NULL && candidate->kind == F2C_STMT_ASSIGN_LABEL &&
                candidate->label_count == 1U &&
                same_symbol(candidate->expression, statement->expression) &&
                !append_label_edge(unit, graph, source, candidate->labels[0], F2C_CFG_EDGE_BRANCH))
                return 0;
        }
    }
    return statement->nested == NULL ||
           append_bare_assigned_edges(unit, graph, source, statement->nested);
}

static size_t construct_end(const Unit *unit, const F2cStatement *opener) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *root = &unit->statements[index];
        const F2cStatement *body = statement_body(root);
        size_t loop;
        if (body != NULL && body->construct_owner == opener &&
            (body->kind == F2C_STMT_END_IF || body->kind == F2C_STMT_END_DO ||
             body->kind == F2C_STMT_END_SELECT || body->kind == F2C_STMT_END_BLOCK_SCOPE ||
             body->kind == F2C_STMT_END_WHERE))
            return index;
        for (loop = 0U; loop < root->terminal_loop_count; ++loop)
            if (root->terminal_loops[loop] == opener)
                return index;
    }
    return SIZE_MAX;
}

static int is_direct_boundary(const F2cStatement *statement, const F2cStatement *owner) {
    const F2cStatement *body = statement_body(statement);
    if (body == NULL || body->construct_owner != owner)
        return 0;
    if (owner->kind == F2C_STMT_IF)
        return body->kind == F2C_STMT_ELSE_IF || body->kind == F2C_STMT_ELSE;
    if (owner->kind == F2C_STMT_SELECT_CASE)
        return body->kind == F2C_STMT_CASE;
    if (owner->kind == F2C_STMT_SELECT_TYPE)
        return body->kind == F2C_STMT_TYPE_GUARD;
    if (owner->kind == F2C_STMT_WHERE)
        return body->kind == F2C_STMT_ELSEWHERE;
    return 0;
}

static size_t next_boundary(const Unit *unit, const F2cStatement *owner, size_t after, size_t end) {
    size_t index;
    for (index = after; index < end; ++index)
        if (is_direct_boundary(&unit->statements[index], owner))
            return index;
    return end;
}

static size_t branch_body_target(const Unit *unit, const F2cStatement *owner, size_t marker,
                                 size_t end) {
    if (marker + 1U >= end || is_direct_boundary(&unit->statements[marker + 1U], owner))
        return end;
    return marker + 1U;
}

static int build_if_edges(const Unit *unit, F2cControlFlowGraph *graph, size_t opener_index,
                          const F2cStatement *opener, size_t end) {
    const size_t first_boundary = next_boundary(unit, opener, opener_index + 1U, end);
    const size_t true_target = opener_index + 1U < first_boundary ? opener_index + 1U : end;
    size_t marker;
    remove_fallthrough_edges(graph, opener_index);
    if (!append_edge(graph, opener_index, true_target, F2C_CFG_EDGE_BRANCH))
        return 0;
    if (!append_edge(graph, opener_index, first_boundary, F2C_CFG_EDGE_BRANCH))
        return 0;
    for (marker = first_boundary; marker < end;
         marker = next_boundary(unit, opener, marker + 1U, end)) {
        const F2cStatement *body = statement_body(&unit->statements[marker]);
        const size_t following = next_boundary(unit, opener, marker + 1U, end);
        remove_fallthrough_edges(graph, marker);
        if (!append_edge(graph, marker, branch_body_target(unit, opener, marker, end),
                         F2C_CFG_EDGE_BRANCH))
            return 0;
        if (body != NULL && body->kind == F2C_STMT_ELSE_IF &&
            !append_edge(graph, marker, following, F2C_CFG_EDGE_BRANCH))
            return 0;
        if (following == end)
            break;
    }
    for (marker = opener_index + 1U; marker < end; ++marker)
        if (marker + 1U < end && is_direct_boundary(&unit->statements[marker + 1U], opener)) {
            remove_fallthrough_edges(graph, marker);
            if (!append_edge(graph, marker, end, F2C_CFG_EDGE_BRANCH))
                return 0;
        }
    return 1;
}

static int build_select_edges(const Unit *unit, F2cControlFlowGraph *graph, size_t opener_index,
                              const F2cStatement *opener, size_t end) {
    size_t marker;
    int has_default = 0;
    remove_fallthrough_edges(graph, opener_index);
    for (marker = opener_index + 1U; marker < end; ++marker) {
        const F2cStatement *body = statement_body(&unit->statements[marker]);
        if (!is_direct_boundary(&unit->statements[marker], opener))
            continue;
        if (body != NULL && ((body->kind == F2C_STMT_CASE && body->case_default) ||
                             (body->kind == F2C_STMT_TYPE_GUARD && body->case_default)))
            has_default = 1;
        if (!append_edge(graph, opener_index, marker, F2C_CFG_EDGE_BRANCH)) {
            return 0;
        }
        remove_fallthrough_edges(graph, marker);
        if (!append_edge(graph, marker, branch_body_target(unit, opener, marker, end),
                         F2C_CFG_EDGE_BRANCH))
            return 0;
    }
    if (!has_default && !append_edge(graph, opener_index, end, F2C_CFG_EDGE_BRANCH))
        return 0;
    for (marker = opener_index + 1U; marker < end; ++marker)
        if (marker + 1U < end && is_direct_boundary(&unit->statements[marker + 1U], opener)) {
            remove_fallthrough_edges(graph, marker);
            if (!append_edge(graph, marker, end, F2C_CFG_EDGE_BRANCH))
                return 0;
        }
    return 1;
}

static int build_loop_edges(const Unit *unit, F2cControlFlowGraph *graph, size_t opener_index,
                            const F2cStatement *opener, size_t end) {
    const int conditional = opener->kind == F2C_STMT_DO_WHILE || opener->right != NULL;
    remove_fallthrough_edges(graph, opener_index);
    if (opener_index + 1U <= end &&
        !append_edge(graph, opener_index, opener_index + 1U, F2C_CFG_EDGE_BRANCH))
        return 0;
    if (conditional && end + 1U < graph->node_count &&
        !append_edge(graph, opener_index, end + 1U, F2C_CFG_EDGE_LOOP_EXIT))
        return 0;
    remove_fallthrough_edges(graph, end);
    if (!append_edge(graph, end, opener_index, F2C_CFG_EDGE_LOOP_BACK))
        return 0;
    if (conditional && end + 1U < graph->node_count &&
        !append_edge(graph, end, end + 1U, F2C_CFG_EDGE_LOOP_EXIT))
        return 0;
    (void)unit;
    return 1;
}

static int build_construct_edges(const Unit *unit, F2cControlFlowGraph *graph) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *opener = statement_body(&unit->statements[index]);
        size_t end;
        if (opener == NULL)
            continue;
        if (!((opener->kind == F2C_STMT_IF && opener->block) ||
              opener->kind == F2C_STMT_SELECT_CASE || opener->kind == F2C_STMT_SELECT_TYPE ||
              opener->kind == F2C_STMT_DO || opener->kind == F2C_STMT_DO_WHILE))
            continue;
        end = construct_end(unit, opener);
        if (end == SIZE_MAX)
            continue;
        if (opener->kind == F2C_STMT_IF) {
            if (!build_if_edges(unit, graph, index, opener, end))
                return 0;
        } else if (opener->kind == F2C_STMT_SELECT_CASE || opener->kind == F2C_STMT_SELECT_TYPE) {
            if (!build_select_edges(unit, graph, index, opener, end))
                return 0;
        } else if (!build_loop_edges(unit, graph, index, opener, end)) {
            return 0;
        }
    }
    return 1;
}

static int build_control_transfer_edges(const Unit *unit, F2cControlFlowGraph *graph) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = statement_body(&unit->statements[index]);
        size_t end;
        if (statement == NULL ||
            (statement->kind != F2C_STMT_CYCLE && statement->kind != F2C_STMT_EXIT) ||
            statement->control_target == NULL)
            continue;
        end = construct_end(unit, statement->control_target);
        remove_fallthrough_edges(graph, index);
        if (statement->kind == F2C_STMT_CYCLE) {
            if (end != SIZE_MAX && !append_edge(graph, index, end, F2C_CFG_EDGE_LOOP_BACK))
                return 0;
        } else if (end != SIZE_MAX && end + 1U < graph->node_count &&
                   !append_edge(graph, index, end + 1U, F2C_CFG_EDGE_LOOP_EXIT)) {
            return 0;
        }
    }
    return 1;
}

static int mark_reachable(F2cControlFlowGraph *graph) {
    size_t *worklist;
    size_t head = 0U;
    size_t tail = 0U;
    if (graph == NULL || graph->node_count == 0U)
        return 1;
    worklist = (size_t *)malloc(graph->node_count * sizeof(*worklist));
    if (worklist == NULL)
        return 0;
    graph->nodes[0].reachable = 1;
    worklist[tail++] = 0U;
    while (head < tail) {
        const size_t node_index = worklist[head++];
        const F2cControlFlowNode *node = &graph->nodes[node_index];
        size_t edge;
        for (edge = 0U; edge < node->successor_count; ++edge) {
            const size_t target = node->successors[edge].target;
            if (!graph->nodes[target].reachable) {
                graph->nodes[target].reachable = 1;
                worklist[tail++] = target;
            }
        }
    }
    free(worklist);
    return 1;
}

int f2c_control_flow_build(Context *context, const Unit *unit, F2cControlFlowGraph *graph) {
    size_t index;
    if (graph == NULL)
        return 0;
    memset(graph, 0, sizeof(*graph));
    if (unit == NULL || unit->statement_count == 0U)
        return 1;
    if (unit->statement_count > SIZE_MAX / sizeof(*graph->nodes))
        goto failure;
    graph->nodes = (F2cControlFlowNode *)calloc(unit->statement_count, sizeof(*graph->nodes));
    if (graph->nodes == NULL)
        goto failure;
    graph->node_count = unit->statement_count;
    for (index = 0U; index + 1U < graph->node_count; ++index) {
        if (!is_terminal_transfer(&unit->statements[index]) &&
            !append_edge(graph, index, index + 1U, F2C_CFG_EDGE_FALLTHROUGH))
            goto failure;
    }
    for (index = 0U; index < graph->node_count; ++index) {
        if (!append_statement_label_edges(unit, graph, index, &unit->statements[index]) ||
            !append_bare_assigned_edges(unit, graph, index, &unit->statements[index]))
            goto failure;
    }
    if (!build_construct_edges(unit, graph) || !build_control_transfer_edges(unit, graph))
        goto failure;
    if (!mark_reachable(graph))
        goto failure;
    return 1;

failure:
    f2c_control_flow_free(graph);
    if (context != NULL)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &unit->header_span, 1,
                                 "out of memory while building the procedure control-flow graph");
    return 0;
}

void f2c_control_flow_free(F2cControlFlowGraph *graph) {
    size_t index;
    if (graph == NULL)
        return;
    for (index = 0U; index < graph->node_count; ++index)
        free(graph->nodes[index].successors);
    free(graph->nodes);
    memset(graph, 0, sizeof(*graph));
}
