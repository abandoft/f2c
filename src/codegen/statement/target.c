#include "codegen/statement/private.h"

#include <stdint.h>
#include <string.h>

static int contains_statement(const F2cStatement *root, const F2cStatement *statement) {
    return root == statement ||
           (root != NULL && root->nested != NULL && contains_statement(root->nested, statement));
}

size_t f2c_statement_unit_index(const Unit *unit, const F2cStatement *statement) {
    size_t index;
    if (unit == NULL || statement == NULL)
        return SIZE_MAX;
    for (index = 0U; index < unit->statement_count; ++index)
        if (contains_statement(&unit->statements[index], statement))
            return index;
    return SIZE_MAX;
}

static int statement_targets_label(const F2cStatement *statement, const char *label) {
    size_t index;
    if (statement->kind == F2C_STMT_GOTO || statement->kind == F2C_STMT_ASSIGNED_GOTO ||
        statement->kind == F2C_STMT_ASSIGN_LABEL || statement->kind == F2C_STMT_ARITHMETIC_IF ||
        (statement->kind == F2C_STMT_CALL && statement->label_count != 0U)) {
        if (statement->name != NULL && f2c_statement_labels_equal(statement->name, label))
            return 1;
        for (index = 0U; index < statement->label_count; ++index)
            if (f2c_statement_labels_equal(statement->labels[index], label))
                return 1;
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_BACKSPACE || statement->kind == F2C_STMT_ENDFILE ||
        statement->kind == F2C_STMT_INQUIRE || statement->kind == F2C_STMT_CLOSE) {
        for (index = 0U; index < statement->control_count; ++index) {
            const F2cIoControl *control = &statement->io_controls[index];
            const F2cExpr *value = control->value;
            if ((control->kind != F2C_IO_CONTROL_END && control->kind != F2C_IO_CONTROL_EOR &&
                 control->kind != F2C_IO_CONTROL_ERR) ||
                value == NULL)
                continue;
            if (value->text != NULL && f2c_statement_labels_equal(value->text, label))
                return 1;
        }
    }
    return statement->nested != NULL && statement_targets_label(statement->nested, label);
}

int f2c_statement_unit_has_label_target(const Unit *unit, const char *label) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index)
        if (statement_targets_label(&unit->statements[index], label))
            return 1;
    return 0;
}

static int statement_targets_construct(const F2cStatement *statement, const F2cStatement *target,
                                       F2cStatementKind transfer_kind) {
    if (statement->kind == transfer_kind && statement->control_name != NULL &&
        statement->control_target == target)
        return 1;
    return statement->nested != NULL &&
           statement_targets_construct(statement->nested, target, transfer_kind);
}

int f2c_statement_unit_targets_construct(const Unit *unit, const F2cStatement *target,
                                         F2cStatementKind transfer_kind) {
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index)
        if (statement_targets_construct(&unit->statements[index], target, transfer_kind))
            return 1;
    return 0;
}
