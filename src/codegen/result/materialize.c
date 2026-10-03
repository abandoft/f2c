#include "codegen/call/private.h"
#include "codegen/result/private.h"

#include "codegen/lowering/private.h"

#include <stdlib.h>

int f2c_result_requires_materialization(const Unit *unit, const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           f2c_lowering_code(unit, expression) == NULL &&
           expression->intrinsic == F2C_INTRINSIC_NONE &&
           f2c_expression_has_descriptor_result(expression);
}

static void append_shape_validation(Buffer *prelude, const F2cExpr *expression,
                                    const char *descriptor, const char *storage, int depth) {
    size_t dimension;
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude,
                      "if (!f2c_descriptor_bridge_valid(&%s, %zuU, sizeof(%s))) abort();\n",
                      descriptor, expression->rank, f2c_expression_c_type(expression));
    if (expression->rank == 0U) {
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "const size_t %s_count = 1U;\n", storage);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "(void)%s_count;\n", storage);
        if (expression->result_use != F2C_FUNCTION_RESULT_REFERENCE) {
            f2c_array_indent(prelude, depth);
            f2c_buffer_printf(prelude, "if (%s.data == NULL) abort();\n", descriptor);
        }
        if (f2c_result_kind_owns_storage(expression->result_kind)) {
            f2c_array_indent(prelude, depth);
            f2c_buffer_printf(prelude, "if (!%s.deallocatable) abort();\n", descriptor);
        }
        return;
    }
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "const size_t %s_extent_%zu = (size_t)%s.extent[%zu];\n",
                          storage, dimension + 1U, descriptor, dimension);
    }
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude,
                      "const size_t %s_count = f2c_inquiry_size(%zuU, "
                      "(const size_t[]){",
                      storage, expression->rank);
    for (dimension = 0U; dimension < expression->rank; ++dimension)
        f2c_buffer_printf(prelude, "%s%s_extent_%zu", dimension == 0U ? "" : ", ", storage,
                          dimension + 1U);
    f2c_buffer_append(prelude, "});\n");
    f2c_array_indent(prelude, depth);
    if (expression->result_use != F2C_FUNCTION_RESULT_REFERENCE)
        f2c_buffer_printf(prelude, "if (%s_count != 0U && %s.data == NULL) abort();\n", storage,
                          descriptor);
    else
        f2c_buffer_printf(prelude, "(void)%s_count;\n", storage);
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude,
                      "const bool %s_contiguous = f2c_descriptor_is_contiguous(%zuU, "
                      "(const size_t[]){",
                      storage, expression->rank);
    for (dimension = 0U; dimension < expression->rank; ++dimension)
        f2c_buffer_printf(prelude, "%s%s_extent_%zu", dimension == 0U ? "" : ", ", storage,
                          dimension + 1U);
    f2c_buffer_printf(prelude, "}, %s.stride);\n", descriptor);
    f2c_array_indent(prelude, depth);
    if (f2c_result_kind_owns_storage(expression->result_kind))
        f2c_buffer_printf(prelude, "if (!%s.deallocatable || !%s_contiguous) abort();\n",
                          descriptor, storage);
    else
        f2c_buffer_printf(prelude, "(void)%s_contiguous;\n", storage);
}

static void append_nonowning_copy(Buffer *prelude, const F2cExpr *expression,
                                  const char *descriptor, const char *storage, int depth) {
    const char *c_type = f2c_expression_c_type(expression);
    Buffer offset = {0};
    if (expression->rank == 0U)
        f2c_buffer_append(&offset, "0");
    else
        f2c_buffer_printf(&offset, "f2c_descriptor_linear_offset(&%s, %s_index)", descriptor,
                          storage);
    f2c_array_indent(prelude, depth);
    if (expression->type == TYPE_CHARACTER) {
        f2c_buffer_printf(prelude,
                          "if (%s.character_length != 0U && %s_count > "
                          "SIZE_MAX / %s.character_length) abort();\n",
                          descriptor, storage, descriptor);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude,
                          "%s = (char *)malloc(%s_count == 0U || %s.character_length == 0U "
                          "? 1U : %s_count * %s.character_length);\n",
                          storage, storage, descriptor, storage, descriptor);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s == NULL) abort();\n", storage);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s.character_length > (size_t)PTRDIFF_MAX) abort();\n",
                          descriptor);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude,
                          "for (size_t %s_index = 0U; %s_index < %s_count; ++%s_index) { "
                          "ptrdiff_t f2c_offset = %s; "
                          "f2c_descriptor_read_record(%s + %s_index * "
                          "%s.character_length, &%s, "
                          "f2c_descriptor_stride_multiply(f2c_offset, "
                          "(ptrdiff_t)%s.character_length), %s.character_length); }\n",
                          storage, storage, storage, storage, offset.data, storage, storage,
                          descriptor, descriptor, descriptor, descriptor);
    } else {
        f2c_buffer_printf(prelude, "if (%s_count > SIZE_MAX / sizeof(%s)) abort();\n", storage,
                          c_type);
        f2c_array_indent(prelude, depth);
        if (expression->type == TYPE_DERIVED)
            f2c_buffer_printf(prelude,
                              "%s = (%s *)calloc(%s_count == 0U ? 1U : %s_count, sizeof(%s));\n",
                              storage, c_type, storage, storage, c_type);
        else
            f2c_buffer_printf(prelude,
                              "%s = (%s *)malloc((%s_count == 0U ? 1U : %s_count) * sizeof(%s));\n",
                              storage, c_type, storage, storage, c_type);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s == NULL) abort();\n", storage);
        f2c_array_indent(prelude, depth);
        if (expression->type == TYPE_DERIVED)
            f2c_buffer_printf(
                prelude,
                "for (size_t %s_index = 0U; %s_index < %s_count; ++%s_index) { "
                "ptrdiff_t f2c_offset = %s; %s f2c_snapshot; "
                "f2c_descriptor_read_record(&f2c_snapshot, &%s, "
                "f2c_descriptor_stride_multiply(f2c_offset, (ptrdiff_t)sizeof(%s)), sizeof(%s)); "
                "f2c_clone_%s(&%s[%s_index], &f2c_snapshot); }\n",
                storage, storage, storage, storage, offset.data, c_type, descriptor, c_type, c_type,
                expression->derived_type->c_name, storage, storage);
        else
            f2c_buffer_printf(prelude,
                              "for (size_t %s_index = 0U; %s_index < %s_count; ++%s_index) { "
                              "ptrdiff_t f2c_offset = %s; "
                              "f2c_descriptor_read_record(&%s[%s_index], &%s, "
                              "f2c_descriptor_stride_multiply(f2c_offset, (ptrdiff_t)sizeof(%s)), "
                              "sizeof(%s)); }\n",
                              storage, storage, storage, storage, offset.data, storage, storage,
                              descriptor, c_type, c_type);
    }
    free(offset.data);
}

int f2c_result_materialize(Unit *unit, F2cExpr *expression, size_t identifier, const char *role,
                           size_t *temporary, Buffer *prelude, F2cArrayCleanupList *cleanup,
                           int depth) {
    Buffer storage = {0};
    Buffer descriptor = {0};
    char *call = NULL;
    if (!f2c_result_requires_materialization(unit, expression))
        return 1;
    if (unit == NULL || role == NULL || temporary == NULL || prelude == NULL || cleanup == NULL ||
        expression->rank > F2C_MAX_RANK || expression->type == TYPE_UNKNOWN ||
        (expression->type == TYPE_DERIVED && expression->derived_type == NULL))
        return 0;
    if (!f2c_array_owned_temporary_valid(unit, expression, F2C_OWNED_TEMPORARY_FUNCTION_RESULT))
        return 0;
    const int state_parameters = f2c_call_has_object_state_parameters(expression);
    if (!state_parameters) {
        call = f2c_array_emit_expression(unit, expression);
        if (call == NULL)
            return 0;
    }
    f2c_buffer_printf(&storage, "f2c_array_%s_function_%zu_%zu", role, identifier,
                      expression->owned_temporary_index);
    f2c_buffer_printf(&descriptor, "%s_descriptor", storage.data != NULL ? storage.data : "");
    if (storage.data == NULL || descriptor.data == NULL) {
        free(call);
        free(storage.data);
        free(descriptor.data);
        return 0;
    }
    f2c_array_indent(prelude, depth);
    if (state_parameters) {
        f2c_buffer_printf(prelude, "f2c_descriptor %s;\n", descriptor.data);
        if (!f2c_call_emit_function_value(prelude, unit, expression, descriptor.data, depth)) {
            free(storage.data);
            free(descriptor.data);
            return 0;
        }
    } else {
        f2c_buffer_printf(prelude, "f2c_descriptor %s = %s;\n", descriptor.data, call);
    }
    append_shape_validation(prelude, expression, descriptor.data, storage.data, depth);
    if (f2c_expression_temporary_release_kind(expression) == F2C_TEMPORARY_STACK_VALUE) {
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "%s %s;\n", f2c_expression_c_type(expression), storage.data);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "f2c_descriptor_read_record(&%s, &%s, 0, sizeof(%s));\n",
                          storage.data, descriptor.data, storage.data);
        const int success =
            f2c_lowering_copy_result_descriptor(unit, expression, descriptor.data) &&
            f2c_lowering_take_code(unit, expression, f2c_buffer_take(&storage));
        free(storage.data);
        free(descriptor.data);
        free(call);
        return success;
    }
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude, "%s *%s = NULL;\n", f2c_expression_c_type(expression), storage.data);
    const int reference = expression->result_use == F2C_FUNCTION_RESULT_REFERENCE;
    if (f2c_result_kind_owns_storage(expression->result_kind) || reference) {
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "%s = (%s *)%s.data;\n", storage.data,
                          f2c_expression_c_type(expression), descriptor.data);
    } else {
        append_nonowning_copy(prelude, expression, descriptor.data, storage.data, depth);
    }
    if (reference) {
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "(void)%s;\n", storage.data);
    }
    Buffer value = {0};
    if (expression->rank == 0U && expression->type != TYPE_CHARACTER)
        f2c_buffer_printf(&value, "(*%s)", storage.data);
    else
        f2c_buffer_append(&value, storage.data);
    const int lowered =
        f2c_lowering_copy_result_descriptor(unit, expression, descriptor.data) &&
        (reference || f2c_lowering_copy_owned_storage(unit, expression, storage.data)) &&
        f2c_lowering_take_code(unit, expression, f2c_buffer_take(&value)) &&
        f2c_lowering_set_array_temporary(unit, expression, expression->rank != 0U);
    free(storage.data);
    free(value.data);
    if (!lowered) {
        free(call);
        free(descriptor.data);
        return 0;
    }
    if (expression->type == TYPE_CHARACTER) {
        Buffer length = {0};
        f2c_buffer_printf(&length, "%s.character_length", descriptor.data);
        if (!f2c_lowering_take_character_length(unit, expression, f2c_buffer_take(&length))) {
            free(call);
            free(descriptor.data);
            return 0;
        }
    }
    if (!reference && !f2c_array_cleanup_append(unit, cleanup, expression, depth)) {
        free(call);
        free(descriptor.data);
        return 0;
    }
    free(call);
    free(descriptor.data);
    return f2c_lowering_code(unit, expression) != NULL;
}
