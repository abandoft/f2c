#include "codegen/array/private.h"

#include <stdlib.h>

int f2c_array_function_result_call(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL && expression->rank != 0U &&
           expression->lowered_c == NULL && expression->intrinsic == F2C_INTRINSIC_NONE &&
           f2c_expression_has_descriptor_result(expression);
}

static void append_shape_validation(Buffer *prelude, const F2cExpr *expression,
                                    const char *descriptor, const char *storage, int depth) {
    size_t dimension;
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude,
                      "if (!f2c_descriptor_bridge_valid(&%s, %zuU, sizeof(%s))) abort();\n",
                      descriptor, expression->rank, f2c_expression_c_type(expression));
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
    f2c_buffer_printf(prelude, "if (%s_count != 0U && %s.data == NULL) abort();\n", storage,
                      descriptor);
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
    f2c_buffer_printf(prelude, "if (%s.deallocatable && !%s_contiguous) abort();\n", descriptor,
                      storage);
}

static void append_nonowning_copy(Buffer *prelude, const F2cExpr *expression,
                                  const char *descriptor, const char *storage, int depth) {
    const char *c_type = f2c_expression_c_type(expression);
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
                          "ptrdiff_t f2c_offset = f2c_descriptor_linear_offset(&%s, %s_index); "
                          "if (%s.character_length != 0U) memmove(%s + %s_index * "
                          "%s.character_length, (const char *)%s.data + "
                          "f2c_descriptor_stride_multiply(f2c_offset, "
                          "(ptrdiff_t)%s.character_length), %s.character_length); }\n",
                          storage, storage, storage, storage, descriptor, storage, descriptor,
                          storage, storage, descriptor, descriptor, descriptor, descriptor);
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
                "ptrdiff_t f2c_offset = f2c_descriptor_linear_offset(&%s, %s_index); "
                "f2c_clone_%s(&%s[%s_index], &((const %s *)%s.data)[f2c_offset]); }\n",
                storage, storage, storage, storage, descriptor, storage,
                expression->derived_type->c_name, storage, storage, c_type, descriptor);
        else
            f2c_buffer_printf(prelude,
                              "for (size_t %s_index = 0U; %s_index < %s_count; ++%s_index) { "
                              "ptrdiff_t f2c_offset = f2c_descriptor_linear_offset(&%s, %s_index); "
                              "%s[%s_index] = ((const %s *)%s.data)[f2c_offset]; }\n",
                              storage, storage, storage, storage, descriptor, storage, storage,
                              storage, c_type, descriptor);
    }
}

int f2c_array_materialize_function_result(Unit *unit, F2cExpr *expression, size_t identifier,
                                          const char *role, size_t *temporary, Buffer *prelude,
                                          F2cArrayCleanupList *cleanup, int depth) {
    Buffer storage = {0};
    Buffer descriptor = {0};
    char *call;
    if (!f2c_array_function_result_call(expression))
        return 1;
    if (unit == NULL || role == NULL || temporary == NULL || prelude == NULL || cleanup == NULL ||
        expression->rank > F2C_MAX_RANK || expression->type == TYPE_UNKNOWN ||
        (expression->type == TYPE_DERIVED && expression->derived_type == NULL))
        return 0;
    if (!f2c_array_owned_temporary_valid(unit, expression,
                                         F2C_OWNED_TEMPORARY_ARRAY_FUNCTION_RESULT))
        return 0;
    call = f2c_array_emit_expression(unit, expression);
    if (call == NULL)
        return 0;
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
    f2c_buffer_printf(prelude, "f2c_descriptor %s = %s;\n", descriptor.data, call);
    append_shape_validation(prelude, expression, descriptor.data, storage.data, depth);
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude, "%s *%s = NULL;\n", f2c_expression_c_type(expression), storage.data);
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude, "if (%s.deallocatable) %s = (%s *)%s.data;\n", descriptor.data,
                      storage.data, f2c_expression_c_type(expression), descriptor.data);
    f2c_array_indent(prelude, depth);
    f2c_buffer_append(prelude, "else {\n");
    append_nonowning_copy(prelude, expression, descriptor.data, storage.data, depth + 1);
    f2c_array_indent(prelude, depth);
    f2c_buffer_append(prelude, "}\n");
    expression->lowered_c = f2c_buffer_take(&storage);
    expression->lowered_array_temporary = 1;
    if (expression->type == TYPE_CHARACTER) {
        Buffer length = {0};
        f2c_buffer_printf(&length, "%s.character_length", descriptor.data);
        expression->lowered_character_length_c = f2c_buffer_take(&length);
        if (expression->lowered_character_length_c == NULL) {
            free(call);
            free(descriptor.data);
            return 0;
        }
    }
    if (!f2c_array_cleanup_append(unit, cleanup, expression, depth)) {
        free(call);
        free(descriptor.data);
        return 0;
    }
    free(call);
    free(descriptor.data);
    return expression->lowered_c != NULL;
}
