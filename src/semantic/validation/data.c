#include "semantic/validation/private.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct DataBindings {
    F2cIntegerSubstitution *items;
    size_t count;
    size_t capacity;
} DataBindings;

typedef struct DataValueCursor {
    const F2cDataGroup *group;
    size_t index;
    uint64_t remaining;
    const F2cExpr *current;
} DataValueCursor;

typedef struct DataValidation {
    Context *context;
    Unit *unit;
    F2cStatement *statement;
    F2cDataGroup *group;
    DataBindings bindings;
    DataValueCursor values;
    size_t target_count;
} DataValidation;

static int add_size(size_t *total, size_t value) {
    if (value > SIZE_MAX - *total)
        return 0;
    *total += value;
    return 1;
}

static int expression_extent(const F2cExpr *expression, size_t *extent) {
    const F2cShape *shape;
    size_t total = 1U;
    size_t dimension;
    if (expression == NULL || extent == NULL)
        return 0;
    if (expression->rank == 0U) {
        *extent = 1U;
        return 1;
    }
    shape = &expression->shape;
    if (shape->rank != expression->rank || expression->rank > F2C_MAX_RANK)
        return 0;
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        const uint64_t value = shape->dimensions[dimension].extent;
        if (!shape->dimensions[dimension].extent_known || value > (uint64_t)SIZE_MAX ||
            (value != 0U && total > SIZE_MAX / (size_t)value))
            return 0;
        total *= (size_t)value;
    }
    *extent = total;
    return 1;
}

static int push_binding(DataBindings *bindings, const F2cExpr *iterator) {
    F2cIntegerSubstitution *replacement;
    size_t next;
    if (bindings->count == bindings->capacity) {
        next = bindings->capacity == 0U ? 4U : bindings->capacity * 2U;
        if (next < bindings->capacity || next > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement =
            (F2cIntegerSubstitution *)realloc(bindings->items, next * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        bindings->items = replacement;
        bindings->capacity = next;
    }
    bindings->items[bindings->count].symbol = iterator != NULL ? iterator->symbol : NULL;
    bindings->items[bindings->count].name = iterator != NULL ? iterator->text : NULL;
    bindings->items[bindings->count].value = 0;
    ++bindings->count;
    return 1;
}

static int evaluate_bound(DataValidation *validation, const F2cExpr *expression, int64_t *value) {
    F2cExpr *substituted = f2c_expr_clone_substitute_integers(
        expression, validation->bindings.items, validation->bindings.count);
    const int valid =
        substituted != NULL && f2c_evaluate_integer_constant(validation->unit, substituted, value);
    f2c_expr_free(substituted);
    return valid;
}

static int expansion_within_budget(DataValidation *validation, uint64_t iterations) {
    const size_t limit = validation->context->limits.max_constant_steps;
    if (iterations <= (uint64_t)SIZE_MAX && (limit == 0U || iterations <= (uint64_t)limit))
        return 1;
    f2c_diagnostic_span_code(
        validation->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &validation->group->span, 1,
        "DATA implied-DO expansion exceeds the constant-step limit of %zu", limit);
    return 0;
}

static int implied_do_iterations(DataValidation *validation, const F2cIoItem *item, int64_t *first,
                                 int64_t *step, uint64_t *iterations) {
    int64_t last;
    if (item->iterator == NULL || item->iterator->kind != F2C_EXPR_NAME ||
        item->iterator->type != TYPE_INTEGER || item->iterator->rank != 0U) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                                 item->iterator != NULL ? &item->iterator->span
                                                        : &validation->group->span,
                                 1, "DATA implied-DO iterator must be a scalar INTEGER name");
        return 0;
    }
    if (item->initial == NULL || item->limit == NULL || item->step == NULL ||
        item->initial->type != TYPE_INTEGER || item->initial->rank != 0U ||
        item->limit->type != TYPE_INTEGER || item->limit->rank != 0U ||
        item->step->type != TYPE_INTEGER || item->step->rank != 0U) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                                 &validation->group->span, 1,
                                 "DATA implied-DO bounds must be scalar INTEGER expressions");
        return 0;
    }
    if (!evaluate_bound(validation, item->initial, first) ||
        !evaluate_bound(validation, item->limit, &last) ||
        !evaluate_bound(validation, item->step, step)) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                                 &validation->group->span, 1,
                                 "DATA implied-DO bounds must be constant after outer iterator "
                                 "substitution");
        return 0;
    }
    if (*step == 0) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &item->step->span, 1,
                                 "DATA implied-DO step cannot be zero");
        return 0;
    }
    if (!f2c_integer_iteration_count(*first, last, *step, iterations) ||
        !expansion_within_budget(validation, *iterations))
        return 0;
    return 1;
}

static const F2cExpr *next_value(DataValueCursor *cursor) {
    if (cursor->remaining == 0U) {
        const F2cDataValue *value;
        if (cursor->index >= cursor->group->value_count)
            return NULL;
        value = &cursor->group->values[cursor->index++];
        cursor->current = value->expression;
        cursor->remaining = value->repeat_count;
    }
    if (cursor->remaining == 0U)
        return NULL;
    --cursor->remaining;
    return cursor->current;
}

static int value_cursor_has_values(const DataValueCursor *cursor) {
    return cursor->remaining != 0U || cursor->index < cursor->group->value_count;
}

static void validate_value_type(DataValidation *validation, const F2cExpr *target,
                                const F2cExpr *value) {
    if (target == NULL || value == NULL)
        return;
    if (!f2c_validation_type_compatible(target->type, value->type) ||
        (target->type == TYPE_DERIVED && target->derived_type != value->derived_type) ||
        (target->type == TYPE_CHARACTER && target->type_kind != value->type_kind)) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &value->span, 1,
                                 "DATA value type or kind is incompatible with its target");
    }
}

static int attach_scalar_initializer(DataValidation *validation, F2cIoItem *item, F2cExpr *target,
                                     const F2cExpr *value) {
    Symbol *symbol = target != NULL ? target->symbol : NULL;
    const char *source;
    char *initializer;
    F2cExpr *expression;
    if (symbol == NULL || target->kind != F2C_EXPR_NAME || target->rank != 0U ||
        symbol->module_entity || symbol->common_block != NULL || symbol->alias_to != NULL ||
        symbol->type == TYPE_CHARACTER || symbol->type == TYPE_COMPLEX ||
        symbol->type == TYPE_DOUBLE_COMPLEX || symbol->type == TYPE_DERIVED)
        return 0;
    if (symbol->initializer != NULL || symbol->initializer_expression != NULL) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                 "DATA target '%s' already has an initializer", symbol->name);
        return -1;
    }
    source = value->source != NULL ? value->source : value->text;
    initializer = source != NULL ? f2c_strdup(source) : NULL;
    expression = f2c_expr_clone_substitute_integers(value, NULL, 0U);
    if (initializer == NULL || expression == NULL) {
        free(initializer);
        f2c_expr_free(expression);
        f2c_diagnostic_code(validation->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                            validation->statement->line, 1,
                            "out of memory lowering DATA initializer for '%s'", symbol->name);
        return -1;
    }
    symbol->initializer = initializer;
    symbol->initializer_expression = expression;
    symbol->data_initializer = 1;
    item->data_static_initializer = 1;
    return 1;
}

static int validate_target(DataValidation *validation, F2cIoItem *item) {
    size_t child;
    if (!item->implied_do) {
        F2cExpr *target = item->expression;
        Symbol *symbol = target != NULL ? target->symbol : NULL;
        size_t extent;
        size_t element;
        if (target == NULL || !target->definable || target->value_category != F2C_VALUE_VARIABLE ||
            symbol == NULL || symbol->parameter || symbol->argument || symbol->allocatable ||
            symbol->pointer || symbol->procedure_pointer) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                                     target != NULL ? &target->span : &validation->group->span, 1,
                                     "DATA target must be a definable non-dummy, non-dynamic "
                                     "variable");
            return 0;
        }
        if (!expression_extent(target, &extent)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                     "DATA array target must have a constant explicit shape");
            return 0;
        }
        symbol->saved = 1;
        if (!add_size(&validation->target_count, extent)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                     &target->span, 1,
                                     "DATA target element count exceeds the supported size");
            return 0;
        }
        for (element = 0U; element < extent; ++element) {
            const F2cExpr *value = next_value(&validation->values);
            if (value != NULL) {
                validate_value_type(validation, target, value);
                if (extent == 1U && element == 0U &&
                    attach_scalar_initializer(validation, item, target, value) < 0)
                    return 0;
            }
        }
        return 1;
    }
    {
        int64_t first;
        int64_t step;
        int64_t current;
        uint64_t iterations;
        uint64_t iteration;
        int valid = 1;
        if (!implied_do_iterations(validation, item, &first, &step, &iterations) ||
            !push_binding(&validation->bindings, item->iterator))
            return 0;
        current = first;
        for (iteration = 0U; iteration < iterations; ++iteration) {
            validation->bindings.items[validation->bindings.count - 1U].value = current;
            for (child = 0U; child < item->child_count; ++child)
                valid &= validate_target(validation, &item->children[child]);
            if (iteration + 1U < iterations)
                current += step;
        }
        --validation->bindings.count;
        return valid;
    }
}

static int validate_values(DataValidation *validation) {
    size_t index;
    size_t total = 0U;
    int valid = 1;
    for (index = 0U; index < validation->group->value_count; ++index) {
        F2cDataValue *value = &validation->group->values[index];
        int64_t repeat = 1;
        f2c_validation_report_parse_error(validation->context, validation->statement->line,
                                          validation->statement->text, value->repeat,
                                          "DATA repeat");
        f2c_validation_report_parse_error(validation->context, validation->statement->line,
                                          validation->statement->text, value->expression,
                                          "DATA value");
        f2c_validation_constructor(validation->context, validation->unit,
                                   validation->statement->line, validation->statement->text,
                                   value->expression);
        f2c_validation_expression_calls(validation->context, validation->unit,
                                        validation->statement->line, validation->statement->text,
                                        value->repeat);
        f2c_validation_expression_calls(validation->context, validation->unit,
                                        validation->statement->line, validation->statement->text,
                                        value->expression);
        if (value->repeat != NULL &&
            (value->repeat->type != TYPE_INTEGER || value->repeat->rank != 0U ||
             !f2c_expression_is_initialization_constant(value->repeat) ||
             !f2c_evaluate_integer_constant(validation->unit, value->repeat, &repeat) ||
             repeat <= 0 || (uint64_t)repeat > (uint64_t)SIZE_MAX)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                                     &value->repeat->span, 1,
                                     "DATA repeat must be a positive scalar INTEGER constant");
            valid = 0;
            continue;
        }
        value->repeat_count = (uint64_t)repeat;
        if (value->expression == NULL || value->expression->rank != 0U ||
            !f2c_expression_is_initialization_constant(value->expression)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                                     value->expression != NULL ? &value->expression->span
                                                               : &value->span,
                                     1, "DATA value must be a scalar initialization expression");
            valid = 0;
        }
        if (!add_size(&total, (size_t)value->repeat_count)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                     &value->span, 1,
                                     "DATA value count exceeds the supported size");
            valid = 0;
        }
    }
    validation->group->expanded_value_count = total;
    return valid;
}

static void validate_group(Context *context, Unit *unit, F2cStatement *statement,
                           F2cDataGroup *group) {
    DataValidation validation = {0};
    size_t index;
    int valid;
    validation.context = context;
    validation.unit = unit;
    validation.statement = statement;
    validation.group = group;
    validation.values.group = group;
    valid = validate_values(&validation);
    for (index = 0U; index < group->target_count; ++index) {
        f2c_validation_io_item(context, statement->line, statement->text, &group->targets[index]);
        f2c_validation_io_item_calls(context, unit, statement->line, statement->text,
                                     &group->targets[index]);
        valid &= validate_target(&validation, &group->targets[index]);
    }
    group->expanded_target_count = validation.target_count;
    if (group->expanded_target_count != group->expanded_value_count ||
        value_cursor_has_values(&validation.values)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &group->span, 1,
                                 "DATA value count %zu does not match target element count %zu",
                                 group->expanded_value_count, group->expanded_target_count);
        valid = 0;
    }
    group->counts_valid = valid;
    free(validation.bindings.items);
}

void f2c_validation_data_statement(Context *context, Unit *unit, F2cStatement *statement) {
    size_t index;
    if (statement == NULL || statement->kind != F2C_STMT_DATA)
        return;
    if (!statement->data_syntax_valid) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "malformed DATA statement group syntax");
        return;
    }
    for (index = 0U; index < statement->data_group_count; ++index)
        validate_group(context, unit, statement, &statement->data_groups[index]);
}
