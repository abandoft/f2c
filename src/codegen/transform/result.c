#include "codegen/transform/private.h"

#include <stdlib.h>
#include <string.h>

void f2c_transform_emit_result_count(Context *context, size_t rank, int depth) {
    size_t dimension;
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_transform_result_count = 1U;\n");
    for (dimension = 0U; dimension < rank; ++dimension) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_transform_result_extent_%zu != 0U && "
                          "f2c_transform_result_count > SIZE_MAX / "
                          "f2c_transform_result_extent_%zu) abort();\n",
                          dimension + 1U, dimension + 1U);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "f2c_transform_result_count *= f2c_transform_result_extent_%zu;\n",
                          dimension + 1U);
    }
}

void f2c_transform_emit_result_allocation(Context *context, Unit *unit, const Symbol *target,
                                          const F2cExpr *element_source, int depth) {
    if (target->type == TYPE_CHARACTER) {
        char *length = target->deferred_character
                           ? f2c_character_length_expression(unit, element_source)
                           : f2c_symbol_character_length(unit, target);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transform_result_element_length = (size_t)(%s);\n",
                          length != NULL ? length : "0U");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_transform_result_element_length != 0U && "
                                            "f2c_transform_result_count > SIZE_MAX / "
                                            "f2c_transform_result_element_length) abort();\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "char *f2c_transform_result = (char *)malloc("
                                            "f2c_transform_result_count == 0U || "
                                            "f2c_transform_result_element_length == 0U ? 1U : "
                                            "f2c_transform_result_count * "
                                            "f2c_transform_result_element_length);\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_transform_result == NULL) abort();\n");
        free(length);
        return;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_transform_result_count > SIZE_MAX / sizeof(%s)) abort();\n",
                      f2c_symbol_c_type(target));
    f2c_transform_indent(&context->output, depth);
    if (target->type == TYPE_DERIVED)
        f2c_buffer_printf(&context->output,
                          "%s *f2c_transform_result = (%s *)calloc("
                          "f2c_transform_result_count == 0U ? 1U : "
                          "f2c_transform_result_count, sizeof(%s));\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target),
                          f2c_symbol_c_type(target));
    else
        f2c_buffer_printf(&context->output,
                          "%s *f2c_transform_result = (%s *)malloc("
                          "f2c_transform_result_count == 0U ? sizeof(%s) : "
                          "f2c_transform_result_count * sizeof(%s));\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target),
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target));
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_transform_result == NULL) abort();\n");
}

void f2c_transform_emit_result_commit(Context *context, Unit *unit, Symbol *target, size_t rank,
                                      int depth) {
    const char *name = f2c_symbol_c_name(unit, target);
    size_t dimension;
    if (target->allocatable) {
        f2c_transform_indent(&context->output, depth);
        if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
            char *old_count = f2c_symbol_element_count(unit, target);
            f2c_buffer_printf(&context->output, "if (%s != NULL) {\n", name);
            f2c_transform_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              target->derived_type->c_name, name,
                              old_count != NULL ? old_count : "0U", rank);
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "free(%s);\n", name);
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = f2c_transform_result;\n", name);
            free(old_count);
        } else {
            f2c_buffer_printf(&context->output, "free(%s);\n", name);
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = f2c_transform_result;\n", name);
        }
        if (target->type == TYPE_CHARACTER && target->deferred_character) {
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "f2c_char_len_%s = f2c_transform_result_element_length;\n", name);
        }
        for (dimension = 0U; dimension < rank; ++dimension) {
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = 1; %s_extent_%zu = "
                              "(int32_t)f2c_transform_result_extent_%zu;\n",
                              name, dimension + 1U, name, dimension + 1U, dimension + 1U);
        }
    } else {
        for (dimension = 0U; dimension < rank; ++dimension) {
            char *extent = f2c_symbol_dimension_extent(unit, target, dimension);
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != f2c_transform_result_extent_%zu) abort();\n",
                              extent != NULL ? extent : "0U", dimension + 1U);
            free(extent);
        }
        f2c_transform_indent(&context->output, depth);
        if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_transform_index = 0U; "
                              "f2c_transform_index < f2c_transform_result_count; "
                              "++f2c_transform_index) f2c_copy_%s(&%s[f2c_transform_index], "
                              "&f2c_transform_result[f2c_transform_index]);\n",
                              target->derived_type->c_name, name);
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "f2c_destroy_array_%s(f2c_transform_result, "
                              "f2c_transform_result_count, %zuU);\n",
                              target->derived_type->c_name, rank);
        } else if (target->type == TYPE_CHARACTER) {
            f2c_buffer_printf(&context->output,
                              "if (f2c_transform_result_count != 0U && "
                              "f2c_transform_result_element_length != 0U) memmove(%s, "
                              "f2c_transform_result, f2c_transform_result_count * "
                              "f2c_transform_result_element_length);\n",
                              name);
        } else {
            f2c_buffer_printf(&context->output,
                              "if (f2c_transform_result_count != 0U) memmove(%s, "
                              "f2c_transform_result, f2c_transform_result_count * sizeof(*%s));\n",
                              name, name);
        }
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "free(f2c_transform_result);\n");
    }
    f2c_transform_indent(&context->output, depth - 1);
    f2c_buffer_append(&context->output, "}\n");
}

void f2c_transform_append_array_store(Buffer *output, const Symbol *target, const char *destination,
                                      const TransformArray *source, const char *source_index) {
    if (target->type == TYPE_CHARACTER) {
        f2c_buffer_printf(output,
                          "{ char *f2c_dst = f2c_transform_result + (%s) * "
                          "f2c_transform_result_element_length; const char *f2c_src = %s + "
                          "(%s) * (size_t)(%s); size_t f2c_copy = "
                          "F2C_MIN(f2c_transform_result_element_length, (size_t)(%s)); "
                          "if (f2c_copy != 0U) memmove(f2c_dst, f2c_src, f2c_copy); "
                          "if (f2c_transform_result_element_length > f2c_copy) "
                          "memset(f2c_dst + f2c_copy, ' ', "
                          "f2c_transform_result_element_length - f2c_copy); } ",
                          destination, source->pointer, source_index,
                          source->element_length != NULL ? source->element_length : "0U",
                          source->element_length != NULL ? source->element_length : "0U");
    } else if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_buffer_printf(output, "f2c_clone_%s(&f2c_transform_result[%s], &%s[%s]); ",
                          target->derived_type->c_name, destination, source->pointer, source_index);
    } else {
        f2c_buffer_printf(output, "f2c_transform_result[%s] = %s[%s]; ", destination,
                          source->pointer, source_index);
    }
}

void f2c_transform_append_scalar_store(Buffer *output, const Symbol *target,
                                       const char *destination, const char *source,
                                       const char *source_length) {
    if (target->type == TYPE_CHARACTER) {
        f2c_buffer_printf(output,
                          "{ char *f2c_dst = f2c_transform_result + (%s) * "
                          "f2c_transform_result_element_length; const char *f2c_src = (%s); "
                          "size_t f2c_src_len = (size_t)(%s); size_t f2c_copy = "
                          "F2C_MIN(f2c_transform_result_element_length, f2c_src_len); "
                          "if (f2c_copy != 0U) memmove(f2c_dst, f2c_src, f2c_copy); "
                          "if (f2c_transform_result_element_length > f2c_copy) "
                          "memset(f2c_dst + f2c_copy, ' ', "
                          "f2c_transform_result_element_length - f2c_copy); } ",
                          destination, source, source_length != NULL ? source_length : "0U");
    } else if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_buffer_printf(output, "f2c_clone_%s(&f2c_transform_result[%s], &(%s)); ",
                          target->derived_type->c_name, destination, source);
    } else {
        f2c_buffer_printf(output, "f2c_transform_result[%s] = (%s); ", destination, source);
    }
}

void f2c_transform_emit_source_extents(Context *context, const TransformArray *source, int depth) {
    size_t dimension;
    for (dimension = 0U; dimension < source->rank; ++dimension) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_source_extent_%zu = %s;\n",
                          dimension + 1U, source->extents[dimension]);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)f2c_transform_source_extent_%zu;\n",
                          dimension + 1U);
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_source_count = %s;\n",
                      source->count);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "(void)f2c_transform_source_count;\n");
}
