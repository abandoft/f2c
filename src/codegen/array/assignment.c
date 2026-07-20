#include "codegen/array/private.h"

#include "codegen/value/private.h"

#include <stdio.h>
#include <stdlib.h>

static void free_extents(char **extents, size_t rank) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension)
        free(extents[dimension]);
}

static int elemental_assignment_type_matches(const Symbol *target, const F2cExpr *right) {
    if (target->type != right->type || target->kind != right->type_kind)
        return 0;
    return target->type != TYPE_DERIVED ||
           (target->derived_type != NULL && target->derived_type == right->derived_type);
}

int f2c_array_emit_derived_scalar_broadcast(Context *context, Unit *unit, Symbol *target,
                                            const F2cExpr *right, const char *element_count,
                                            int depth) {
    const size_t output_start = context != NULL ? context->output.length : 0U;
    const char *type_name;
    const char *target_name;
    if (context == NULL || unit == NULL || target == NULL || right == NULL ||
        element_count == NULL || target->type != TYPE_DERIVED || target->derived_type == NULL ||
        target->rank == 0U || right->type != TYPE_DERIVED || right->derived_type == NULL ||
        right->rank != 0U || target->derived_type != right->derived_type)
        return 0;
    type_name = target->derived_type->c_name;
    target_name = f2c_symbol_c_name(unit, target);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_whole_count = (size_t)(%s);\n",
                      element_count);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s f2c_whole_source;\n", type_name);
    if (!f2c_emit_derived_clone_expression(&context->output, unit, right, "f2c_whole_source",
                                           "array_broadcast", 0U, depth + 1)) {
        if (context->output.data != NULL && output_start <= context->output.length) {
            context->output.length = output_start;
            context->output.data[output_start] = '\0';
        }
        return 0;
    }
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "f2c_destroy_array_%s(%s, f2c_whole_count, %zuU);\n",
                      type_name, target_name, target->rank);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "for (size_t f2c_whole_index = 0U; f2c_whole_index < "
                                        "f2c_whole_count; ++f2c_whole_index)\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "f2c_clone_%s(&%s[f2c_whole_index], &f2c_whole_source);\n",
                      type_name, target_name);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_whole_source);\n", type_name);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    return 1;
}

int f2c_array_emit_elemental_assignment(Context *context, Unit *unit, Symbol *target,
                                        const F2cExpr *right, size_t line, int depth) {
    char *target_extents[F2C_MAX_RANK] = {0};
    char *right_extents[F2C_MAX_RANK] = {0};
    char ordinal_storage[F2C_MAX_RANK][48];
    const char *ordinals[F2C_MAX_RANK] = {0};
    F2cExpr *element = NULL;
    F2cExpr *prepared_right = NULL;
    char *value = NULL;
    char *character_length = NULL;
    char *old_element_count = NULL;
    const size_t output_start = context != NULL ? context->output.length : 0U;
    size_t dimension;
    size_t loop;
    size_t temporary = 0U;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    int emitted_depth;
    if (context == NULL || unit == NULL || target == NULL || right == NULL || right->rank == 0U ||
        target->rank == 0U || right->rank != target->rank || right->kind == F2C_EXPR_NAME ||
        right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR)
        return 0;
    if (!elemental_assignment_type_matches(target, right)) {
        f2c_diagnostic(context, line, 1,
                       "array expression assignment requires matching type, kind, and derived "
                       "type");
        return 1;
    }
    prepared_right = f2c_array_clone_expression(right);
    if (prepared_right == NULL ||
        !f2c_array_materialize_constructors(context, unit, prepared_right, line, "assignment",
                                            &temporary, &prelude, &cleanup, depth + 1))
        goto unsupported;
    right = prepared_right;
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        right_extents[dimension] = f2c_array_expression_extent(unit, right, dimension);
        target_extents[dimension] =
            target->allocatable
                ? (right_extents[dimension] != NULL ? f2c_strdup(right_extents[dimension]) : NULL)
                : f2c_symbol_dimension_extent(unit, target, dimension);
        (void)snprintf(ordinal_storage[dimension], sizeof(ordinal_storage[dimension]),
                       "f2c_element_ordinal_%zu", dimension);
        ordinals[dimension] = ordinal_storage[dimension];
        if (target_extents[dimension] == NULL || right_extents[dimension] == NULL)
            goto unsupported;
    }
    element = f2c_array_element_expression(unit, right, right->rank, ordinals);
    value = element != NULL && target->type != TYPE_DERIVED
                ? f2c_array_emit_expression(unit, element)
                : NULL;
    if (element == NULL || (target->type != TYPE_DERIVED && value == NULL))
        goto unsupported;
    if (target->type == TYPE_CHARACTER) {
        character_length = target->allocatable && target->deferred_character
                               ? f2c_character_length_expression(unit, element)
                               : f2c_symbol_character_length(unit, target);
        if (character_length == NULL)
            goto unsupported;
    }
    if (target->allocatable && target->type == TYPE_DERIVED) {
        old_element_count = f2c_symbol_element_count(unit, target);
        if (old_element_count == NULL)
            goto unsupported;
    }

    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emitted_depth = depth + 1;
    f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "const size_t f2c_element_extent_%zu = (size_t)(%s);\n",
                          dimension, target_extents[dimension]);
        if (!target->allocatable) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != f2c_element_extent_%zu) abort();\n",
                              right_extents[dimension], dimension);
        }
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output,
                      "const size_t f2c_element_count = f2c_inquiry_size(%zuU, "
                      "(const size_t[]){",
                      target->rank);
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        f2c_buffer_printf(&context->output, "%sf2c_element_extent_%zu", dimension == 0U ? "" : ", ",
                          dimension);
    }
    f2c_buffer_append(&context->output, "});\n");
    if (target->pointer) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "if (%s == NULL) abort();\n",
                          f2c_symbol_c_name(unit, target));
    }
    if (target->type == TYPE_CHARACTER) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "const size_t f2c_element_length = (size_t)(%s);\n",
                          character_length);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "if (f2c_element_length != 0U && f2c_element_count > "
                                            "SIZE_MAX / f2c_element_length) abort();\n");
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "const size_t f2c_element_bytes = "
                                            "f2c_element_count * f2c_element_length;\n");
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "char *f2c_element_values = (char *)malloc("
                                            "f2c_element_bytes == 0U ? 1U : f2c_element_bytes);\n");
    } else if (target->type == TYPE_DERIVED) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_element_count > SIZE_MAX / sizeof(%s)) abort();\n",
                          target->derived_type->c_name);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "%s *f2c_element_values = (%s *)calloc("
                          "f2c_element_count == 0U ? 1U : f2c_element_count, "
                          "sizeof(*f2c_element_values));\n",
                          target->derived_type->c_name, target->derived_type->c_name);
    } else {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_element_count > SIZE_MAX / sizeof(%s)) abort();\n",
                          f2c_symbol_c_type(target));
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "%s *f2c_element_values = (%s *)malloc("
                          "(f2c_element_count == 0U ? 1U : f2c_element_count) * "
                          "sizeof(*f2c_element_values));\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target));
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "if (f2c_element_values == NULL) abort();\n");
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "size_t f2c_element_linear = 0U;\n");
    for (loop = target->rank; loop != 0U; --loop) {
        dimension = loop - 1U;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_element_ordinal_%zu = 0U; "
                          "f2c_element_ordinal_%zu < f2c_element_extent_%zu; "
                          "++f2c_element_ordinal_%zu) {\n",
                          dimension, dimension, dimension, dimension);
        ++emitted_depth;
    }
    if (target->type == TYPE_CHARACTER) {
        if (!f2c_emit_character_storage_assignment(
                context, unit, "f2c_element_values + f2c_element_linear * f2c_element_length",
                "f2c_element_length", element, value, emitted_depth))
            goto emission_failed;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_element_linear;\n");
    } else if (target->type == TYPE_DERIVED) {
        if (!f2c_emit_derived_clone_expression(&context->output, unit, element,
                                               "f2c_element_values[f2c_element_linear]",
                                               "array_element", line, emitted_depth))
            goto emission_failed;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_element_linear;\n");
    } else {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_element_values[f2c_element_linear++] = %s;\n",
                          value);
    }
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        --emitted_depth;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_array_indent(&context->output, emitted_depth);
    if (target->allocatable) {
        const char *target_name = f2c_symbol_c_name(unit, target);
        if (target->type == TYPE_DERIVED) {
            f2c_buffer_printf(&context->output,
                              "if (%s != NULL) f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              target_name, target->derived_type->c_name, target_name,
                              old_element_count, target->rank);
            f2c_array_indent(&context->output, emitted_depth);
        }
        f2c_buffer_printf(&context->output, "free(%s);\n", target_name);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "%s = f2c_element_values;\n", target_name);
        if (target->type == TYPE_CHARACTER && target->deferred_character) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_element_length;\n",
                              target_name);
        }
        for (dimension = 0U; dimension < target->rank; ++dimension) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = 1; %s_extent_%zu = "
                              "(int32_t)f2c_element_extent_%zu;\n",
                              target_name, dimension + 1U, target_name, dimension + 1U, dimension);
        }
    } else if (target->type == TYPE_CHARACTER)
        f2c_buffer_printf(&context->output,
                          "if (f2c_element_bytes != 0U) memmove(%s, f2c_element_values, "
                          "f2c_element_bytes);\n",
                          f2c_symbol_c_name(unit, target));
    else if (target->type == TYPE_DERIVED) {
        f2c_buffer_printf(&context->output, "f2c_destroy_array_%s(%s, f2c_element_count, %zuU);\n",
                          target->derived_type->c_name, f2c_symbol_c_name(unit, target),
                          target->rank);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_element_count != 0U) memmove(%s, f2c_element_values, "
                          "f2c_element_count * sizeof(*f2c_element_values));\n",
                          f2c_symbol_c_name(unit, target));
    } else
        f2c_buffer_printf(&context->output,
                          "if (f2c_element_count != 0U) memmove(%s, f2c_element_values, "
                          "f2c_element_count * sizeof(*f2c_element_values));\n",
                          f2c_symbol_c_name(unit, target));
    if (!target->allocatable) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "free(f2c_element_values);\n");
    }
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(value);
    free(character_length);
    free(old_element_count);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_right);
    f2c_expr_free(element);
    free_extents(target_extents, target->rank);
    free_extents(right_extents, target->rank);
    return 1;

unsupported:
    free(value);
    free(character_length);
    free(old_element_count);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_right);
    f2c_expr_free(element);
    free_extents(target_extents, target->rank);
    free_extents(right_extents, target->rank);
    f2c_diagnostic(context, line, 1,
                   "array expression assignment cannot derive an element or runtime extent");
    return 1;

emission_failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(value);
    free(character_length);
    free(old_element_count);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_right);
    f2c_expr_free(element);
    free_extents(target_extents, target->rank);
    free_extents(right_extents, target->rank);
    f2c_diagnostic(context, line, 1,
                   "CHARACTER array expression assignment cannot lower an element value");
    return 1;
}
