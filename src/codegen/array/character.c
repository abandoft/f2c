#include "codegen/array/private.h"

#include <stdlib.h>

int f2c_array_emit_whole_character_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                              const F2cExpr *right, Symbol *right_symbol,
                                              const char *element_count, int depth) {
    const size_t output_start = context->output.length;
    const int has_constructor = right != NULL && right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR;
    /* F2018 10.2.1.3(3): change deferred length but preserve array bounds. */
    const int scalar_length_reallocation = left_symbol != NULL && left_symbol->allocatable &&
                                           left_symbol->deferred_character && right != NULL &&
                                           right->rank == 0U;
    char *left_length = NULL;
    char *right_length = NULL;
    char *right_count = NULL;
    char *scalar_code = NULL;
    int result = 0;
    if (left_symbol == NULL || left_symbol->type != TYPE_CHARACTER || element_count == NULL)
        return 0;
    if (!has_constructor && right_symbol != NULL && right_symbol->rank != 0U &&
        right_symbol->type == TYPE_CHARACTER) {
        right_length = f2c_symbol_character_length(unit, right_symbol);
        right_count = f2c_symbol_element_count(unit, right_symbol);
        if (right_length == NULL || right_count == NULL)
            goto cleanup;
    } else if (!has_constructor && right != NULL && right->rank == 0U &&
               right->type == TYPE_CHARACTER) {
        scalar_code = f2c_array_emit_expression(unit, right);
        if (scalar_code == NULL)
            goto cleanup;
    } else if (!has_constructor) {
        goto cleanup;
    }
    left_length = scalar_length_reallocation ? f2c_character_length_expression(unit, right)
                                             : f2c_symbol_character_length(unit, left_symbol);
    if (left_length == NULL)
        goto cleanup;

    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_whole_count = (size_t)(%s);\n",
                      element_count);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_whole_length = (size_t)(%s);\n",
                      left_length);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "const size_t f2c_whole_bytes = f2c_size_multiply_checked("
                                        "f2c_whole_count, f2c_whole_length);\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "char *f2c_whole_values = f2c_whole_count == 0U ? NULL : "
                      "(char *)malloc(f2c_whole_length == 0U ? 1U : f2c_whole_bytes);\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_whole_count != 0U && f2c_whole_values == NULL) abort();\n");
    if (has_constructor) {
        if (!f2c_array_emit_fixed_character_constructor_values(
                context, unit, left_symbol, right, "f2c_whole_values", "f2c_whole_count",
                "f2c_whole_length", depth + 1))
            goto cleanup;
    } else if (right_symbol != NULL && right_symbol->rank != 0U) {
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_whole_source_count = (size_t)(%s);\n",
                          right_count);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_whole_source_length = (size_t)(%s);\n", right_length);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (f2c_whole_source_count != f2c_whole_count) abort();\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "const size_t f2c_whole_copy_length = "
                                            "f2c_character_copy_length(f2c_whole_length, "
                                            "f2c_whole_source_length);\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_whole_index = 0U; "
                          "f2c_whole_index < f2c_whole_count; ++f2c_whole_index) {\n");
        f2c_array_indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "memmove(f2c_whole_values + f2c_whole_index * f2c_whole_length, "
                          "%s + f2c_whole_index * f2c_whole_source_length, "
                          "f2c_whole_copy_length);\n",
                          f2c_symbol_c_name(unit, right_symbol));
        f2c_array_indent(&context->output, depth + 2);
        f2c_buffer_append(&context->output,
                          "if (f2c_whole_length > f2c_whole_copy_length) "
                          "memset(f2c_whole_values + f2c_whole_index * f2c_whole_length + "
                          "f2c_whole_copy_length, ' ', "
                          "f2c_whole_length - f2c_whole_copy_length);\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "char *f2c_whole_scalar = (char *)malloc("
                                            "f2c_whole_length == 0U ? 1U : f2c_whole_length);\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_whole_scalar == NULL) abort();\n");
        if (!f2c_emit_character_storage_assignment(context, unit, "f2c_whole_scalar",
                                                   "f2c_whole_length", right, scalar_code,
                                                   depth + 1))
            goto cleanup;
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_whole_index = 0U; "
                          "f2c_whole_index < f2c_whole_count; ++f2c_whole_index) "
                          "if (f2c_whole_length != 0U) "
                          "memmove(f2c_whole_values + f2c_whole_index * f2c_whole_length, "
                          "f2c_whole_scalar, f2c_whole_length);\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "free(f2c_whole_scalar);\n");
    }
    if (scalar_length_reallocation) {
        /* The scalar and full broadcast have been snapshotted before freeing
         * potentially overlapping source storage; equal lengths keep aliases. */
        const char *name = f2c_symbol_c_name(unit, left_symbol);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (f2c_char_len_%s != f2c_whole_length) {\n", name);
        f2c_array_indent(&context->output, depth + 2);
        f2c_buffer_append(&context->output,
                          "if (f2c_whole_values == NULL) { f2c_whole_values = "
                          "(char *)malloc(1U); if (f2c_whole_values == NULL) abort(); }\n");
        f2c_array_indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "free(%s); %s = f2c_whole_values; f2c_whole_values = NULL;\n"
                          "f2c_char_len_%s = f2c_whole_length;\n",
                          name, name, name);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_whole_values != NULL && f2c_whole_bytes != 0U) "
                      "memmove(%s, f2c_whole_values, "
                      "f2c_whole_bytes);\n",
                      f2c_symbol_c_name(unit, left_symbol));
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_whole_values);\n");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    if (!result && context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(left_length);
    free(right_length);
    free(right_count);
    free(scalar_code);
    return result;
}
