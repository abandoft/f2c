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
    f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                             &validation->group->span, 1,
                             "DATA expansion exceeds the constant-step limit of %zu", limit);
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

static int expression_has_static_c_form(const F2cExpr *expression, size_t depth) {
    size_t child;
    if (expression == NULL || depth > 64U)
        return 0;
    switch (expression->kind) {
    case F2C_EXPR_INTEGER_LITERAL:
    case F2C_EXPR_REAL_LITERAL:
    case F2C_EXPR_LOGICAL_LITERAL:
        return 1;
    case F2C_EXPR_NAME:
        return expression->symbol != NULL && expression->symbol->parameter &&
               expression_has_static_c_form(expression->symbol->initializer_expression, depth + 1U);
    case F2C_EXPR_UNARY:
    case F2C_EXPR_BINARY:
        break;
    default:
        return 0;
    }
    for (child = 0U; child < expression->child_count; ++child)
        if (!expression_has_static_c_form(expression->children[child], depth + 1U))
            return 0;
    return expression->child_count != 0U;
}

static int symbol_supports_static_data(const Unit *unit, const Symbol *symbol) {
    if (symbol == NULL || symbol->alias_to != NULL || symbol->allocatable || symbol->pointer ||
        symbol->procedure_pointer || symbol->type == TYPE_DERIVED)
        return 0;
    if (symbol->common_block != NULL)
        return unit != NULL && unit->kind == UNIT_BLOCK_DATA && symbol->common_block[0] != '\0';
    if (symbol->module_entity)
        return unit != NULL && unit->kind == UNIT_MODULE;
    return symbol->type != TYPE_CHARACTER && symbol->type != TYPE_COMPLEX &&
           symbol->type != TYPE_DOUBLE_COMPLEX;
}

static int common_value_has_static_form(Unit *unit, const Symbol *symbol, const F2cExpr *value) {
    int64_t integer_value;
    double real_value;
    char *character_value = NULL;
    size_t character_length = 0U;
    int supported = 0;
    if (symbol == NULL || value == NULL)
        return 0;
    switch (symbol->type) {
    case TYPE_INTEGER:
    case TYPE_LOGICAL:
        return f2c_evaluate_integer_constant(unit, value, &integer_value);
    case TYPE_REAL:
    case TYPE_DOUBLE:
        return f2c_evaluate_real_constant(unit, value, &real_value);
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        if (value->kind == F2C_EXPR_COMPLEX_LITERAL && value->child_count == 2U)
            return f2c_evaluate_real_constant(unit, value->children[0], &real_value) &&
                   f2c_evaluate_real_constant(unit, value->children[1], &real_value);
        return f2c_evaluate_real_constant(unit, value, &real_value);
    case TYPE_CHARACTER:
        supported =
            f2c_evaluate_character_constant(unit, value, &character_value, &character_length);
        free(character_value);
        return supported;
    case TYPE_DERIVED:
    case TYPE_UNKNOWN:
    default:
        return 0;
    }
}

static int data_value_has_static_form(Unit *unit, const Symbol *symbol, const F2cExpr *value) {
    return symbol != NULL && (symbol->common_block != NULL || symbol->module_entity)
               ? common_value_has_static_form(unit, symbol, value)
               : expression_has_static_c_form(value, 0U);
}

static void mark_data_storage_saved(Unit *unit, Symbol *symbol) {
    size_t remaining = unit != NULL ? unit->symbol_count : 0U;
    while (symbol != NULL) {
        symbol->saved = 1;
        if (symbol->alias_to == NULL || remaining-- == 0U)
            return;
        symbol = f2c_find_symbol(unit, symbol->alias_to);
    }
}

static void free_initializers(F2cExpr **initializers, size_t count) {
    size_t index;
    if (initializers == NULL)
        return;
    for (index = 0U; index < count; ++index)
        f2c_expr_free(initializers[index]);
    free(initializers);
}

static int array_element_offset(DataValidation *validation, const F2cExpr *target, size_t *offset,
                                size_t *element_count) {
    const Symbol *symbol = target != NULL ? target->symbol : NULL;
    size_t stride = 1U;
    size_t result = 0U;
    size_t dimension;
    if (target == NULL || target->kind != F2C_EXPR_ARRAY_REFERENCE || symbol == NULL ||
        symbol->rank == 0U || target->rank != 0U || target->child_count != symbol->rank ||
        symbol->shape.rank != symbol->rank)
        return 0;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        const F2cShapeDimension *shape = &symbol->shape.dimensions[dimension];
        F2cExpr *subscript;
        int64_t value;
        uint64_t distance;
        size_t extent;
        if (!shape->lower_known || !shape->extent_known || shape->extent > (uint64_t)SIZE_MAX)
            return 0;
        subscript = f2c_expr_clone_substitute_integers(
            target->children[dimension], validation->bindings.items, validation->bindings.count);
        if (subscript == NULL ||
            !f2c_evaluate_integer_constant(validation->unit, subscript, &value)) {
            f2c_expr_free(subscript);
            f2c_diagnostic_span_code(
                validation->context, F2C_DIAGNOSTIC_SEMANTIC,
                target->children[dimension] != NULL ? &target->children[dimension]->span
                                                    : &target->span,
                1, "DATA array subscript must be constant after implied-DO substitution");
            return -1;
        }
        f2c_expr_free(subscript);
        distance = (uint64_t)value - (uint64_t)shape->lower;
        if (value < shape->lower || distance >= shape->extent) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                     "DATA array subscript is outside the declared bounds of '%s'",
                                     symbol->name);
            return -1;
        }
        extent = (size_t)shape->extent;
        if ((size_t)distance > (SIZE_MAX - result) / stride ||
            (extent != 0U && stride > SIZE_MAX / extent)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                     &target->span, 1,
                                     "DATA array offset exceeds the supported size range");
            return -1;
        }
        result += (size_t)distance * stride;
        stride *= extent;
    }
    *offset = result;
    *element_count = stride;
    return 1;
}

static int attach_array_element_initializer(DataValidation *validation, F2cIoItem *item,
                                            F2cExpr *target, const F2cExpr *value) {
    Symbol *symbol = target != NULL ? target->symbol : NULL;
    F2cExpr **initializers;
    size_t offset;
    size_t count;
    int mapped;
    if (target == NULL || target->kind != F2C_EXPR_ARRAY_REFERENCE || target->rank != 0U)
        return 0;
    mapped = array_element_offset(validation, target, &offset, &count);
    if (mapped <= 0)
        return mapped;
    if (!expansion_within_budget(validation, (uint64_t)count))
        return -1;
    if (!symbol_supports_static_data(validation->unit, symbol) ||
        !data_value_has_static_form(validation->unit, symbol, value)) {
        item->data_static_initializer = -1;
        return 0;
    }
    if (symbol->data_element_initializers == NULL) {
        initializers = (F2cExpr **)calloc(count, sizeof(*initializers));
        if (initializers == NULL) {
            f2c_diagnostic_code(validation->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                validation->statement->line, 1,
                                "out of memory lowering DATA initializer for '%s'", symbol->name);
            return -1;
        }
        symbol->data_element_initializers = initializers;
        symbol->data_element_initializer_count = count;
    } else if (symbol->data_element_initializer_count != count) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                 "inconsistent DATA shape for array '%s'", symbol->name);
        return -1;
    }
    if (symbol->data_element_initializers[offset] != NULL) {
        f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                 "DATA element of '%s' is initialized more than once",
                                 symbol->name);
        return -1;
    }
    symbol->data_element_initializers[offset] = f2c_expr_clone_substitute_integers(
        value, validation->bindings.items, validation->bindings.count);
    if (symbol->data_element_initializers[offset] == NULL) {
        f2c_diagnostic_code(validation->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                            validation->statement->line, 1,
                            "out of memory lowering DATA initializer for '%s'", symbol->name);
        return -1;
    }
    if (item->data_static_initializer >= 0)
        item->data_static_initializer = 1;
    return 1;
}

static int attach_scalar_initializer(DataValidation *validation, F2cIoItem *item, F2cExpr *target,
                                     const F2cExpr *value) {
    Symbol *symbol = target != NULL ? target->symbol : NULL;
    const char *source;
    char *initializer;
    F2cExpr *expression;
    if (symbol == NULL || target->kind != F2C_EXPR_NAME || target->rank != 0U ||
        !symbol_supports_static_data(validation->unit, symbol) ||
        !data_value_has_static_form(validation->unit, symbol, value))
        return 0;
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

static int prepare_array_initializers(DataValidation *validation, F2cIoItem *item, F2cExpr *target,
                                      F2cExpr ***initializers, size_t extent) {
    Symbol *symbol = target != NULL ? target->symbol : NULL;
    if (initializers == NULL || extent == 0U || target == NULL || target->kind != F2C_EXPR_NAME ||
        target->rank == 0U || !symbol_supports_static_data(validation->unit, symbol))
        return 0;
    *initializers = (F2cExpr **)calloc(extent, sizeof(**initializers));
    if (*initializers == NULL) {
        f2c_diagnostic_code(validation->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                            validation->statement->line, 1,
                            "out of memory lowering DATA initializer for '%s'", symbol->name);
        return -1;
    }
    item->data_static_initializer = 1;
    return 1;
}

static int validate_target(DataValidation *validation, F2cIoItem *item) {
    size_t child;
    if (!item->implied_do) {
        F2cExpr *target = item->expression;
        Symbol *symbol = target != NULL ? target->symbol : NULL;
        F2cExpr **array_initializers = NULL;
        int static_array = 0;
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
        if (symbol->use_associated) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                     "DATA target cannot be a use-associated entity");
            return 0;
        }
        if (symbol->common_block != NULL && validation->unit->kind != UNIT_BLOCK_DATA) {
            f2c_diagnostic_span_code(
                validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                "a COMMON object may be initialized only in a BLOCK DATA program unit");
            return 0;
        }
        if (validation->unit->kind == UNIT_BLOCK_DATA &&
            (symbol->common_block == NULL || symbol->common_block[0] == '\0')) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                     "a BLOCK DATA target must belong to a named COMMON block");
            return 0;
        }
        if (!expression_extent(target, &extent)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                     "DATA array target must have a constant explicit shape");
            return 0;
        }
        if (!expansion_within_budget(validation, (uint64_t)extent))
            return 0;
        if (symbol->initializer != NULL || symbol->initializer_expression != NULL ||
            symbol->data_initializer ||
            (target->kind == F2C_EXPR_NAME && symbol->data_element_initializers != NULL)) {
            f2c_diagnostic_span_code(validation->context, F2C_DIAGNOSTIC_SEMANTIC, &target->span, 1,
                                     "DATA target '%s' already has an initializer", symbol->name);
            return 0;
        }
        if (target->kind == F2C_EXPR_NAME && target->rank != 0U) {
            static_array =
                prepare_array_initializers(validation, item, target, &array_initializers, extent);
            if (static_array < 0)
                return 0;
        }
        mark_data_storage_saved(validation->unit, symbol);
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
                if (static_array && data_value_has_static_form(validation->unit, symbol, value)) {
                    array_initializers[element] =
                        f2c_expr_clone_substitute_integers(value, NULL, 0U);
                    if (array_initializers[element] == NULL) {
                        f2c_diagnostic_code(validation->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                            validation->statement->line, 1,
                                            "out of memory lowering DATA initializer for '%s'",
                                            symbol->name);
                        free_initializers(array_initializers, extent);
                        return 0;
                    }
                } else if (static_array) {
                    item->data_static_initializer = 0;
                    static_array = 0;
                }
                if (extent == 1U && element == 0U && target->rank == 0U &&
                    attach_scalar_initializer(validation, item, target, value) < 0) {
                    free_initializers(array_initializers, extent);
                    return 0;
                }
                if (extent == 1U && element == 0U &&
                    attach_array_element_initializer(validation, item, target, value) < 0) {
                    free_initializers(array_initializers, extent);
                    return 0;
                }
            } else if (static_array) {
                item->data_static_initializer = 0;
                static_array = 0;
            }
        }
        if (target->kind == F2C_EXPR_NAME)
            symbol->data_initializer = 1;
        if (static_array) {
            symbol->data_element_initializers = array_initializers;
            symbol->data_element_initializer_count = extent;
        } else {
            free_initializers(array_initializers, extent);
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

static int data_item_has_static_initializer(const F2cIoItem *item) {
    size_t child;
    if (item == NULL)
        return 0;
    if (!item->implied_do)
        return item->data_static_initializer == 1;
    for (child = 0U; child < item->child_count; ++child)
        if (!data_item_has_static_initializer(&item->children[child]))
            return 0;
    return 1;
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
    if ((unit->kind == UNIT_BLOCK_DATA || unit->kind == UNIT_MODULE) && valid) {
        for (index = 0U; index < group->target_count; ++index) {
            if (!data_item_has_static_initializer(&group->targets[index])) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_UNSUPPORTED, &group->span, 1,
                    "%s initializer cannot be represented as portable static C17 data",
                    unit->kind == UNIT_BLOCK_DATA ? "BLOCK DATA" : "module DATA");
                valid = 0;
                break;
            }
        }
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
