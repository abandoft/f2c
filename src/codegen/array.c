#include "codegen/array/private.h"
#include "codegen/call/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

void f2c_array_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

char *f2c_array_emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

char *f2c_symbol_element_count(Unit *unit, Symbol *symbol) {
    Buffer count = {0};
    size_t d;
    if (f2c_symbol_is_automatic_array(unit, symbol)) {
        f2c_buffer_printf(&count, "f2c_auto_count_%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&count);
    }
    for (d = 0U; d < symbol->rank; ++d) {
        char *extent = f2c_symbol_dimension_extent(unit, symbol, d);
        if (extent == NULL) {
            free(f2c_buffer_take(&count));
            return NULL;
        }
        f2c_buffer_printf(&count, "%s(size_t)(%s)", d == 0U ? "" : " * ", extent);
        free(extent);
    }
    return f2c_buffer_take(&count);
}

int f2c_emit_whole_array_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                    const F2cExpr *right, size_t line, int depth) {
    Symbol *left_symbol = left != NULL ? left->symbol : NULL;
    Symbol *right_symbol = right != NULL && right->kind == F2C_EXPR_NAME ? right->symbol : NULL;
    char *element_count;
    const int has_constructor = right != NULL && right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR;
    if (left == NULL || left->kind != F2C_EXPR_NAME || left_symbol == NULL ||
        left_symbol->rank == 0U)
        return 0;
    if (f2c_array_emit_prepared_transform_assignment(context, unit, left, right, line, depth))
        return 1;
    if (f2c_emit_transform_assignment(context, unit, left, right, line, depth))
        return 1;
    if (left_symbol->allocatable && right != NULL && right->kind == F2C_EXPR_CALL &&
        f2c_expression_has_allocatable_result(right) &&
        (left_symbol->type != TYPE_CHARACTER || left_symbol->deferred_character)) {
        const int state_parameters = f2c_call_has_object_state_parameters(right);
        char *result = state_parameters ? NULL : f2c_array_emit_expression(unit, right);
        const F2cStorageReference reference = f2c_ir_storage_reference(left);
        char *name = f2c_storage_write_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        char *old_count =
            left_symbol->type == TYPE_DERIVED ? f2c_symbol_element_count(unit, left_symbol) : NULL;
        size_t dimension;
        if (name == NULL || (!state_parameters && result == NULL) ||
            right->rank != left_symbol->rank || right->type != left_symbol->type ||
            right->type_kind != left_symbol->kind ||
            (left_symbol->type == TYPE_DERIVED &&
             (right->derived_type == NULL || right->derived_type != left_symbol->derived_type)) ||
            (left_symbol->type == TYPE_DERIVED && old_count == NULL)) {
            free(result);
            free(old_count);
            free(name);
            f2c_diagnostic(context, line, 1,
                           "allocatable function-result assignment requires matching type, kind, "
                           "rank, and derived type");
            return 1;
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        f2c_array_indent(&context->output, depth + 1);
        if (state_parameters) {
            f2c_buffer_append(&context->output, "f2c_descriptor f2c_function_result;\n");
            if (!f2c_call_emit_function_value(&context->output, unit, right, "f2c_function_result",
                                              depth + 1)) {
                free(old_count);
                free(name);
                return 1;
            }
        } else {
            f2c_buffer_printf(&context->output, "f2c_descriptor f2c_function_result = %s;\n",
                              result);
        }
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(
            &context->output,
            "if (!f2c_descriptor_bridge_valid(&f2c_function_result, %zuU, sizeof(%s)) || "
            "!f2c_function_result.deallocatable || "
            "!f2c_descriptor_is_contiguous(%zuU, (const size_t[]){",
            left_symbol->rank, f2c_symbol_c_type(left_symbol), left_symbol->rank);
        for (dimension = 0U; dimension < left_symbol->rank; ++dimension)
            f2c_buffer_printf(&context->output, "%s(size_t)f2c_function_result.extent[%zu]",
                              dimension == 0U ? "" : ", ", dimension);
        f2c_buffer_append(&context->output, "}, f2c_function_result.stride)) abort();\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (f2c_descriptor_element_count(&f2c_function_result) != 0U && "
                          "f2c_function_result.data == NULL) abort();\n");
        if (left_symbol->type == TYPE_DERIVED) {
            f2c_array_indent(&context->output, depth + 1);
            f2c_buffer_printf(
                &context->output, "if (%s != NULL) f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                name, left_symbol->derived_type->c_name, name, old_count, left_symbol->rank);
        }
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "free(%s);\n", name);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = (%s *)f2c_function_result.data;\n", name,
                          f2c_symbol_c_type(left_symbol));
        if (left_symbol->deferred_character) {
            if (!f2c_storage_emit_store(&context->output, unit, &reference,
                                        F2C_OBJECT_CHARACTER_LENGTH, 0U, NULL,
                                        "f2c_function_result.character_length", depth + 1))
                context->output.failed = 1;
        }
        for (dimension = 0U; dimension < left_symbol->rank; ++dimension) {
            f2c_array_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_function_result.lower[%zu] < INT32_MIN || "
                              "f2c_function_result.lower[%zu] > INT32_MAX || "
                              "f2c_function_result.extent[%zu] < 0 || "
                              "f2c_function_result.extent[%zu] > INT32_MAX) abort();\n",
                              dimension, dimension, dimension, dimension);
            Buffer extent = {0};
            f2c_buffer_printf(&extent, "(int32_t)f2c_function_result.extent[%zu]", dimension);
            if (!f2c_storage_emit_contiguous_dimension(&context->output, unit, &reference,
                                                       dimension, NULL, "1", extent.data,
                                                       depth + 1))
                context->output.failed = 1;
            free(extent.data);
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(result);
        free(old_count);
        free(name);
        return 1;
    }
    if (f2c_emit_allocatable_array_assignment(context, unit, left, right, depth))
        return 1;
    if (left_symbol->allocatable && has_constructor) {
        const int emitted = left_symbol->type == TYPE_CHARACTER
                                ? f2c_array_emit_allocatable_character_constructor(
                                      context, unit, left_symbol, right, depth)
                                : f2c_array_emit_allocatable_numeric_constructor(
                                      context, unit, left_symbol, right, depth);
        if (!emitted)
            f2c_diagnostic(context, line, 1,
                           "allocatable array-constructor assignment requires a supported "
                           "rank-one intrinsic target and compatible element values");
        return 1;
    }
    if (f2c_array_emit_elemental_assignment(context, unit, left_symbol, right, line, depth))
        return 1;
    if (left_symbol->allocatable || left_symbol->pointer) {
        char *data = f2c_storage_symbol_data(unit, left_symbol);
        if (data == NULL)
            return 0;
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (%s == NULL) abort();\n", data);
        free(data);
    }
    element_count = f2c_symbol_element_count(unit, left_symbol);
    if (element_count == NULL) {
        f2c_diagnostic(context, line, 1, "whole-array assignment requires a known target shape");
        return 1;
    }
    if (has_constructor) {
        const int emitted =
            left_symbol->type == TYPE_CHARACTER
                ? f2c_array_emit_whole_character_assignment(context, unit, left_symbol, right,
                                                            right_symbol, element_count, depth)
                : f2c_array_emit_numeric_constructor(context, unit, left_symbol, right,
                                                     element_count, depth);
        if (!emitted)
            f2c_diagnostic(context, line, 1,
                           "array constructor contains an unsupported value or incompatible "
                           "element type");
        goto cleanup;
    }
    if (left_symbol->type == TYPE_CHARACTER &&
        f2c_array_emit_whole_character_assignment(context, unit, left_symbol, right, right_symbol,
                                                  element_count, depth))
        goto cleanup;
    if (right_symbol != NULL && right_symbol->rank != 0U &&
        right_symbol->type == left_symbol->type) {
        char *right_count = f2c_symbol_element_count(unit, right_symbol);
        f2c_array_indent(&context->output, depth);
        if (left_symbol->type == TYPE_DERIVED && left_symbol->derived_type != NULL &&
            left_symbol->derived_type == right_symbol->derived_type) {
            f2c_buffer_printf(
                &context->output,
                "{ const size_t f2c_whole_count = (size_t)(%s); "
                "if ((size_t)(%s) != f2c_whole_count) abort(); "
                "%s *f2c_whole_temporary = (%s *)calloc("
                "f2c_whole_count == 0U ? 1U : f2c_whole_count, "
                "sizeof(*f2c_whole_temporary)); "
                "if (f2c_whole_temporary == NULL) abort(); "
                "for (size_t i = 0U; i < f2c_whole_count; ++i) "
                "f2c_copy_%s(&f2c_whole_temporary[i], &%s[i]); "
                "f2c_destroy_array_%s(%s, f2c_whole_count, %zuU); "
                "if (f2c_whole_count != 0U) memmove(%s, f2c_whole_temporary, "
                "f2c_whole_count * sizeof(*%s)); free(f2c_whole_temporary); }\n",
                element_count, right_count, f2c_symbol_c_type(left_symbol),
                f2c_symbol_c_type(left_symbol), left_symbol->derived_type->c_name,
                f2c_symbol_c_name(unit, right_symbol), left_symbol->derived_type->c_name,
                f2c_symbol_c_name(unit, left_symbol), left_symbol->rank,
                f2c_symbol_c_name(unit, left_symbol), f2c_symbol_c_name(unit, left_symbol));
        } else if (left_symbol->equivalence_unaligned || right_symbol->equivalence_unaligned) {
            char *right_value =
                right_symbol->equivalence_unaligned
                    ? f2c_emit_unaligned_linear_load(unit, right_symbol, "f2c_whole_index")
                    : NULL;
            char *left_address =
                left_symbol->equivalence_unaligned
                    ? f2c_emit_unaligned_linear_address(unit, left_symbol, "f2c_whole_index")
                    : NULL;
            const char *left_suffix = f2c_unaligned_access_suffix(left_symbol);
            f2c_buffer_printf(
                &context->output,
                "{ const size_t f2c_whole_count = (size_t)(%s); "
                "if ((size_t)(%s) != f2c_whole_count) abort(); "
                "if (f2c_whole_count > SIZE_MAX / sizeof(%s)) abort(); "
                "%s *f2c_whole_temporary = (%s *)malloc("
                "F2C_MAX((size_t)1U, f2c_whole_count) * sizeof(%s)); "
                "if (f2c_whole_temporary == NULL) abort(); "
                "for (size_t f2c_whole_index = 0U; f2c_whole_index < f2c_whole_count; "
                "++f2c_whole_index) f2c_whole_temporary[f2c_whole_index] = %s%s%s; "
                "for (size_t f2c_whole_index = 0U; f2c_whole_index < f2c_whole_count; "
                "++f2c_whole_index) ",
                element_count, right_count, f2c_symbol_c_type(left_symbol),
                f2c_symbol_c_type(left_symbol), f2c_symbol_c_type(left_symbol),
                f2c_symbol_c_type(left_symbol),
                right_value != NULL ? right_value : f2c_symbol_c_name(unit, right_symbol),
                right_value != NULL ? "" : "[f2c_whole_index]", "");
            if (left_address != NULL)
                f2c_buffer_printf(&context->output,
                                  "f2c_unaligned_store_%s(%s, "
                                  "f2c_whole_temporary[f2c_whole_index]); ",
                                  left_suffix, left_address);
            else
                f2c_buffer_printf(&context->output,
                                  "%s[f2c_whole_index] = "
                                  "f2c_whole_temporary[f2c_whole_index]; ",
                                  f2c_symbol_c_name(unit, left_symbol));
            f2c_buffer_append(&context->output, "free(f2c_whole_temporary); }\n");
            free(right_value);
            free(left_address);
        } else {
            f2c_buffer_printf(&context->output,
                              "{ const size_t f2c_whole_count = (size_t)(%s); "
                              "if ((size_t)(%s) != f2c_whole_count) abort(); "
                              "if (f2c_whole_count != 0U) memmove(%s, %s, "
                              "f2c_whole_count * sizeof(*%s)); }\n",
                              element_count, right_count, f2c_symbol_c_name(unit, left_symbol),
                              f2c_symbol_c_name(unit, right_symbol),
                              f2c_symbol_c_name(unit, left_symbol));
        }
        free(right_count);
    } else {
        char *value;
        if (f2c_array_emit_derived_scalar_broadcast(context, unit, left_symbol, right,
                                                    element_count, depth))
            goto cleanup;
        value = f2c_array_emit_expression(unit, right);
        if (value == NULL) {
            f2c_diagnostic(context, line, 1,
                           "whole-array assignment has an unsupported right-hand expression");
            goto cleanup;
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "{ const %s f2c_whole_scalar = (%s)(%s); "
                          "size_t f2c_fill_index; for (f2c_fill_index = 0; ",
                          f2c_symbol_c_type(left_symbol), f2c_symbol_c_type(left_symbol), value);
        if (left_symbol->equivalence_unaligned) {
            char *address = f2c_emit_unaligned_linear_address(unit, left_symbol, "f2c_fill_index");
            f2c_buffer_printf(&context->output,
                              "f2c_fill_index < %s; ++f2c_fill_index) "
                              "f2c_unaligned_store_%s(%s, f2c_whole_scalar); }\n",
                              element_count, f2c_unaligned_access_suffix(left_symbol), address);
            free(address);
        } else {
            char *data = f2c_storage_symbol_data(unit, left_symbol);
            Buffer access = {0};
            if (data == NULL) {
                context->output.failed = 1;
                free(value);
                goto cleanup;
            }
            if (left_symbol->volatile_entity)
                f2c_buffer_printf(&access, "((volatile %s *)(%s))", f2c_symbol_c_type(left_symbol),
                                  data);
            else
                f2c_buffer_append(&access, data);
            f2c_buffer_printf(&context->output,
                              "f2c_fill_index < %s; ++f2c_fill_index) %s[f2c_fill_index] = "
                              "f2c_whole_scalar; }\n",
                              element_count, access.data);
            if (access.failed)
                context->output.failed = 1;
            free(access.data);
            free(data);
        }
        free(value);
    }
cleanup:
    free(element_count);
    return 1;
}
