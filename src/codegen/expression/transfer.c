#include "codegen/expression/private.h"

#include "codegen/array/value.h"

#include <stdlib.h>

typedef struct F2cTransferSource {
    char *pointer;
    char *size;
    const char *release;
} F2cTransferSource;

static int scalar_source_is_owned(const F2cExpr *source) {
    source = f2c_expr_value_source(source);
    return source != NULL && source->type == TYPE_DERIVED && source->derived_type != NULL &&
           (source->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR ||
            (source->kind == F2C_EXPR_CALL && source->intrinsic != F2C_INTRINSIC_MERGE) ||
            source->resolved_procedure != NULL);
}

static void clear_source(F2cTransferSource *source) {
    if (source == NULL)
        return;
    free(source->pointer);
    free(source->size);
    source->pointer = NULL;
    source->size = NULL;
}

static int scalar_source(Unit *unit, const F2cExpr *source, F2cTransferSource *result,
                         int *supported) {
    Buffer pointer = {0};
    char *value;
    if (source->type == TYPE_CHARACTER) {
        value = f2c_expression_emit(unit, source, supported);
        result->pointer =
            *supported && value != NULL ? f2c_character_source_pointer(unit, source, value) : NULL;
        result->size = f2c_character_length_expression(unit, source);
        free(value);
        return *supported && result->pointer != NULL && result->size != NULL;
    }
    if (source->type == TYPE_UNKNOWN ||
        (source->type == TYPE_DERIVED && source->derived_type == NULL))
        return 0;
    value = f2c_expression_emit(unit, source, supported);
    if (!*supported || value == NULL) {
        free(value);
        return 0;
    }
    if (scalar_source_is_owned(source)) {
        f2c_buffer_printf(&pointer, "((%s[]){(%s)})", f2c_expression_c_type(source), value);
        result->release = source->derived_type->c_name;
    } else {
        f2c_buffer_printf(&pointer, "((const %s[]){(%s)})", f2c_expression_c_type(source), value);
    }
    result->pointer = f2c_buffer_take(&pointer);
    {
        Buffer size = {0};
        f2c_buffer_printf(&size, "sizeof(%s)", f2c_expression_c_type(source));
        result->size = f2c_buffer_take(&size);
    }
    free(value);
    return result->pointer != NULL && result->size != NULL;
}

static int array_source(Unit *unit, const F2cExpr *source, F2cTransferSource *result) {
    F2cArrayValue view = {0};
    Buffer size = {0};
    if (!f2c_array_value_view(unit, source, &view) || view.pointer == NULL || view.count == NULL) {
        f2c_array_value_clear(&view);
        return 0;
    }
    result->pointer = f2c_strdup(view.pointer);
    if (source->type == TYPE_CHARACTER && view.element_length != NULL)
        f2c_buffer_printf(&size, "f2c_size_multiply_checked((size_t)(%s), (size_t)(%s))",
                          view.count, view.element_length);
    else
        f2c_buffer_printf(&size, "f2c_size_multiply_checked((size_t)(%s), sizeof(%s))", view.count,
                          f2c_expression_c_type(source));
    result->size = f2c_buffer_take(&size);
    f2c_array_value_clear(&view);
    return result->pointer != NULL && result->size != NULL;
}

static const char *target_helper(const F2cExpr *expression) {
    const int kind =
        expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(expression->type);
    switch (expression->type) {
    case TYPE_INTEGER:
        return kind == 1   ? "f2c_transfer_i8"
               : kind == 2 ? "f2c_transfer_i16"
               : kind == 8 ? "f2c_transfer_i64"
                           : "f2c_transfer_i32";
    case TYPE_LOGICAL:
        return kind == 1   ? "f2c_transfer_logical"
               : kind == 2 ? "f2c_transfer_i16"
               : kind == 8 ? "f2c_transfer_i64"
                           : "f2c_transfer_i32";
    case TYPE_REAL:
    case TYPE_DOUBLE:
        return kind == 4 ? "f2c_transfer_r4" : kind == 8 ? "f2c_transfer_r8" : "f2c_transfer_r16";
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        return kind == 4 ? "f2c_transfer_c4" : kind == 8 ? "f2c_transfer_c8" : "f2c_transfer_c16";
    default:
        return NULL;
    }
}

static char *emit_transfer_value(Unit *unit, const F2cExpr *expression, const F2cExpr *source,
                                 const F2cExpr *mold, int *supported) {
    F2cTransferSource storage = {0};
    Buffer result = {0};
    const char *helper;
    if (source != NULL && source->type == TYPE_DERIVED && source->rank == 0U)
        source = f2c_expr_value_source(source);
    if (source != NULL && source->rank == 0U && source->type == TYPE_DERIVED &&
        source->kind == F2C_EXPR_CALL && source->intrinsic == F2C_INTRINSIC_MERGE) {
        const F2cExpr *true_source =
            f2c_intrinsic_argument(source->children, source->child_count, "tsource", 0U);
        const F2cExpr *false_source =
            f2c_intrinsic_argument(source->children, source->child_count, "fsource", 1U);
        const F2cExpr *mask =
            f2c_intrinsic_argument(source->children, source->child_count, "mask", 2U);
        char *true_value;
        char *false_value;
        char *mask_value;
        if (true_source == NULL || false_source == NULL || mask == NULL || mask->rank != 0U) {
            *supported = 0;
            return NULL;
        }
        true_value = emit_transfer_value(unit, expression, true_source, mold, supported);
        false_value = *supported
                          ? emit_transfer_value(unit, expression, false_source, mold, supported)
                          : NULL;
        mask_value = *supported ? f2c_expression_emit(unit, mask, supported) : NULL;
        if (!*supported || true_value == NULL || false_value == NULL || mask_value == NULL) {
            free(true_value);
            free(false_value);
            free(mask_value);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "((bool)(%s) ? (%s) : (%s))", mask_value, true_value,
                          false_value);
        free(true_value);
        free(false_value);
        free(mask_value);
        return f2c_buffer_take(&result);
    }
    if (source == NULL || mold == NULL ||
        !(source->rank == 0U ? scalar_source(unit, source, &storage, supported)
                             : array_source(unit, source, &storage))) {
        clear_source(&storage);
        *supported = 0;
        return NULL;
    }
    if (expression->type == TYPE_CHARACTER) {
        char *length = f2c_character_length_expression(unit, mold);
        if (expression->temporary_index == SIZE_MAX || length == NULL) {
            free(length);
            clear_source(&storage);
            *supported = 0;
            return NULL;
        }
        if (storage.release != NULL)
            f2c_buffer_printf(
                &result,
                "f2c_transfer_character_owned(&f2c_character_result_%zu, (size_t)(%s), "
                "%s, (size_t)(%s), f2c_transfer_destroy_%s)",
                expression->temporary_index, length, storage.pointer, storage.size,
                storage.release);
        else
            f2c_buffer_printf(&result,
                              "f2c_transfer_character(&f2c_character_result_%zu, (size_t)(%s), "
                              "%s, (size_t)(%s))",
                              expression->temporary_index, length, storage.pointer, storage.size);
        free(length);
    } else if (expression->type == TYPE_DERIVED && expression->derived_type != NULL) {
        f2c_buffer_printf(&result, "f2c_transfer_%s%s(%s, (size_t)(%s)",
                          expression->derived_type->c_name, storage.release != NULL ? "_owned" : "",
                          storage.pointer, storage.size);
        if (storage.release != NULL)
            f2c_buffer_printf(&result, ", f2c_transfer_destroy_%s", storage.release);
        f2c_buffer_append(&result, ")");
    } else {
        helper = target_helper(expression);
        if (helper == NULL) {
            clear_source(&storage);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "%s%s(%s, (size_t)(%s)", helper,
                          storage.release != NULL ? "_owned" : "", storage.pointer, storage.size);
        if (storage.release != NULL)
            f2c_buffer_printf(&result, ", f2c_transfer_destroy_%s", storage.release);
        f2c_buffer_append(&result, ")");
    }
    clear_source(&storage);
    return f2c_buffer_take(&result);
}

char *f2c_expression_transfer_intrinsic(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source;
    const F2cExpr *mold;
    if (unit == NULL || expression == NULL || supported == NULL || expression->rank != 0U) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    source = f2c_intrinsic_argument(expression->children, expression->child_count, "source", 0U);
    mold = f2c_intrinsic_argument(expression->children, expression->child_count, "mold", 1U);
    return emit_transfer_value(unit, expression, source, mold, supported);
}
