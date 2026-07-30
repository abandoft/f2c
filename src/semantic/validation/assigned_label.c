#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

typedef struct AssignedLabelDefinition {
    const char *label;
    int branch_target;
    int format_target;
} AssignedLabelDefinition;

typedef struct AssignedLabelCatalog {
    AssignedLabelDefinition *items;
    size_t count;
    size_t capacity;
} AssignedLabelCatalog;

typedef struct AssignedLabelState {
    uint64_t *labels;
    int may_be_integer_or_undefined;
    int initialized;
} AssignedLabelState;

typedef enum AssignedLabelEffectKind {
    ASSIGNED_LABEL_EFFECT_NONE,
    ASSIGNED_LABEL_EFFECT_DEFINE,
    ASSIGNED_LABEL_EFFECT_INVALIDATE
} AssignedLabelEffectKind;

typedef struct AssignedLabelEffect {
    AssignedLabelEffectKind kind;
    size_t definition;
    int conditional;
} AssignedLabelEffect;

static const F2cStatement *statement_body(const F2cStatement *statement) {
    return statement != NULL && statement->kind == F2C_STMT_LABEL && statement->nested != NULL
               ? statement->nested
               : statement;
}

static int same_symbol(const F2cExpr *left, const F2cExpr *right) {
    if (left == NULL || right == NULL)
        return 0;
    if (left->symbol != NULL && right->symbol != NULL)
        return left->symbol == right->symbol;
    return left->text != NULL && right->text != NULL && strcmp(left->text, right->text) == 0;
}

static const F2cStatement *assigned_goto_for_variable(const F2cStatement *statement,
                                                      const F2cExpr *variable) {
    if (statement == NULL)
        return NULL;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO && same_symbol(statement->expression, variable))
        return statement;
    return assigned_goto_for_variable(statement->nested, variable);
}

static const F2cStatement *label_statement(const Unit *unit, const char *label) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = &unit->statements[index];
        if ((statement->kind == F2C_STMT_LABEL || statement->kind == F2C_STMT_FORMAT) &&
            statement->name != NULL && f2c_statement_labels_equal(statement->name, label))
            return statement;
    }
    return NULL;
}

static int branchable_statement(const F2cStatement *statement) {
    const F2cStatement *body = statement_body(statement);
    if (statement == NULL || statement->kind != F2C_STMT_LABEL || body == NULL)
        return 0;
    switch (body->kind) {
    case F2C_STMT_INVALID:
    case F2C_STMT_EMPTY:
    case F2C_STMT_DECLARATION:
    case F2C_STMT_FORMAT:
    case F2C_STMT_DATA:
    case F2C_STMT_CASE:
    case F2C_STMT_ELSE:
    case F2C_STMT_ELSE_IF:
    case F2C_STMT_ELSEWHERE:
    case F2C_STMT_TYPE_GUARD:
    case F2C_STMT_END_IF:
    case F2C_STMT_END_DO:
    case F2C_STMT_END_SELECT:
    case F2C_STMT_END_BLOCK_SCOPE:
    case F2C_STMT_END_WHERE:
        return 0;
    default:
        return 1;
    }
}

static size_t catalog_find(const AssignedLabelCatalog *catalog, const char *label) {
    size_t index;
    for (index = 0U; index < catalog->count; ++index)
        if (f2c_statement_labels_equal(catalog->items[index].label, label))
            return index;
    return SIZE_MAX;
}

static int catalog_append(AssignedLabelCatalog *catalog, const Unit *unit, const char *label) {
    const F2cStatement *target;
    AssignedLabelDefinition *replacement;
    size_t capacity;
    if (catalog_find(catalog, label) != SIZE_MAX)
        return 1;
    if (catalog->count == catalog->capacity) {
        capacity = catalog->capacity == 0U ? 4U : catalog->capacity * 2U;
        if (capacity < catalog->capacity || capacity > SIZE_MAX / sizeof(*catalog->items))
            return 0;
        replacement =
            (AssignedLabelDefinition *)realloc(catalog->items, capacity * sizeof(*catalog->items));
        if (replacement == NULL)
            return 0;
        catalog->items = replacement;
        catalog->capacity = capacity;
    }
    target = label_statement(unit, label);
    catalog->items[catalog->count++] =
        (AssignedLabelDefinition){label, branchable_statement(target),
                                  target != NULL && target->kind == F2C_STMT_FORMAT &&
                                      target->format != NULL && target->format->validated};
    return 1;
}

static int collect_definitions(const Unit *unit, const F2cStatement *statement,
                               const F2cExpr *variable, AssignedLabelCatalog *catalog) {
    if (statement == NULL)
        return 1;
    if (statement->kind == F2C_STMT_ASSIGN_LABEL && statement->label_count == 1U &&
        same_symbol(statement->expression, variable) &&
        !catalog_append(catalog, unit, statement->labels[0]))
        return 0;
    return statement->nested == NULL ||
           collect_definitions(unit, statement->nested, variable, catalog);
}

static int build_catalog(const Unit *unit, const F2cExpr *variable, AssignedLabelCatalog *catalog) {
    size_t index;
    memset(catalog, 0, sizeof(*catalog));
    for (index = 0U; index < unit->statement_count; ++index)
        if (!collect_definitions(unit, &unit->statements[index], variable, catalog)) {
            free(catalog->items);
            memset(catalog, 0, sizeof(*catalog));
            return 0;
        }
    return 1;
}

static int expression_defines_symbol(const F2cExpr *expression, const F2cExpr *variable) {
    if (same_symbol(expression, variable))
        return 1;
    if (expression == NULL || expression->child_count == 0U)
        return 0;
    return expression_defines_symbol(expression->children[0], variable);
}

static int io_item_defines_symbol(const F2cIoItem *item, const F2cExpr *variable) {
    size_t index;
    if (item == NULL)
        return 0;
    if (!item->implied_do && expression_defines_symbol(item->expression, variable))
        return 1;
    for (index = 0U; index < item->child_count; ++index)
        if (io_item_defines_symbol(&item->children[index], variable))
            return 1;
    return 0;
}

static int output_control(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_IOSTAT || kind == F2C_IO_CONTROL_IOMSG ||
           kind == F2C_IO_CONTROL_SIZE || kind == F2C_IO_CONTROL_NEWUNIT ||
           kind == F2C_IO_CONTROL_IOLENGTH || kind == F2C_IO_CONTROL_EXIST ||
           kind == F2C_IO_CONTROL_OPENED || kind == F2C_IO_CONTROL_NUMBER ||
           kind == F2C_IO_CONTROL_NAMED || kind == F2C_IO_CONTROL_NAME ||
           kind == F2C_IO_CONTROL_SEQUENTIAL || kind == F2C_IO_CONTROL_DIRECT ||
           kind == F2C_IO_CONTROL_FORMATTED || kind == F2C_IO_CONTROL_UNFORMATTED ||
           kind == F2C_IO_CONTROL_NEXTREC || kind == F2C_IO_CONTROL_POSITION ||
           kind == F2C_IO_CONTROL_READ || kind == F2C_IO_CONTROL_WRITE ||
           kind == F2C_IO_CONTROL_READWRITE;
}

static int allocation_output_keyword(const F2cExpr *argument);

static const F2cExpr *argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static int statement_invalidates_variable(const F2cStatement *statement, const F2cExpr *variable) {
    size_t index;
    if (statement == NULL)
        return 0;
    if (statement->kind == F2C_STMT_ASSIGNMENT &&
        expression_defines_symbol(statement->left, variable))
        return 1;
    if (statement->kind == F2C_STMT_POINTER_ASSIGNMENT &&
        expression_defines_symbol(statement->left, variable))
        return 1;
    if (statement->kind == F2C_STMT_DO && expression_defines_symbol(statement->left, variable))
        return 1;
    if (statement->kind == F2C_STMT_READ) {
        for (index = 0U; index < statement->io_item_count; ++index)
            if (io_item_defines_symbol(&statement->io_items[index], variable))
                return 1;
    }
    if (statement->kind == F2C_STMT_CALL) {
        for (index = 0U; index < statement->item_count; ++index) {
            const F2cExpr *actual =
                argument_value(statement->arguments != NULL ? statement->arguments[index] : NULL);
            if (expression_defines_symbol(actual, variable))
                return 1;
        }
    }
    if (statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE ||
        statement->kind == F2C_STMT_MOVE_ALLOC || statement->kind == F2C_STMT_NULLIFY) {
        for (index = 0U; index < statement->item_count; ++index) {
            const F2cExpr *argument =
                statement->arguments != NULL ? statement->arguments[index] : NULL;
            if ((statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE) &&
                argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                !allocation_output_keyword(argument))
                continue;
            if (expression_defines_symbol(argument_value(argument), variable))
                return 1;
        }
    }
    for (index = 0U; index < statement->control_count; ++index)
        if (output_control(statement->io_controls[index].kind) &&
            expression_defines_symbol(statement->io_controls[index].value, variable))
            return 1;
    return 0;
}

static AssignedLabelEffect statement_effect(const F2cStatement *statement, const F2cExpr *variable,
                                            const AssignedLabelCatalog *catalog) {
    AssignedLabelEffect effect = {ASSIGNED_LABEL_EFFECT_NONE, SIZE_MAX, 0};
    if (statement == NULL)
        return effect;
    if (statement->kind == F2C_STMT_LABEL && statement->nested != NULL)
        return statement_effect(statement->nested, variable, catalog);
    if (statement->kind == F2C_STMT_ASSIGN_LABEL && statement->label_count == 1U &&
        same_symbol(statement->expression, variable)) {
        effect.kind = ASSIGNED_LABEL_EFFECT_DEFINE;
        effect.definition = catalog_find(catalog, statement->labels[0]);
        return effect;
    }
    if (statement_invalidates_variable(statement, variable)) {
        effect.kind = ASSIGNED_LABEL_EFFECT_INVALIDATE;
        return effect;
    }
    if (statement->nested != NULL) {
        effect = statement_effect(statement->nested, variable, catalog);
        if (effect.kind != ASSIGNED_LABEL_EFFECT_NONE &&
            ((statement->kind == F2C_STMT_IF && !statement->block) ||
             (statement->kind == F2C_STMT_WHERE && !statement->block)))
            effect.conditional = 1;
    }
    return effect;
}

static void state_copy(uint64_t *target, const uint64_t *source, size_t words) {
    if (words != 0U)
        memcpy(target, source, words * sizeof(*target));
}

static void apply_effect(const AssignedLabelState *input, const AssignedLabelEffect *effect,
                         uint64_t *output, int *output_invalid, size_t words) {
    state_copy(output, input->labels, words);
    *output_invalid = input->may_be_integer_or_undefined;
    if (effect->kind == ASSIGNED_LABEL_EFFECT_NONE)
        return;
    if (!effect->conditional) {
        if (words != 0U)
            memset(output, 0, words * sizeof(*output));
        *output_invalid = effect->kind == ASSIGNED_LABEL_EFFECT_INVALIDATE;
    }
    if (effect->kind == ASSIGNED_LABEL_EFFECT_DEFINE && effect->definition != SIZE_MAX)
        output[effect->definition / 64U] |= UINT64_C(1) << (effect->definition % 64U);
    else if (effect->kind == ASSIGNED_LABEL_EFFECT_INVALIDATE)
        *output_invalid = 1;
}

static int output_has_definition(const uint64_t *output, size_t definition) {
    return output != NULL && (output[definition / 64U] & (UINT64_C(1) << (definition % 64U))) != 0U;
}

static int successor_is_feasible(const Unit *unit, const F2cControlFlowEdge *edge,
                                 const F2cStatement *assigned_goto,
                                 const AssignedLabelCatalog *catalog, const uint64_t *output) {
    const F2cStatement *target;
    size_t definition;
    if (assigned_goto == NULL || edge->kind != F2C_CFG_EDGE_BRANCH)
        return 1;
    if (edge->target >= unit->statement_count)
        return 0;
    target = &unit->statements[edge->target];
    if (target->kind != F2C_STMT_LABEL || target->name == NULL)
        return 1;
    definition = catalog_find(catalog, target->name);
    return definition != SIZE_MAX && output_has_definition(output, definition);
}

static int merge_state(AssignedLabelState *target, const uint64_t *labels, int invalid,
                       size_t words) {
    size_t word;
    int changed = !target->initialized;
    if (!target->initialized) {
        target->initialized = 1;
        target->may_be_integer_or_undefined = invalid;
        state_copy(target->labels, labels, words);
        return 1;
    }
    if (invalid && !target->may_be_integer_or_undefined) {
        target->may_be_integer_or_undefined = 1;
        changed = 1;
    }
    for (word = 0U; word < words; ++word) {
        const uint64_t merged = target->labels[word] | labels[word];
        if (merged != target->labels[word]) {
            target->labels[word] = merged;
            changed = 1;
        }
    }
    return changed;
}

static int solve_states(const Unit *unit, const F2cControlFlowGraph *graph, const F2cExpr *variable,
                        const AssignedLabelCatalog *catalog, AssignedLabelState **states_out,
                        size_t *words_out) {
    AssignedLabelState *states;
    uint64_t *storage;
    uint64_t *output;
    size_t words = (catalog->count + 63U) / 64U;
    size_t index;
    int changed = 1;
    if (graph->node_count > SIZE_MAX / sizeof(*states) ||
        (words != 0U && graph->node_count > SIZE_MAX / words) ||
        (words != 0U && graph->node_count * words > SIZE_MAX / sizeof(*storage)))
        return 0;
    states = (AssignedLabelState *)calloc(graph->node_count, sizeof(*states));
    storage = words != 0U ? (uint64_t *)calloc(graph->node_count * words, sizeof(*storage)) : NULL;
    output = words != 0U ? (uint64_t *)calloc(words, sizeof(*output)) : NULL;
    if (states == NULL || (words != 0U && (storage == NULL || output == NULL))) {
        free(states);
        free(storage);
        free(output);
        return 0;
    }
    for (index = 0U; index < graph->node_count; ++index)
        states[index].labels = words != 0U ? storage + index * words : NULL;
    states[0].initialized = 1;
    states[0].may_be_integer_or_undefined = 1;
    while (changed) {
        changed = 0;
        for (index = 0U; index < graph->node_count; ++index) {
            const F2cControlFlowNode *node = &graph->nodes[index];
            const F2cStatement *assigned_goto;
            AssignedLabelEffect effect;
            int output_invalid;
            size_t edge;
            if (!states[index].initialized || !node->reachable)
                continue;
            effect = statement_effect(&unit->statements[index], variable, catalog);
            apply_effect(&states[index], &effect, output, &output_invalid, words);
            assigned_goto = assigned_goto_for_variable(&unit->statements[index], variable);
            for (edge = 0U; edge < node->successor_count; ++edge)
                if (successor_is_feasible(unit, &node->successors[edge], assigned_goto, catalog,
                                          output) &&
                    merge_state(&states[node->successors[edge].target], output, output_invalid,
                                words))
                    changed = 1;
        }
    }
    free(output);
    *states_out = states;
    *words_out = words;
    return 1;
}

static int state_has_label(const AssignedLabelState *state, size_t definition) {
    return state->initialized &&
           (state->labels[definition / 64U] & (UINT64_C(1) << (definition % 64U))) != 0U;
}

static void clear_resolved_branches(F2cStatement *statement) {
    while (statement->resolved_branch_count != 0U) {
        F2cResolvedBranch *branch =
            &statement->resolved_branches[--statement->resolved_branch_count];
        free(branch->label);
        free(branch->cleanup.symbols);
    }
    free(statement->resolved_branches);
    statement->resolved_branches = NULL;
}

static int append_resolved_branch(F2cStatement *statement, const char *label) {
    F2cResolvedBranch *replacement;
    F2cResolvedBranch *branch;
    size_t index;
    for (index = 0U; index < statement->resolved_branch_count; ++index)
        if (f2c_statement_labels_equal(statement->resolved_branches[index].label, label))
            return 1;
    if (statement->resolved_branch_count == SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement = (F2cResolvedBranch *)realloc(statement->resolved_branches,
                                               (statement->resolved_branch_count + 1U) *
                                                   sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    statement->resolved_branches = replacement;
    branch = &statement->resolved_branches[statement->resolved_branch_count];
    memset(branch, 0, sizeof(*branch));
    branch->label = f2c_strdup(f2c_statement_label_canonical(label));
    if (branch->label == NULL)
        return 0;
    ++statement->resolved_branch_count;
    return 1;
}

static void clear_resolved_formats(F2cIoControl *control) {
    while (control->resolved_label_count != 0U)
        free(control->resolved_labels[--control->resolved_label_count]);
    free(control->resolved_labels);
    control->resolved_labels = NULL;
}

static int append_resolved_format(F2cIoControl *control, const char *label) {
    char **replacement;
    char *copy;
    size_t index;
    for (index = 0U; index < control->resolved_label_count; ++index)
        if (f2c_statement_labels_equal(control->resolved_labels[index], label))
            return 1;
    if (control->resolved_label_count == SIZE_MAX / sizeof(*replacement))
        return 0;
    copy = f2c_strdup(f2c_statement_label_canonical(label));
    if (copy == NULL)
        return 0;
    replacement = (char **)realloc(control->resolved_labels,
                                   (control->resolved_label_count + 1U) * sizeof(*replacement));
    if (replacement == NULL) {
        free(copy);
        return 0;
    }
    control->resolved_labels = replacement;
    control->resolved_labels[control->resolved_label_count++] = copy;
    return 1;
}

static int allowed_label(const F2cStatement *statement, const char *label) {
    size_t index;
    if (statement->label_count == 0U)
        return 1;
    for (index = 0U; index < statement->label_count; ++index)
        if (f2c_statement_labels_equal(statement->labels[index], label))
            return 1;
    return 0;
}

static int resolve_goto(Context *context, F2cStatement *statement,
                        const AssignedLabelCatalog *catalog, const AssignedLabelState *state,
                        int reachable, int diagnose) {
    size_t definition;
    int success = 1;
    clear_resolved_branches(statement);
    statement->assigned_labels_resolved = 1;
    if (diagnose && reachable && state->may_be_integer_or_undefined) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
            "assigned GOTO variable '%s' is not defined with a statement label on every "
            "reachable path",
            statement->name != NULL ? statement->name : "<unknown>");
    }
    for (definition = 0U; definition < catalog->count; ++definition) {
        const AssignedLabelDefinition *item = &catalog->items[definition];
        const int reaches = reachable && state_has_label(state, definition);
        if (!reaches)
            continue;
        if (!item->branch_target) {
            if (diagnose)
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                    "assigned GOTO variable '%s' may hold nonexecutable FORMAT label %s",
                    statement->name != NULL ? statement->name : "<unknown>", item->label);
            continue;
        }
        if (!allowed_label(statement, item->label)) {
            if (diagnose)
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                    "assigned GOTO variable '%s' may hold label %s outside its allowed label list",
                    statement->name != NULL ? statement->name : "<unknown>", item->label);
            continue;
        }
        if (!append_resolved_branch(statement, item->label))
            success = 0;
    }
    return success;
}

static int resolve_format(Context *context, const F2cStatement *statement, F2cIoControl *control,
                          const AssignedLabelCatalog *catalog, const AssignedLabelState *state,
                          int reachable, int diagnose) {
    size_t definition;
    int success = 1;
    clear_resolved_formats(control);
    control->assigned_labels_resolved = 1;
    if (diagnose && reachable && state->may_be_integer_or_undefined) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &control->span, 1,
            "%s assigned FORMAT variable '%s' is not defined with a statement label on every "
            "reachable path",
            statement->kind == F2C_STMT_PRINT ? "PRINT" : "I/O",
            control->value != NULL && control->value->text != NULL ? control->value->text
                                                                   : "<unknown>");
    }
    for (definition = 0U; definition < catalog->count; ++definition) {
        const AssignedLabelDefinition *item = &catalog->items[definition];
        const int reaches = reachable && state_has_label(state, definition);
        if (!reaches)
            continue;
        if (!item->format_target) {
            if (diagnose)
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &control->span, 1,
                    "%s assigned FORMAT variable '%s' may hold executable label %s",
                    statement->kind == F2C_STMT_PRINT ? "PRINT" : "I/O",
                    control->value != NULL && control->value->text != NULL ? control->value->text
                                                                           : "<unknown>",
                    item->label);
            continue;
        }
        if (!append_resolved_format(control, item->label))
            success = 0;
    }
    return success;
}

static int expression_references_symbol(const F2cExpr *expression, const F2cExpr *variable) {
    size_t child;
    if (expression == NULL)
        return 0;
    if (same_symbol(expression, variable))
        return 1;
    for (child = 0U; child < expression->child_count; ++child)
        if (expression_references_symbol(expression->children[child], variable))
            return 1;
    return 0;
}

static int designator_indices_reference_symbol(const F2cExpr *designator, const F2cExpr *variable) {
    size_t child;
    if (designator == NULL || same_symbol(designator, variable))
        return 0;
    for (child = 0U; child < designator->child_count; ++child)
        if (expression_references_symbol(designator->children[child], variable))
            return 1;
    return 0;
}

static int io_item_references_symbol(const F2cIoItem *item, const F2cExpr *variable, int input) {
    size_t child;
    if (item == NULL)
        return 0;
    if (item->implied_do) {
        if (expression_references_symbol(item->initial, variable) ||
            expression_references_symbol(item->limit, variable) ||
            expression_references_symbol(item->step, variable))
            return 1;
        for (child = 0U; child < item->child_count; ++child)
            if (io_item_references_symbol(&item->children[child], variable, input))
                return 1;
        return 0;
    }
    return input ? designator_indices_reference_symbol(item->expression, variable)
                 : expression_references_symbol(item->expression, variable);
}

static int allocation_output_keyword(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
           argument->text != NULL &&
           (strcmp(argument->text, "stat") == 0 || strcmp(argument->text, "errmsg") == 0);
}

static int statement_references_label_value(const F2cStatement *statement,
                                            const F2cExpr *variable) {
    size_t index;
    if (statement == NULL)
        return 0;
    if (statement->kind == F2C_STMT_LABEL && statement->nested != NULL)
        return statement_references_label_value(statement->nested, variable);
    if (statement->kind == F2C_STMT_ASSIGN_LABEL)
        return 0;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO && same_symbol(statement->expression, variable))
        return 0;
    if (statement->kind == F2C_STMT_ASSIGNMENT || statement->kind == F2C_STMT_POINTER_ASSIGNMENT) {
        if (expression_references_symbol(statement->right, variable) ||
            designator_indices_reference_symbol(statement->left, variable))
            return 1;
    } else if (statement->kind == F2C_STMT_DO) {
        if (expression_references_symbol(statement->right, variable) ||
            expression_references_symbol(statement->limit, variable) ||
            expression_references_symbol(statement->step, variable) ||
            designator_indices_reference_symbol(statement->left, variable))
            return 1;
    } else if (statement->expression != NULL &&
               expression_references_symbol(statement->expression, variable)) {
        return 1;
    }
    if (statement->kind != F2C_STMT_ASSIGNMENT && statement->kind != F2C_STMT_POINTER_ASSIGNMENT &&
        statement->kind != F2C_STMT_DO) {
        if (expression_references_symbol(statement->right, variable) ||
            expression_references_symbol(statement->limit, variable) ||
            expression_references_symbol(statement->step, variable))
            return 1;
    }
    for (index = 0U; index < statement->item_count; ++index) {
        const F2cExpr *argument = statement->arguments != NULL ? statement->arguments[index] : NULL;
        if (statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE) {
            if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                !allocation_output_keyword(argument)) {
                if (expression_references_symbol(argument_value(argument), variable))
                    return 1;
            } else if (designator_indices_reference_symbol(argument_value(argument), variable)) {
                return 1;
            }
            continue;
        }
        if (statement->kind == F2C_STMT_MOVE_ALLOC || statement->kind == F2C_STMT_NULLIFY) {
            if (designator_indices_reference_symbol(argument_value(argument), variable))
                return 1;
            continue;
        }
        if (expression_references_symbol(argument, variable))
            return 1;
    }
    for (index = 0U; index < statement->control_count; ++index) {
        const F2cIoControl *control = &statement->io_controls[index];
        if (output_control(control->kind))
            continue;
        if (control->kind == F2C_IO_CONTROL_FMT && !control->asterisk &&
            same_symbol(control->value, variable))
            continue;
        if (expression_references_symbol(control->value, variable))
            return 1;
    }
    for (index = 0U; index < statement->io_item_count; ++index)
        if (io_item_references_symbol(&statement->io_items[index], variable,
                                      statement->kind == F2C_STMT_READ))
            return 1;
    for (index = 0U; index < statement->case_range_count; ++index)
        if (expression_references_symbol(statement->case_ranges[index].lower, variable) ||
            expression_references_symbol(statement->case_ranges[index].upper, variable))
            return 1;
    return statement->nested != NULL &&
           statement_references_label_value(statement->nested, variable);
}

static int state_has_any_label(const AssignedLabelState *state, size_t words) {
    size_t word;
    if (state == NULL || !state->initialized)
        return 0;
    for (word = 0U; word < words; ++word)
        if (state->labels[word] != 0U)
            return 1;
    return 0;
}

typedef struct AssignedLabelVariables {
    const F2cExpr **items;
    size_t count;
    size_t capacity;
} AssignedLabelVariables;

static int append_variable(AssignedLabelVariables *variables, const F2cExpr *variable) {
    const F2cExpr **replacement;
    size_t capacity;
    size_t index;
    if (variable == NULL)
        return 1;
    for (index = 0U; index < variables->count; ++index)
        if (same_symbol(variables->items[index], variable))
            return 1;
    if (variables->count == variables->capacity) {
        capacity = variables->capacity == 0U ? 4U : variables->capacity * 2U;
        if (capacity < variables->capacity || capacity > SIZE_MAX / sizeof(*variables->items))
            return 0;
        replacement = (const F2cExpr **)realloc(variables->items, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        variables->items = replacement;
        variables->capacity = capacity;
    }
    variables->items[variables->count++] = variable;
    return 1;
}

static int collect_variables(const F2cStatement *statement, AssignedLabelVariables *variables) {
    size_t control_index;
    if (statement == NULL)
        return 1;
    if ((statement->kind == F2C_STMT_ASSIGN_LABEL || statement->kind == F2C_STMT_ASSIGNED_GOTO) &&
        !append_variable(variables, statement->expression))
        return 0;
    for (control_index = 0U; control_index < statement->control_count; ++control_index) {
        const F2cIoControl *control = &statement->io_controls[control_index];
        if (control->kind == F2C_IO_CONTROL_FMT && !control->asterisk && control->value != NULL &&
            control->value->type == TYPE_INTEGER && control->value->kind == F2C_EXPR_NAME &&
            !append_variable(variables, control->value))
            return 0;
    }
    return statement->nested == NULL || collect_variables(statement->nested, variables);
}

static int resolve_statement_uses(Context *context, F2cStatement *statement,
                                  const F2cExpr *variable, const AssignedLabelCatalog *catalog,
                                  const AssignedLabelState *state, int reachable, int diagnose) {
    size_t control_index;
    if (statement == NULL)
        return 1;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO && same_symbol(statement->expression, variable) &&
        !resolve_goto(context, statement, catalog, state, reachable, diagnose))
        return 0;
    for (control_index = 0U; control_index < statement->control_count; ++control_index) {
        F2cIoControl *control = &statement->io_controls[control_index];
        if (control->kind == F2C_IO_CONTROL_FMT && !control->asterisk &&
            same_symbol(control->value, variable) &&
            !resolve_format(context, statement, control, catalog, state, reachable, diagnose))
            return 0;
    }
    return statement->nested == NULL || resolve_statement_uses(context, statement->nested, variable,
                                                               catalog, state, reachable, diagnose);
}

static int analyze_variables(Context *context, Unit *unit, const F2cControlFlowGraph *graph,
                             const AssignedLabelVariables *variables, int diagnose) {
    size_t variable_index;
    size_t index;
    for (variable_index = 0U; variable_index < variables->count; ++variable_index) {
        const F2cExpr *variable = variables->items[variable_index];
        AssignedLabelCatalog catalog;
        AssignedLabelState *states = NULL;
        size_t words = 0U;
        if (!build_catalog(unit, variable, &catalog) ||
            !solve_states(unit, graph, variable, &catalog, &states, &words)) {
            free(catalog.items);
            free(states);
            return 0;
        }
        for (index = 0U; index < unit->statement_count; ++index) {
            F2cStatement *statement = &unit->statements[index];
            const int reachable = graph->nodes[index].reachable && states[index].initialized;
            if (!resolve_statement_uses(context, statement, variable, &catalog, &states[index],
                                        reachable, diagnose)) {
                free(states != NULL && graph->node_count != 0U ? states[0].labels : NULL);
                free(states);
                free(catalog.items);
                return 0;
            }
            if (diagnose && reachable && state_has_any_label(&states[index], words) &&
                statement_references_label_value(statement, variable)) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                    "integer variable '%s' defined with a statement label may only be referenced "
                    "by assigned GOTO or as an assigned FORMAT",
                    variable->text != NULL ? variable->text : "<unknown>");
            }
        }
        free(states != NULL && graph->node_count != 0U ? states[0].labels : NULL);
        free(states);
        free(catalog.items);
    }
    return 1;
}

static int control_flow_graphs_equal(const F2cControlFlowGraph *left,
                                     const F2cControlFlowGraph *right) {
    size_t node;
    if (left->node_count != right->node_count)
        return 0;
    for (node = 0U; node < left->node_count; ++node) {
        const F2cControlFlowNode *left_node = &left->nodes[node];
        const F2cControlFlowNode *right_node = &right->nodes[node];
        size_t edge;
        if (left_node->reachable != right_node->reachable ||
            left_node->successor_count != right_node->successor_count)
            return 0;
        for (edge = 0U; edge < left_node->successor_count; ++edge)
            if (left_node->successors[edge].target != right_node->successors[edge].target ||
                left_node->successors[edge].kind != right_node->successors[edge].kind)
                return 0;
    }
    return 1;
}

static size_t refinement_limit(const F2cControlFlowGraph *graph) {
    size_t limit = 2U;
    size_t node;
    for (node = 0U; node < graph->node_count; ++node) {
        if (graph->nodes[node].successor_count > SIZE_MAX - limit)
            return SIZE_MAX;
        limit += graph->nodes[node].successor_count;
    }
    return limit;
}

void f2c_validation_assigned_labels(Context *context, Unit *unit, F2cControlFlowGraph *graph) {
    AssignedLabelVariables variables = {0};
    const size_t limit = graph != NULL ? refinement_limit(graph) : 0U;
    size_t iteration = 0U;
    size_t index;
    if (context == NULL || unit == NULL || graph == NULL)
        return;
    for (index = 0U; index < unit->statement_count; ++index)
        if (!collect_variables(&unit->statements[index], &variables))
            goto out_of_memory;
    for (;;) {
        F2cControlFlowGraph refined;
        if (!analyze_variables(context, unit, graph, &variables, 0))
            goto out_of_memory;
        if (!f2c_control_flow_build(context, unit, &refined)) {
            free(variables.items);
            return;
        }
        if (control_flow_graphs_equal(graph, &refined)) {
            f2c_control_flow_free(&refined);
            break;
        }
        f2c_control_flow_free(graph);
        *graph = refined;
        if (++iteration > limit) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_INTERNAL, &unit->header_span, 1,
                "assigned statement-label control-flow refinement did not converge");
            free(variables.items);
            return;
        }
    }
    if (!analyze_variables(context, unit, graph, &variables, 1))
        goto out_of_memory;
    free(variables.items);
    return;

out_of_memory:
    free(variables.items);
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &unit->header_span, 1,
                             "out of memory while analyzing assigned statement-label values");
}
