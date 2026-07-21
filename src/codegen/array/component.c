#include "codegen/array/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/value/private.h"

#include <stdio.h>
#include <stdlib.h>

static void free_dimensions(char **values, size_t rank) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension)
        free(values[dimension]);
}

static int numeric_type(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
           type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
}

static void append_loops(Buffer *output, size_t rank, int *depth) {
    size_t loop;
    for (loop = rank; loop != 0U; --loop) {
        const size_t dimension = loop - 1U;
        f2c_array_indent(output, *depth);
        f2c_buffer_printf(output,
                          "for (size_t f2c_component_ordinal_%zu = 0U; "
                          "f2c_component_ordinal_%zu < f2c_component_extent_%zu; "
                          "++f2c_component_ordinal_%zu) {\n",
                          dimension, dimension, dimension, dimension);
        ++*depth;
    }
}

static void close_loops(Buffer *output, size_t rank, int *depth) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension) {
        --*depth;
        f2c_array_indent(output, *depth);
        f2c_buffer_append(output, "}\n");
    }
}

static int emit_shape(Buffer *output, Unit *unit, const F2cExpr *target, const F2cExpr *right,
                      int depth, char **target_extents, char **right_extents) {
    size_t dimension;
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        target_extents[dimension] = target->child_count == 1U
                                        ? f2c_descriptor_dimension_extent(unit, target, dimension)
                                        : f2c_array_expression_extent(unit, target, dimension);
        if (right->rank != 0U)
            right_extents[dimension] = f2c_array_expression_extent(unit, right, dimension);
        if (target_extents[dimension] == NULL ||
            (right->rank != 0U && right_extents[dimension] == NULL))
            return 0;
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output, "const size_t f2c_component_extent_%zu = (size_t)(%s);\n",
                          dimension, target_extents[dimension]);
        if (right->rank != 0U) {
            f2c_array_indent(output, depth);
            f2c_buffer_printf(output, "if ((size_t)(%s) != f2c_component_extent_%zu) abort();\n",
                              right_extents[dimension], dimension);
        }
    }
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output,
                      "const size_t f2c_component_count = f2c_inquiry_size(%zuU, "
                      "(const size_t[]){",
                      target->rank);
    for (dimension = 0U; dimension < target->rank; ++dimension)
        f2c_buffer_printf(output, "%sf2c_component_extent_%zu", dimension == 0U ? "" : ", ",
                          dimension);
    f2c_buffer_append(output, "});\n");
    return 1;
}

static int emit_temporary_declaration(Context *context, Unit *unit, const F2cExpr *target,
                                      char **character_length, int depth) {
    const Symbol *symbol = target->symbol;
    f2c_array_indent(&context->output, depth);
    if (symbol->type == TYPE_CHARACTER) {
        *character_length = f2c_character_length_expression(unit, target);
        if (*character_length == NULL)
            return 0;
        f2c_buffer_printf(&context->output, "const size_t f2c_component_length = (size_t)(%s);\n",
                          *character_length);
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "if (f2c_component_length != 0U && f2c_component_count > "
                          "SIZE_MAX / f2c_component_length) abort();\n");
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "char *f2c_component_values = (char *)malloc("
                          "f2c_component_count == 0U || f2c_component_length == 0U ? 1U : "
                          "f2c_component_count * f2c_component_length);\n");
    } else {
        f2c_buffer_printf(&context->output,
                          "if (f2c_component_count > SIZE_MAX / sizeof(%s)) abort();\n",
                          f2c_symbol_c_type(symbol));
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "%s *f2c_component_values = (%s *)calloc("
                          "f2c_component_count == 0U ? 1U : f2c_component_count, "
                          "sizeof(*f2c_component_values));\n",
                          f2c_symbol_c_type(symbol), f2c_symbol_c_type(symbol));
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_component_values == NULL) abort();\n");
    return 1;
}

int f2c_array_emit_component_assignment(Context *context, Unit *unit, const F2cExpr *target,
                                        const F2cExpr *right, size_t line, int depth) {
    const Symbol *symbol = target != NULL ? target->symbol : NULL;
    const size_t output_start = context != NULL ? context->output.length : 0U;
    char *target_extents[F2C_MAX_RANK] = {0};
    char *right_extents[F2C_MAX_RANK] = {0};
    char ordinal_names[F2C_MAX_RANK][64];
    const char *ordinals[F2C_MAX_RANK] = {0};
    F2cExpr *prepared_right = NULL;
    F2cExpr *right_element = NULL;
    F2cExpr *left_element = NULL;
    char *right_code = NULL;
    char *left_code = NULL;
    char *storage = NULL;
    char *character_length = NULL;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    size_t temporary = 0U;
    size_t dimension;
    int emitted_depth;
    int result = 0;
    if (context == NULL || unit == NULL || target == NULL || right == NULL || symbol == NULL ||
        target->kind != F2C_EXPR_COMPONENT || target->child_count == 0U || target->rank == 0U)
        return 0;
    if (symbol->allocatable && target->child_count == 1U &&
        right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR)
        return 0;
    if (right->rank != 0U && right->rank != target->rank) {
        f2c_diagnostic(context, line, 1, "component array assignment requires conformable ranks");
        return 1;
    }
    if ((!numeric_type(symbol->type) || !numeric_type(right->type)) &&
        (symbol->type != right->type || symbol->kind != right->type_kind ||
         (symbol->type == TYPE_DERIVED && symbol->derived_type != right->derived_type))) {
        f2c_diagnostic(context, line, 1,
                       "component array assignment requires compatible type and kind");
        return 1;
    }
    prepared_right = f2c_array_clone_expression(right);
    if (prepared_right == NULL ||
        !f2c_array_materialize_constructors(context, unit, prepared_right, line, "component",
                                            &temporary, &prelude, &cleanup, depth + 1))
        goto unsupported;
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        (void)snprintf(ordinal_names[dimension], sizeof(ordinal_names[dimension]),
                       "f2c_component_ordinal_%zu", dimension);
        ordinals[dimension] = ordinal_names[dimension];
    }
    right_element = f2c_array_element_expression(unit, prepared_right, target->rank, ordinals);
    left_element = f2c_array_element_expression(unit, target, target->rank, ordinals);
    if (right_element == NULL || left_element == NULL)
        goto unsupported;
    if (symbol->type != TYPE_DERIVED)
        right_code = f2c_array_emit_expression(unit, right_element);
    left_code = f2c_array_emit_expression(unit, left_element);
    storage = f2c_descriptor_storage_designator(unit, target);
    if ((symbol->type != TYPE_DERIVED && right_code == NULL) || left_code == NULL ||
        storage == NULL)
        goto unsupported;

    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emitted_depth = depth + 1;
    f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
    if ((symbol->pointer || symbol->allocatable)) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "if (%s == NULL) abort();\n", storage);
    }
    if (!emit_shape(&context->output, unit, target, prepared_right, emitted_depth, target_extents,
                    right_extents) ||
        !emit_temporary_declaration(context, unit, target, &character_length, emitted_depth))
        goto emission_failed;
    if (prepared_right->rank == 0U) {
        if (symbol->type == TYPE_CHARACTER) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output,
                              "char *f2c_component_scalar = (char *)malloc("
                              "f2c_component_length == 0U ? 1U : f2c_component_length);\n");
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output, "if (f2c_component_scalar == NULL) abort();\n");
            if (!f2c_emit_character_storage_assignment(context, unit, "f2c_component_scalar",
                                                       "f2c_component_length", right_element,
                                                       right_code, emitted_depth))
                goto emission_failed;
        } else if (symbol->type == TYPE_DERIVED) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output, "%s f2c_component_scalar = {0};\n",
                              symbol->derived_type->c_name);
            if (!f2c_emit_derived_clone_expression(&context->output, unit, right_element,
                                                   "f2c_component_scalar", "component_scalar", line,
                                                   emitted_depth))
                goto emission_failed;
        } else {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output, "const %s f2c_component_scalar = (%s)(%s);\n",
                              f2c_symbol_c_type(symbol), f2c_symbol_c_type(symbol), right_code);
        }
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "size_t f2c_component_linear = 0U;\n");
    append_loops(&context->output, target->rank, &emitted_depth);
    if (symbol->type == TYPE_CHARACTER) {
        if (prepared_right->rank == 0U) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output, "if (f2c_component_length != 0U) memmove("
                                                "f2c_component_values + f2c_component_linear * "
                                                "f2c_component_length, f2c_component_scalar, "
                                                "f2c_component_length);\n");
        } else if (!f2c_emit_character_storage_assignment(
                       context, unit,
                       "f2c_component_values + f2c_component_linear * f2c_component_length",
                       "f2c_component_length", right_element, right_code, emitted_depth)) {
            goto emission_failed;
        }
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_component_linear;\n");
    } else if (symbol->type == TYPE_DERIVED) {
        if (prepared_right->rank == 0U) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "f2c_clone_%s(&f2c_component_values[f2c_component_linear], "
                              "&f2c_component_scalar);\n",
                              symbol->derived_type->c_name);
        } else if (!f2c_emit_derived_clone_expression(&context->output, unit, right_element,
                                                      "f2c_component_values[f2c_component_linear]",
                                                      "component", line, emitted_depth)) {
            goto emission_failed;
        }
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_component_linear;\n");
    } else {
        f2c_array_indent(&context->output, emitted_depth);
        if (prepared_right->rank == 0U)
            f2c_buffer_append(&context->output, "f2c_component_values[f2c_component_linear++] = "
                                                "f2c_component_scalar;\n");
        else
            f2c_buffer_printf(&context->output,
                              "f2c_component_values[f2c_component_linear++] = (%s)(%s);\n",
                              f2c_symbol_c_type(symbol), right_code);
    }
    close_loops(&context->output, target->rank, &emitted_depth);
    if (prepared_right->rank == 0U) {
        f2c_array_indent(&context->output, emitted_depth);
        if (symbol->type == TYPE_CHARACTER)
            f2c_buffer_append(&context->output, "free(f2c_component_scalar);\n");
        else if (symbol->type == TYPE_DERIVED)
            f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_component_scalar);\n",
                              symbol->derived_type->c_name);
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "f2c_component_linear = 0U;\n");
    append_loops(&context->output, target->rank, &emitted_depth);
    if (symbol->type == TYPE_CHARACTER) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_component_length != 0U) memmove(&(%s), "
                          "f2c_component_values + f2c_component_linear * "
                          "f2c_component_length, f2c_component_length);\n",
                          left_code);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_component_linear;\n");
    } else if (symbol->type == TYPE_DERIVED) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_destroy_%s(&(%s));\n",
                          symbol->derived_type->c_name, left_code);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "f2c_clone_%s(&(%s), "
                          "&f2c_component_values[f2c_component_linear++]);\n",
                          symbol->derived_type->c_name, left_code);
    } else {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "%s = f2c_component_values[f2c_component_linear++];\n",
                          left_code);
    }
    close_loops(&context->output, target->rank, &emitted_depth);
    if (symbol->type == TYPE_DERIVED) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "f2c_destroy_array_%s(f2c_component_values, "
                          "f2c_component_count, %zuU);\n",
                          symbol->derived_type->c_name, target->rank);
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "free(f2c_component_values);\n");
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;
    goto cleanup_all;

unsupported:
    f2c_diagnostic(context, line, 1,
                   "component array assignment cannot derive an element or runtime extent");
    result = 1;
    goto cleanup_all;

emission_failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    f2c_diagnostic(context, line, 1, "component array assignment could not be emitted safely");
    result = 1;

cleanup_all:
    free_dimensions(target_extents, target->rank);
    free_dimensions(right_extents, target->rank);
    free(right_code);
    free(left_code);
    free(storage);
    free(character_length);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_right);
    f2c_expr_free(right_element);
    f2c_expr_free(left_element);
    return result;
}
