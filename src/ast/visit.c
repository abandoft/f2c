#include "internal/f2c.h"

void f2c_visit_expression(F2cExpr *expression, F2cExpressionVisitor visitor, void *state) {
    size_t i;
    if (expression == NULL || visitor == NULL)
        return;
    for (i = 0U; i < expression->child_count; ++i)
        f2c_visit_expression(expression->children[i], visitor, state);
    visitor(expression, state);
}

static void visit_io_item(F2cIoItem *item, F2cExpressionVisitor visitor, void *state) {
    size_t i;
    if (item == NULL)
        return;
    f2c_visit_expression(item->expression, visitor, state);
    f2c_visit_expression(item->iterator, visitor, state);
    f2c_visit_expression(item->initial, visitor, state);
    f2c_visit_expression(item->limit, visitor, state);
    f2c_visit_expression(item->step, visitor, state);
    for (i = 0U; i < item->child_count; ++i)
        visit_io_item(&item->children[i], visitor, state);
}

void f2c_visit_statement_expressions(F2cStatement *statement, F2cExpressionVisitor visitor,
                                     void *state) {
    size_t i;
    size_t j;
    if (statement == NULL || visitor == NULL)
        return;
    f2c_visit_expression(statement->expression, visitor, state);
    f2c_visit_expression(statement->left, visitor, state);
    f2c_visit_expression(statement->right, visitor, state);
    f2c_visit_expression(statement->limit, visitor, state);
    f2c_visit_expression(statement->step, visitor, state);
    f2c_visit_expression(statement->allocation_character_length, visitor, state);
    for (i = 0U; i < statement->case_range_count; ++i) {
        f2c_visit_expression(statement->case_ranges[i].lower, visitor, state);
        f2c_visit_expression(statement->case_ranges[i].upper, visitor, state);
    }
    for (i = 0U; i < statement->item_count; ++i)
        f2c_visit_expression(statement->arguments != NULL ? statement->arguments[i] : NULL, visitor,
                             state);
    for (i = 0U; i < statement->control_count; ++i)
        f2c_visit_expression(statement->io_controls[i].value, visitor, state);
    for (i = 0U; i < statement->io_item_count; ++i)
        visit_io_item(&statement->io_items[i], visitor, state);
    for (i = 0U; i < statement->data_group_count; ++i) {
        F2cDataGroup *group = &statement->data_groups[i];
        for (j = 0U; j < group->target_count; ++j)
            visit_io_item(&group->targets[j], visitor, state);
        for (j = 0U; j < group->value_count; ++j) {
            f2c_visit_expression(group->values[j].expression, visitor, state);
            f2c_visit_expression(group->values[j].repeat, visitor, state);
        }
    }
    f2c_visit_statement_expressions(statement->nested, visitor, state);
}
