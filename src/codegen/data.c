#include "internal/f2c.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DataCursor {
    const F2cDataGroup *group;
    size_t value_index;
    int32_t repetitions_left;
    const F2cExpr *current;
} DataCursor;

typedef struct DataSubstitution {
    const F2cExpr *iterator;
    int32_t value;
} DataSubstitution;

typedef struct DataSubstitutions {
    DataSubstitution *items;
    size_t count;
    size_t capacity;
} DataSubstitutions;

static int push_substitution(DataSubstitutions *substitutions, const F2cExpr *iterator) {
    DataSubstitution *replacement;
    size_t capacity;
    if (substitutions->count == substitutions->capacity) {
        capacity = substitutions->capacity == 0U ? 8U : substitutions->capacity * 2U;
        if (capacity < substitutions->capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement =
            (DataSubstitution *)realloc(substitutions->items, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        substitutions->items = replacement;
        substitutions->capacity = capacity;
    }
    substitutions->items[substitutions->count].iterator = iterator;
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

static int evaluate_integer(Unit *unit, const F2cExpr *expression, int32_t *value) {
    int64_t evaluated;
    if (!f2c_evaluate_integer_constant(unit, expression, &evaluated) || evaluated < INT32_MIN ||
        evaluated > INT32_MAX)
        return 0;
    *value = (int32_t)evaluated;
    return 1;
}

static F2cExpr *clone_with_integers(const F2cExpr *expression,
                                    const DataSubstitution *substitutions,
                                    size_t substitution_count) {
    F2cExpr *clone;
    size_t i;
    if (expression == NULL)
        return NULL;
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    clone->kind = expression->kind;
    clone->type = expression->type;
    clone->rank = expression->rank;
    clone->definable = expression->definable;
    clone->symbol = expression->symbol;
    clone->temporary_index = expression->temporary_index;
    clone->lowered_c = expression->lowered_c != NULL ? f2c_strdup(expression->lowered_c) : NULL;
    clone->lowered_extent_c =
        expression->lowered_extent_c != NULL ? f2c_strdup(expression->lowered_extent_c) : NULL;
    clone->lowered_character_length_c = expression->lowered_character_length_c != NULL
                                            ? f2c_strdup(expression->lowered_character_length_c)
                                            : NULL;
    if ((expression->lowered_c != NULL && clone->lowered_c == NULL) ||
        (expression->lowered_extent_c != NULL && clone->lowered_extent_c == NULL) ||
        (expression->lowered_character_length_c != NULL &&
         clone->lowered_character_length_c == NULL)) {
        f2c_expr_free(clone);
        return NULL;
    }
    if (expression->kind == F2C_EXPR_NAME) {
        for (i = substitution_count; i != 0U; --i) {
            const F2cExpr *iterator = substitutions[i - 1U].iterator;
            if (iterator != NULL &&
                ((expression->symbol != NULL && expression->symbol == iterator->symbol) ||
                 (expression->text != NULL && iterator->text != NULL &&
                  strcmp(expression->text, iterator->text) == 0))) {
                char literal[32];
                (void)snprintf(literal, sizeof(literal), "%d", (int)substitutions[i - 1U].value);
                clone->kind = F2C_EXPR_INTEGER_LITERAL;
                clone->type = TYPE_INTEGER;
                clone->rank = 0U;
                clone->definable = 0;
                clone->symbol = NULL;
                clone->text = f2c_strdup(literal);
                return clone;
            }
        }
    }
    clone->text = expression->text != NULL ? f2c_strdup(expression->text) : NULL;
    if (expression->child_count != 0U) {
        clone->children = (F2cExpr **)calloc(expression->child_count, sizeof(*clone->children));
        if (clone->children == NULL) {
            f2c_expr_free(clone);
            return NULL;
        }
        for (i = 0U; i < expression->child_count; ++i) {
            clone->children[i] =
                clone_with_integers(expression->children[i], substitutions, substitution_count);
            if (clone->children[i] == NULL) {
                clone->child_count = i;
                f2c_expr_free(clone);
                return NULL;
            }
            ++clone->child_count;
        }
    }
    return clone;
}

static const F2cExpr *next_data_value(Unit *unit, DataCursor *cursor) {
    if (cursor->repetitions_left == 0) {
        const F2cDataValue *value;
        int32_t repeat = 1;
        if (cursor->value_index >= cursor->group->value_count)
            return NULL;
        value = &cursor->group->values[cursor->value_index++];
        if (value->repeat != NULL &&
            (!evaluate_integer(unit, value->repeat, &repeat) || repeat <= 0))
            return NULL;
        cursor->current = value->expression;
        cursor->repetitions_left = repeat;
    }
    --cursor->repetitions_left;
    return cursor->current;
}

static int cursor_has_values(const DataCursor *cursor) {
    return cursor->repetitions_left != 0 || cursor->value_index < cursor->group->value_count;
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
                                        const DataSubstitution *substitutions,
                                        size_t substitution_count, int32_t *value) {
    F2cExpr *substituted = clone_with_integers(expression, substitutions, substitution_count);
    const int valid = substituted != NULL && evaluate_integer(unit, substituted, value);
    f2c_expr_free(substituted);
    return valid;
}

static int emit_data_target(Context *context, Unit *unit, const F2cIoItem *target,
                            DataCursor *cursor, DataSubstitutions *substitutions, int depth) {
    int32_t first;
    int32_t last;
    int32_t step;
    int64_t current;
    size_t child;
    if (!target->implied_do) {
        const F2cExpr *value = next_data_value(unit, cursor);
        F2cExpr *substituted;
        if (value == NULL)
            return 0;
        substituted =
            clone_with_integers(target->expression, substitutions->items, substitutions->count);
        if (substituted == NULL ||
            !emit_data_assignment(context, unit, substituted, value, depth)) {
            f2c_expr_free(substituted);
            return 0;
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
        step == 0)
        return 0;
    if (!push_substitution(substitutions, target->iterator))
        return 0;
    for (current = first; (step > 0 && current <= last) || (step < 0 && current >= last);
         current += step) {
        substitutions->items[substitutions->count - 1U].value = (int32_t)current;
        for (child = 0U; child < target->child_count; ++child) {
            const F2cIoItem *item = &target->children[child];
            if (!emit_data_target(context, unit, item, cursor, substitutions, depth)) {
                --substitutions->count;
                return 0;
            }
        }
    }
    --substitutions->count;
    return 1;
}

static int symbol_element_count(Unit *unit, const Symbol *symbol, size_t *count) {
    size_t result = 1U;
    size_t dimension;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        const F2cExpr *lower = symbol->dimensions[dimension].lower_expression;
        const F2cExpr *upper = symbol->dimensions[dimension].upper_expression;
        int32_t lower_value = 0;
        int32_t upper_value = 0;
        size_t extent;
        const int valid = evaluate_integer(unit, lower, &lower_value) &&
                          evaluate_integer(unit, upper, &upper_value) && upper_value >= lower_value;
        if (!valid)
            return 0;
        extent = (size_t)((int64_t)upper_value - (int64_t)lower_value + 1);
        if (extent != 0U && result > SIZE_MAX / extent)
            return 0;
        result *= extent;
    }
    *count = result;
    return 1;
}

static int emit_array_values(Context *context, Unit *unit, Symbol *symbol, DataCursor *cursor,
                             int depth) {
    size_t index = 0U;
    size_t element_count;
    char *character_length = NULL;
    const F2cExpr *value;
    if (!symbol_element_count(unit, symbol, &element_count))
        return 0;
    if (symbol->type == TYPE_CHARACTER)
        character_length =
            symbol->character_length != NULL
                ? f2c_emit_typed_expression(unit, symbol->character_length_expression)
                : f2c_strdup("1U");
    while ((value = next_data_value(unit, cursor)) != NULL) {
        char *right = emit_expression(unit, value);
        if (right == NULL || index >= element_count) {
            free(right);
            free(character_length);
            return 0;
        }
        if (symbol->type == TYPE_CHARACTER) {
            Buffer target = {0};
            char *target_pointer;
            f2c_buffer_printf(&target, "&%s[(size_t)(%s) * %zuU]", f2c_symbol_c_name(unit, symbol),
                              character_length, index);
            target_pointer = f2c_buffer_take(&target);
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
        ++index;
        free(right);
    }
    free(character_length);
    return index == element_count;
}

static void emit_data_group(Context *context, Unit *unit, const F2cDataGroup *group,
                            size_t line_number, int depth) {
    DataCursor cursor = {group, 0U, 0, NULL};
    DataSubstitutions substitutions = {0};
    size_t target_index;
    if (group->target_count == 1U && !group->targets[0].implied_do &&
        group->targets[0].expression != NULL &&
        group->targets[0].expression->kind == F2C_EXPR_NAME &&
        group->targets[0].expression->symbol != NULL &&
        group->targets[0].expression->symbol->rank != 0U) {
        if (!emit_array_values(context, unit, group->targets[0].expression->symbol, &cursor, depth))
            f2c_diagnostic(context, line_number, 1, "invalid DATA array initializer");
        return;
    }
    for (target_index = 0U; target_index < group->target_count; ++target_index) {
        const F2cIoItem *target = &group->targets[target_index];
        if (target->implied_do) {
            if (!emit_data_target(context, unit, target, &cursor, &substitutions, depth)) {
                f2c_diagnostic(context, line_number, 1,
                               "DATA implied-DO bounds or value count are invalid");
                free(substitutions.items);
                return;
            }
        } else {
            const F2cExpr *value = next_data_value(unit, &cursor);
            if (value == NULL ||
                !emit_data_assignment(context, unit, target->expression, value, depth)) {
                f2c_diagnostic(context, line_number, 1,
                               "DATA value count does not match its target list");
                free(substitutions.items);
                return;
            }
        }
    }
    if (cursor_has_values(&cursor))
        f2c_diagnostic(context, line_number, 1, "DATA value count does not match its target list");
    free(substitutions.items);
}

void f2c_emit_data_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    size_t i;
    if (statement->data_group_count == 0U) {
        f2c_diagnostic(context, statement->line, 1, "malformed DATA statement: %s",
                       statement->text);
        return;
    }
    for (i = 0U; i < statement->data_group_count; ++i)
        emit_data_group(context, unit, &statement->data_groups[i], statement->line, depth);
}
