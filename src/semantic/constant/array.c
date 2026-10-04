#include "semantic/constant/array.h"
#include "semantic/constant/private.h"
#include "semantic/constant/transform/private.h"

#include "core/numeric/loop.h"
#include "internal/f2c.h"
#include "semantic/scope.h"

#include <stdlib.h>
#include <string.h>

typedef struct F2cConstantArrayCache {
    const Symbol *symbol;
    F2cConstantArray value;
    int busy;
    int valid;
    struct F2cConstantArrayCache *next;
} F2cConstantArrayCache;

void f2c_constant_evaluation_finish(F2cConstantEvaluation *evaluation) {
    while (evaluation->arrays != NULL) {
        F2cConstantArrayCache *entry = evaluation->arrays;
        evaluation->arrays = entry->next;
        f2c_constant_array_free(&entry->value);
        free(entry);
    }
}

int f2c_constant_array_allocate(F2cConstantEvaluation *evaluation, F2cConstantArray *array,
                                size_t count) {
    if (count > SIZE_MAX / sizeof(*array->values) ||
        !f2c_reserve_constant_steps(evaluation->unit, count) ||
        !f2c_constant_array_reserve(array, count))
        return 0;
    if (count != 0U)
        memset(array->values, 0, count * sizeof(*array->values));
    array->count = count;
    return 1;
}

static int append_value(F2cConstantEvaluation *evaluation, F2cConstantArray *array,
                        F2cConstantValue *value, size_t depth) {
    if (!f2c_constant_consume_step(evaluation, depth) || array->count == SIZE_MAX)
        return 0;
    if (array->count == array->capacity) {
        const size_t capacity = array->capacity == 0U              ? 8U
                                : array->capacity <= SIZE_MAX / 2U ? array->capacity * 2U
                                                                   : SIZE_MAX;
        if (!f2c_constant_array_reserve(array, capacity))
            return 0;
    }
    array->values[array->count++] = *value;
    memset(value, 0, sizeof(*value));
    return 1;
}

static int append_expression(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                             F2cConstantArray *array, size_t depth);

static int append_implied_do(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                             F2cConstantArray *array, size_t depth) {
    if (expression->child_count < 4U || expression->text == NULL)
        return 0;
    const size_t values = expression->child_count - 3U;
    int64_t first, last, step;
    uint64_t count;
    const int kind = expression->symbol != NULL ? expression->symbol->kind : 0;
    if (!f2c_constant_evaluate_integer(evaluation, expression->children[values], &first,
                                       depth + 1U) ||
        !f2c_constant_evaluate_integer(evaluation, expression->children[values + 1U], &last,
                                       depth + 1U) ||
        !f2c_constant_evaluate_integer(evaluation, expression->children[values + 2U], &step,
                                       depth + 1U) ||
        !f2c_integer_loop_parameters_fit(kind, first, last, step) ||
        !f2c_integer_iteration_count(first, last, step, &count) || count > SIZE_MAX ||
        !f2c_reserve_constant_steps(evaluation->unit, (size_t)count))
        return 0;
    int64_t iterator = first;
    for (uint64_t trip = 0U; trip < count; ++trip) {
        const F2cIntegerSubstitution substitution = {expression->symbol, expression->text,
                                                     iterator};
        for (size_t value = 0U; value < values; ++value) {
            F2cExpr *item =
                f2c_expr_clone_substitute_integers(expression->children[value], &substitution, 1U);
            const int success =
                item != NULL && append_expression(evaluation, item, array, depth + 1U);
            f2c_expr_free(item);
            if (!success)
                return 0;
        }
        if (trip + 1U < count)
            iterator = f2c_integer_loop_add_i64(iterator, step);
    }
    return 1;
}

static int append_expression(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                             F2cConstantArray *array, size_t depth) {
    expression = f2c_expr_value_source(expression);
    if (expression == NULL || !f2c_constant_consume_step(evaluation, depth))
        return 0;
    if (expression->kind == F2C_EXPR_IMPLIED_DO)
        return append_implied_do(evaluation, expression, array, depth);
    if (expression->rank != 0U) {
        F2cConstantArray source = {0};
        if (!f2c_constant_evaluate_array(evaluation, expression, &source, depth + 1U))
            return 0;
        for (size_t index = 0U; index < source.count; ++index)
            if (!append_value(evaluation, array, &source.values[index], depth + 1U)) {
                f2c_constant_array_free(&source);
                return 0;
            }
        /* The elements were moved into the constructor's owned storage. */
        f2c_constant_array_free(&source);
        return 1;
    }
    F2cConstantValue value = {0};
    const int result = f2c_constant_evaluate_value(evaluation, expression, &value, depth + 1U) &&
                       append_value(evaluation, array, &value, depth + 1U);
    f2c_constant_value_free(&value);
    return result;
}

int f2c_constant_evaluate_storage(F2cConstantEvaluation *evaluation, const Symbol *symbol,
                                  F2cConstantArray *result, size_t depth) {
    F2cConstantArray source = {0}, storage = {0};
    if (symbol == NULL || symbol->initializer_expression == NULL ||
        !f2c_constant_consume_step(evaluation, depth))
        return 0;
    storage.type = (F2cScalarType){
        symbol->type, symbol->kind != 0 ? symbol->kind : f2c_default_kind(symbol->type)};
    storage.derived_type = symbol->derived_type;
    size_t count;
    if (!f2c_constant_storage_shape(evaluation, symbol, &storage.shape, &storage.character_length,
                                    depth + 1U) ||
        !f2c_constant_shape_count(&storage.shape, &count))
        return 0;
    if (!f2c_constant_evaluate_array(evaluation, symbol->initializer_expression, &source,
                                     depth + 1U) ||
        (source.shape.rank != 0U && !f2c_constant_conformable(&source.shape, &storage.shape)) ||
        !f2c_constant_array_allocate(evaluation, &storage, count))
        goto failed;
    for (size_t index = 0U; index < count; ++index) {
        const size_t position = source.shape.rank == 0U ? 0U : index;
        if (position >= source.count ||
            !f2c_constant_value_copy(&storage.values[index], &source.values[position]) ||
            !f2c_constant_value_convert(&storage.values[index], storage.type, storage.derived_type,
                                        storage.character_length))
            goto failed;
    }
    f2c_constant_array_free(&source);
    *result = storage;
    return 1;
failed:
    f2c_constant_array_free(&source);
    f2c_constant_array_free(&storage);
    return 0;
}

static const F2cConstantArray *parameter_array(F2cConstantEvaluation *evaluation,
                                               const Symbol *symbol, size_t depth) {
    F2cConstantArrayCache *entry;
    for (entry = evaluation->arrays; entry != NULL; entry = entry->next)
        if (entry->symbol == symbol)
            return entry->valid && !entry->busy ? &entry->value : NULL;
    entry = (F2cConstantArrayCache *)calloc(1U, sizeof(*entry));
    if (entry == NULL)
        return NULL;
    entry->symbol = symbol;
    entry->busy = 1;
    entry->next = evaluation->arrays;
    evaluation->arrays = entry;
    Unit *caller = evaluation->unit;
    evaluation->unit = f2c_symbol_specification_scope(caller, symbol);
    entry->valid = f2c_constant_evaluate_storage(evaluation, symbol, &entry->value, depth + 1U);
    evaluation->unit = caller;
    entry->busy = 0;
    return entry->valid ? &entry->value : NULL;
}

const F2cConstantValue *f2c_constant_array_element(F2cConstantEvaluation *evaluation,
                                                   const F2cExpr *expression, size_t depth) {
    if (expression == NULL || expression->kind != F2C_EXPR_ARRAY_REFERENCE ||
        expression->symbol == NULL || !expression->symbol->parameter || expression->rank != 0U ||
        expression->child_count != expression->symbol->rank ||
        !f2c_constant_consume_step(evaluation, depth))
        return NULL;
    const F2cConstantArray *array = parameter_array(evaluation, expression->symbol, depth + 1U);
    if (array == NULL)
        return NULL;
    size_t offset = 0U, stride = 1U;
    for (size_t dimension = 0U; dimension < array->shape.rank; ++dimension) {
        int64_t index;
        const F2cShapeDimension *shape = &array->shape.dimensions[dimension];
        if (!shape->lower_known ||
            !f2c_constant_evaluate_integer(evaluation, expression->children[dimension], &index,
                                           depth + 1U) ||
            index < shape->lower)
            return NULL;
        const uint64_t position = (uint64_t)index - (uint64_t)shape->lower;
        if (position >= shape->extent || position > SIZE_MAX / stride ||
            (size_t)position * stride > SIZE_MAX - offset)
            return NULL;
        offset += (size_t)position * stride;
        if (shape->extent > SIZE_MAX / stride)
            return NULL;
        stride *= (size_t)shape->extent;
    }
    return offset < array->count ? &array->values[offset] : NULL;
}

int f2c_constant_evaluate_array(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                F2cConstantArray *result, size_t depth) {
    F2cConstantArray array = {0};
    expression = f2c_expr_value_source(expression);
    if (expression == NULL || !f2c_constant_consume_step(evaluation, depth))
        return 0;
    if (expression->kind == F2C_EXPR_CALL &&
        f2c_constant_transform_supported(expression->intrinsic))
        return f2c_constant_evaluate_transform(evaluation, expression, result, depth + 1U);
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter && expression->rank != 0U) {
        const F2cConstantArray *source =
            parameter_array(evaluation, expression->symbol, depth + 1U);
        return source != NULL && f2c_constant_array_copy(result, source);
    }
    array.type = (F2cScalarType){expression->type, expression->type_kind != 0
                                                       ? expression->type_kind
                                                       : f2c_default_kind(expression->type)};
    array.derived_type = expression->derived_type;
    if (array.type.type == TYPE_UNKNOWN)
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (size_t index = 0U; index < expression->child_count; ++index)
            if (!append_expression(evaluation, expression->children[index], &array, depth + 1U))
                goto failed;
        array.shape.kind = F2C_SHAPE_EXPRESSION;
        array.shape.rank = 1U;
        array.shape.dimensions[0] =
            (F2cShapeDimension){F2C_DIMENSION_EXPLICIT, 1, 1, 1, array.count};
    } else if (expression->rank == 0U) {
        if (!f2c_constant_array_allocate(evaluation, &array, 1U) ||
            !f2c_constant_evaluate_value(evaluation, expression, &array.values[0], depth + 1U))
            goto failed;
        array.shape.kind = F2C_SHAPE_SCALAR;
    } else {
        return 0;
    }
    if (array.type.type == TYPE_CHARACTER && array.count != 0U)
        array.character_length = array.values[0].payload.character.length;
    for (size_t index = 0U; index < array.count; ++index) {
        if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
            const F2cConstantValue *value = &array.values[index];
            const F2cConstantArray element = {
                .type = value->type,
                .derived_type =
                    value->type.type == TYPE_DERIVED ? value->payload.derived->derived_type : NULL,
                .character_length =
                    value->type.type == TYPE_CHARACTER ? value->payload.character.length : 0U};
            if (!f2c_constant_same_element(&array, &element))
                goto failed;
        }
        if (!f2c_constant_value_convert(&array.values[index], array.type, array.derived_type,
                                        array.character_length))
            goto failed;
    }
    *result = array;
    return 1;
failed:
    f2c_constant_array_free(&array);
    return 0;
}

int f2c_evaluate_constant_array(Unit *unit, const F2cExpr *expression, F2cConstantArray *result) {
    F2cConstantEvaluation evaluation = {.unit = unit,
                                        .context = unit != NULL ? unit->context : NULL};
    memset(result, 0, sizeof(*result));
    const int success = f2c_constant_evaluate_array(&evaluation, expression, result, 0U);
    f2c_constant_evaluation_finish(&evaluation);
    return success;
}

int f2c_evaluate_constant_storage(Unit *unit, const Symbol *symbol, F2cConstantArray *result) {
    F2cConstantEvaluation evaluation = {.unit = unit,
                                        .context = unit != NULL ? unit->context : NULL};
    memset(result, 0, sizeof(*result));
    const int success = f2c_constant_evaluate_storage(&evaluation, symbol, result, 0U);
    f2c_constant_evaluation_finish(&evaluation);
    return success;
}
