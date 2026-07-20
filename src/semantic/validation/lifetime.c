#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

static const F2cStatement *statement_body(const F2cStatement *statement) {
    return statement != NULL && statement->kind == F2C_STMT_LABEL && statement->nested != NULL
               ? statement->nested
               : statement;
}

static int block_scoped_symbol(const Unit *unit, const Symbol *symbol) {
    return symbol->scope_begin_line != 0U && !unit->save_all && !symbol->saved &&
           symbol->initializer == NULL && !symbol->argument && !symbol->module_entity;
}

static int build_cleanup_plan(Context *context, Unit *unit, size_t source_line, size_t target_line,
                              F2cScopeCleanupPlan *plan, const F2cSourceSpan *span) {
    size_t count = 0U;
    size_t index;
    free(plan->symbols);
    memset(plan, 0, sizeof(*plan));
    for (index = unit->symbol_count; index != 0U; --index) {
        Symbol *symbol = &unit->symbols[index - 1U];
        const int source_inside =
            source_line > symbol->scope_begin_line && source_line < symbol->scope_end_line;
        const int target_inside =
            target_line > symbol->scope_begin_line && target_line < symbol->scope_end_line;
        if (block_scoped_symbol(unit, symbol) && source_inside && !target_inside)
            ++count;
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

static const F2cStatement *label_target(const Unit *unit, const char *label) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *root = &unit->statements[index];
        if (root->kind == F2C_STMT_LABEL && root->name != NULL &&
            f2c_statement_labels_equal(root->name, label))
            return root;
    }
    return NULL;
}

static int build_label_plan(Context *context, Unit *unit, const F2cStatement *source,
                            const char *label, const F2cSourceSpan *span,
                            F2cScopeCleanupPlan *plan) {
    const F2cStatement *target = label_target(unit, label);
    return target == NULL ||
           build_cleanup_plan(context, unit, source->line, target->line, plan, span);
}

static int prepare_label_plans(Context *context, Unit *unit, F2cStatement *statement) {
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
        if (!build_label_plan(context, unit, statement, statement->labels[label], span,
                              &statement->label_cleanups[label]))
            return 0;
    }
    return 1;
}

static int append_resolved_branch(Context *context, Unit *unit, F2cStatement *statement,
                                  const char *label, const F2cSourceSpan *span) {
    F2cResolvedBranch *replacement;
    F2cResolvedBranch *branch;
    size_t index;
    for (index = 0U; index < statement->resolved_branch_count; ++index)
        if (f2c_statement_labels_equal(statement->resolved_branches[index].label, label))
            return 1;
    if (statement->resolved_branch_count == SIZE_MAX / sizeof(*replacement))
        goto failed;
    replacement = (F2cResolvedBranch *)realloc(statement->resolved_branches,
                                               (statement->resolved_branch_count + 1U) *
                                                   sizeof(*replacement));
    if (replacement == NULL)
        goto failed;
    statement->resolved_branches = replacement;
    branch = &statement->resolved_branches[statement->resolved_branch_count];
    memset(branch, 0, sizeof(*branch));
    branch->label = f2c_strdup(label);
    if (branch->label == NULL ||
        !build_label_plan(context, unit, statement, label, span, &branch->cleanup)) {
        free(branch->label);
        free(branch->cleanup.symbols);
        memset(branch, 0, sizeof(*branch));
        return 0;
    }
    ++statement->resolved_branch_count;
    return 1;

failed:
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &statement->span, 1,
                             "out of memory while resolving assigned GOTO targets");
    return 0;
}

static int prepare_assigned_targets(Context *context, Unit *unit, F2cStatement *statement) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *assignment = statement_body(&unit->statements[index]);
        if (assignment == NULL || assignment->kind != F2C_STMT_ASSIGN_LABEL ||
            assignment->name == NULL || statement->name == NULL ||
            strcmp(assignment->name, statement->name) != 0 || assignment->label_count != 1U)
            continue;
        if (!append_resolved_branch(context, unit, statement, assignment->labels[0],
                                    assignment->label_spans != NULL ? &assignment->label_spans[0]
                                                                    : &assignment->span))
            return 0;
    }
    return 1;
}

static int prepare_io_plans(Context *context, Unit *unit, F2cStatement *statement) {
    size_t control;
    for (control = 0U; control < statement->control_count; ++control) {
        F2cIoControl *item = &statement->io_controls[control];
        if ((item->kind != F2C_IO_CONTROL_END && item->kind != F2C_IO_CONTROL_EOR &&
             item->kind != F2C_IO_CONTROL_ERR) ||
            item->value == NULL || item->value->text == NULL)
            continue;
        if (!build_label_plan(context, unit, statement, item->value->text, &item->span,
                              &item->cleanup))
            return 0;
    }
    return 1;
}

static int prepare_statement_plans(Context *context, Unit *unit, F2cStatement *statement) {
    if (statement == NULL)
        return 1;
    if ((statement->kind == F2C_STMT_ARITHMETIC_IF || statement->kind == F2C_STMT_GOTO ||
         (statement->kind == F2C_STMT_ASSIGNED_GOTO && statement->label_count != 0U) ||
         (statement->kind == F2C_STMT_CALL && statement->label_count != 0U)) &&
        !prepare_label_plans(context, unit, statement))
        return 0;
    if (statement->kind == F2C_STMT_GOTO && statement->name != NULL &&
        !build_label_plan(context, unit, statement, statement->name, &statement->label_span,
                          &statement->transfer_cleanup))
        return 0;
    if ((statement->kind == F2C_STMT_CYCLE || statement->kind == F2C_STMT_EXIT) &&
        statement->control_target != NULL &&
        !build_cleanup_plan(context, unit, statement->line, statement->control_target->line,
                            &statement->transfer_cleanup, &statement->span))
        return 0;
    if (statement->kind == F2C_STMT_ASSIGNED_GOTO && statement->label_count == 0U &&
        !prepare_assigned_targets(context, unit, statement))
        return 0;
    if (!prepare_io_plans(context, unit, statement))
        return 0;
    return statement->nested == NULL || prepare_statement_plans(context, unit, statement->nested);
}

void f2c_validation_lifetimes(Context *context, Unit *unit) {
    size_t statement;
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!prepare_statement_plans(context, unit, &unit->statements[statement]))
            return;
}
