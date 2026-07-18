#include "internal/f2c.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct DataCursor {
    const F2cDataGroup *group;
    size_t value_index;
    uint64_t repetitions_left;
    const F2cExpr *current;
} DataCursor;

typedef struct DataSubstitutions {
    F2cIntegerSubstitution *items;
    size_t count;
    size_t capacity;
} DataSubstitutions;

static int push_substitution(DataSubstitutions *substitutions, const F2cExpr *iterator) {
    F2cIntegerSubstitution *replacement;
    size_t capacity;
    if (substitutions->count == substitutions->capacity) {
        capacity = substitutions->capacity == 0U ? 8U : substitutions->capacity * 2U;
        if (capacity < substitutions->capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement = (F2cIntegerSubstitution *)realloc(substitutions->items,
                                                        capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        substitutions->items = replacement;
        substitutions->capacity = capacity;
    }
    substitutions->items[substitutions->count].symbol = iterator != NULL ? iterator->symbol : NULL;
    substitutions->items[substitutions->count].name = iterator != NULL ? iterator->text : NULL;
    substitutions->items[substitutions->count].value = 0;
    ++substitutions->count;
    return 1;
}

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static char *emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static const F2cExpr *next_data_value(DataCursor *cursor) {
    if (cursor->repetitions_left == 0U) {
        const F2cDataValue *value;
        if (cursor->value_index >= cursor->group->value_count)
            return NULL;
        value = &cursor->group->values[cursor->value_index++];
        if (value->repeat_count == 0U)
            return NULL;
        cursor->current = value->expression;
        cursor->repetitions_left = value->repeat_count;
    }
    --cursor->repetitions_left;
    return cursor->current;
}

static int cursor_has_values(const DataCursor *cursor) {
    return cursor->repetitions_left != 0 || cursor->value_index < cursor->group->value_count;
}

static int data_item_requires_runtime_initialization(const F2cIoItem *item) {
    size_t child;
    if (item == NULL)
        return 1;
    if (!item->implied_do)
        return item->expression == NULL || item->expression->rank != 0U ||
               !item->data_static_initializer;
    for (child = 0U; child < item->child_count; ++child)
        if (data_item_requires_runtime_initialization(&item->children[child]))
            return 1;
    return 0;
}

static int data_group_requires_runtime_initialization(const F2cDataGroup *group) {
    size_t target;
    if (group == NULL)
        return 1;
    for (target = 0U; target < group->target_count; ++target)
        if (data_item_requires_runtime_initialization(&group->targets[target]))
            return 1;
    return 0;
}

static int emit_data_assignment(Context *context, Unit *unit, const F2cExpr *target,
                                const F2cExpr *value, int depth) {
    Symbol *symbol = target != NULL ? target->symbol : NULL;
    char *left = emit_expression(unit, target);
    char *right = emit_expression(unit, value);
    if (left == NULL || right == NULL) {
        free(left);
        free(right);
        return 0;
    }
    if (symbol != NULL && symbol->type == TYPE_CHARACTER &&
        f2c_emit_character_assignment(context, unit, symbol, target, value, left, right, depth)) {
        free(left);
        free(right);
        return 1;
    }
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s = %s;\n", left, right);
    free(left);
    free(right);
    return 1;
}

static int evaluate_substituted_integer(Unit *unit, const F2cExpr *expression,
                                        const F2cIntegerSubstitution *substitutions,
                                        size_t substitution_count, int64_t *value) {
    F2cExpr *substituted =
        f2c_expr_clone_substitute_integers(expression, substitutions, substitution_count);
    const int valid =
        substituted != NULL && f2c_evaluate_integer_constant(unit, substituted, value);
    f2c_expr_free(substituted);
    return valid;
}

static int expression_element_count(const F2cExpr *expression, size_t *count) {
    size_t result = 1U;
    size_t dimension;
    if (expression == NULL || count == NULL || expression->rank == 0U ||
        expression->shape.rank != expression->rank)
        return 0;
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        const F2cShapeDimension *shape = &expression->shape.dimensions[dimension];
        if (!shape->extent_known || shape->extent > (uint64_t)SIZE_MAX ||
            (shape->extent != 0U && result > SIZE_MAX / (size_t)shape->extent))
            return 0;
        result *= (size_t)shape->extent;
    }
    *count = result;
    return 1;
}

static int emit_array_values(Context *context, Unit *unit, const F2cExpr *target,
                             DataCursor *cursor, int depth) {
    Symbol *symbol = target != NULL ? target->symbol : NULL;
    size_t element_count;
    size_t index;
    char *character_length = NULL;
    if (symbol == NULL || target->kind != F2C_EXPR_NAME ||
        !expression_element_count(target, &element_count))
        return 0;
    if (symbol->type == TYPE_CHARACTER) {
        character_length =
            symbol->character_length_expression != NULL
                ? f2c_emit_typed_expression(unit, symbol->character_length_expression)
                : f2c_strdup("1U");
        if (character_length == NULL)
            return 0;
    }
    for (index = 0U; index < element_count; ++index) {
        const F2cExpr *value = next_data_value(cursor);
        char *right = value != NULL ? emit_expression(unit, value) : NULL;
        if (right == NULL) {
            free(character_length);
            return 0;
        }
        if (symbol->type == TYPE_CHARACTER) {
            Buffer pointer = {0};
            char *target_pointer;
            f2c_buffer_printf(&pointer, "&%s[(size_t)(%s) * %zuU]", f2c_symbol_c_name(unit, symbol),
                              character_length, index);
            target_pointer = f2c_buffer_take(&pointer);
            if (!f2c_emit_character_storage_assignment(context, unit, target_pointer,
                                                       character_length, value, right, depth)) {
                free(target_pointer);
                free(right);
                free(character_length);
                return 0;
            }
            free(target_pointer);
        } else {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s[%zu] = %s;\n", f2c_symbol_c_name(unit, symbol),
                              index, right);
        }
        free(right);
    }
    free(character_length);
    return 1;
}

static int emit_data_target(Context *context, Unit *unit, const F2cIoItem *target,
                            DataCursor *cursor, DataSubstitutions *substitutions, int depth) {
    int64_t first;
    int64_t last;
    int64_t step;
    int64_t current;
    uint64_t iterations;
    uint64_t iteration;
    size_t child;
    if (!target->implied_do) {
        F2cExpr *substituted;
        substituted = f2c_expr_clone_substitute_integers(target->expression, substitutions->items,
                                                         substitutions->count);
        if (substituted == NULL) {
            return 0;
        }
        if (substituted->rank != 0U) {
            const int emitted = emit_array_values(context, unit, substituted, cursor, depth);
            f2c_expr_free(substituted);
            return emitted;
        }
        {
            const F2cExpr *value = next_data_value(cursor);
            if (value == NULL ||
                (!target->data_static_initializer &&
                 !emit_data_assignment(context, unit, substituted, value, depth))) {
                f2c_expr_free(substituted);
                return 0;
            }
        }
        f2c_expr_free(substituted);
        return 1;
    }
    if (!evaluate_substituted_integer(unit, target->initial, substitutions->items,
                                      substitutions->count, &first) ||
        !evaluate_substituted_integer(unit, target->limit, substitutions->items,
                                      substitutions->count, &last) ||
        !evaluate_substituted_integer(unit, target->step, substitutions->items,
                                      substitutions->count, &step) ||
        step == 0 || !f2c_integer_iteration_count(first, last, step, &iterations))
        return 0;
    if (!push_substitution(substitutions, target->iterator))
        return 0;
    current = first;
    for (iteration = 0U; iteration < iterations; ++iteration) {
        substitutions->items[substitutions->count - 1U].value = current;
        for (child = 0U; child < target->child_count; ++child) {
            const F2cIoItem *item = &target->children[child];
            if (!emit_data_target(context, unit, item, cursor, substitutions, depth)) {
                --substitutions->count;
                return 0;
            }
        }
        if (iteration + 1U < iterations)
            current += step;
    }
    --substitutions->count;
    return 1;
}

static int emit_data_group_contents(Context *context, Unit *unit, const F2cDataGroup *group,
                                    size_t line_number, int depth) {
    DataCursor cursor = {group, 0U, 0, NULL};
    DataSubstitutions substitutions = {0};
    size_t target_index;
    if (!group->counts_valid) {
        f2c_diagnostic(context, line_number, 1, "DATA typed IR was not validated");
        return 0;
    }
    for (target_index = 0U; target_index < group->target_count; ++target_index) {
        const F2cIoItem *target = &group->targets[target_index];
        if (!emit_data_target(context, unit, target, &cursor, &substitutions, depth)) {
            f2c_diagnostic(context, line_number, 1, "validated DATA IR could not be emitted");
            free(substitutions.items);
            return 0;
        }
    }
    if (cursor_has_values(&cursor)) {
        f2c_diagnostic(context, line_number, 1, "validated DATA IR retained unconsumed values");
        free(substitutions.items);
        return 0;
    }
    free(substitutions.items);
    return 1;
}

static void emit_data_statement_initializers(Context *context, Unit *unit,
                                             const F2cStatement *statement, size_t *identifier,
                                             int depth) {
    size_t group_index;
    if (statement != NULL && statement->kind == F2C_STMT_LABEL) {
        emit_data_statement_initializers(context, unit, statement->nested, identifier, depth);
        return;
    }
    if (statement == NULL || statement->kind != F2C_STMT_DATA)
        return;
    if (statement->data_group_count == 0U) {
        f2c_diagnostic(context, statement->line, 1, "malformed DATA statement: %s",
                       statement->text);
        return;
    }
    for (group_index = 0U; group_index < statement->data_group_count; ++group_index) {
        const F2cDataGroup *group = &statement->data_groups[group_index];
        const size_t current = (*identifier)++;
        if (data_group_requires_runtime_initialization(group)) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "static bool f2c_data_initialized_%zu = false;\n",
                              current);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "if (!f2c_data_initialized_%zu) {\n", current);
            if (emit_data_group_contents(context, unit, group, statement->line, depth + 1)) {
                indent(&context->output, depth + 1);
                f2c_buffer_printf(&context->output, "f2c_data_initialized_%zu = true;\n", current);
            }
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        } else {
            (void)emit_data_group_contents(context, unit, group, statement->line, depth);
        }
    }
}

void f2c_emit_unit_data_initializers(Context *context, Unit *unit, int depth) {
    size_t statement_index;
    size_t identifier = 0U;
    if (context == NULL || unit == NULL)
        return;
    for (statement_index = 0U; statement_index < unit->statement_count; ++statement_index)
        emit_data_statement_initializers(context, unit, &unit->statements[statement_index],
                                         &identifier, depth);
}
