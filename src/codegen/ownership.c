#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static const F2cExpr *move_alloc_actual(const F2cStatement *statement, size_t index) {
    const F2cExpr *actual;
    if (statement == NULL || statement->arguments == NULL || index >= statement->item_count)
        return NULL;
    actual = statement->arguments[index];
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        return actual->children[0];
    return actual;
}

static char *emit_scalar_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result;
    if (expression == NULL)
        return NULL;
    result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static int assignment_types_compatible(Type target, Type source) {
    if (target == TYPE_DERIVED || source == TYPE_DERIVED)
        return target == TYPE_DERIVED && source == TYPE_DERIVED;
    if (target == TYPE_CHARACTER || source == TYPE_CHARACTER)
        return target == TYPE_CHARACTER && source == TYPE_CHARACTER;
    if (target == TYPE_LOGICAL || source == TYPE_LOGICAL)
        return target == TYPE_LOGICAL && source == TYPE_LOGICAL;
    return f2c_type_is_numeric(target) && f2c_type_is_numeric(source);
}

static int emit_source_descriptor(Context *context, Unit *unit, const Symbol *source,
                                  size_t dimension, int depth) {
    char *lower = f2c_symbol_dimension_lower(unit, source, dimension);
    char *extent = f2c_symbol_dimension_extent(unit, source, dimension);
    if (lower == NULL || extent == NULL) {
        free(lower);
        free(extent);
        return 0;
    }
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "const int64_t f2c_assign_lower_value_%zu = (int64_t)(%s);\n", dimension + 1U,
                      lower);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_assign_extent_%zu = (size_t)(%s);\n",
                      dimension + 1U, extent);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_assign_lower_value_%zu < INT32_MIN || "
                      "f2c_assign_lower_value_%zu > INT32_MAX || "
                      "f2c_assign_extent_%zu > (size_t)INT32_MAX) abort();\n",
                      dimension + 1U, dimension + 1U, dimension + 1U);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "const int32_t f2c_assign_lower_%zu = "
                      "(int32_t)f2c_assign_lower_value_%zu;\n",
                      dimension + 1U, dimension + 1U);
    free(lower);
    free(extent);
    return 1;
}

static void emit_count(Context *context, const Symbol *target, int depth) {
    size_t dimension;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_assign_count = 1U;\n");
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_assign_extent_%zu != 0U && f2c_assign_count > "
                          "SIZE_MAX / f2c_assign_extent_%zu) abort();\n",
                          dimension + 1U, dimension + 1U);
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_assign_count *= f2c_assign_extent_%zu;\n",
                          dimension + 1U);
    }
}

static void emit_reallocation_test(Context *context, Unit *unit, const Symbol *target,
                                   int deferred_character, int depth) {
    const char *name = f2c_symbol_c_name(unit, target);
    size_t dimension;
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "bool f2c_assign_reallocate = %s == NULL", name);
    for (dimension = 0U; dimension < target->rank; ++dimension)
        f2c_buffer_printf(&context->output, " || (size_t)%s_extent_%zu != f2c_assign_extent_%zu",
                          name, dimension + 1U, dimension + 1U);
    if (deferred_character)
        f2c_buffer_printf(&context->output, " || f2c_char_len_%s != f2c_assign_character_length",
                          name);
    f2c_buffer_append(&context->output, ";\n");
}

static void emit_character_copy(Context *context, Unit *unit, const Symbol *target, int depth) {
    const char *name = f2c_symbol_c_name(unit, target);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "if (f2c_assign_character_length != 0U && f2c_assign_count > "
                      "SIZE_MAX / f2c_assign_character_length) abort();\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "const size_t f2c_assign_bytes = f2c_assign_count * "
                                        "f2c_assign_character_length;\n");
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "char *f2c_assign_destination = f2c_assign_reallocate ? "
                      "(char *)malloc(f2c_assign_bytes == 0U ? 1U : f2c_assign_bytes) : %s;\n",
                      name);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_assign_destination == NULL) abort();\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "const size_t f2c_assign_copy_length = "
                                        "F2C_MIN(f2c_assign_character_length, "
                                        "f2c_assign_source_character_length);\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "for (size_t f2c_assign_index = 0U; f2c_assign_index < "
                                        "f2c_assign_count; ++f2c_assign_index) {\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_assign_copy_length != 0U) "
                                        "memmove(f2c_assign_destination + f2c_assign_index * "
                                        "f2c_assign_character_length, f2c_assign_source + "
                                        "f2c_assign_index * f2c_assign_source_character_length, "
                                        "f2c_assign_copy_length);\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_assign_character_length > f2c_assign_copy_length) "
                      "memset(f2c_assign_destination + f2c_assign_index * "
                      "f2c_assign_character_length + f2c_assign_copy_length, ' ', "
                      "f2c_assign_character_length - f2c_assign_copy_length);\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

static void emit_intrinsic_copy(Context *context, Unit *unit, const Symbol *target, int depth) {
    const char *name = f2c_symbol_c_name(unit, target);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (f2c_assign_count > SIZE_MAX / sizeof(%s)) abort();\n",
                      f2c_symbol_c_type(target));
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_assign_destination = f2c_assign_reallocate ? "
                      "(%s *)malloc(f2c_assign_count == 0U ? 1U : "
                      "f2c_assign_count * sizeof(*f2c_assign_destination)) : %s;\n",
                      f2c_symbol_c_type(target), f2c_symbol_c_type(target), name);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_assign_destination == NULL) abort();\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "for (size_t f2c_assign_index = 0U; f2c_assign_index < "
                                        "f2c_assign_count; ++f2c_assign_index)\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "f2c_assign_destination[f2c_assign_index] = (%s)"
                      "f2c_assign_source[f2c_assign_index];\n",
                      f2c_symbol_c_type(target));
}

static void emit_descriptor_commit(Context *context, Unit *unit, const Symbol *target,
                                   int deferred_character, int depth) {
    const char *name = f2c_symbol_c_name(unit, target);
    size_t dimension;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_assign_reallocate) {\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s = f2c_assign_destination;\n", name);
    if (deferred_character) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_assign_character_length;\n",
                          name);
    }
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s_lower_%zu = f2c_assign_lower_%zu;\n", name,
                          dimension + 1U, dimension + 1U);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s_extent_%zu = (int32_t)f2c_assign_extent_%zu;\n",
                          name, dimension + 1U, dimension + 1U);
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

int f2c_emit_allocatable_array_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                          const F2cExpr *right, int depth) {
    Symbol *target = left != NULL ? left->symbol : NULL;
    Symbol *source = right != NULL && right->kind == F2C_EXPR_NAME ? right->symbol : NULL;
    char *source_length = NULL;
    const size_t output_start = context != NULL ? context->output.length : 0U;
    size_t dimension;
    const int character = target != NULL && target->type == TYPE_CHARACTER;
    const int derived = target != NULL && target->type == TYPE_DERIVED;
    const int deferred_character = character && target->deferred_character;
    if (context == NULL || unit == NULL || left == NULL || right == NULL ||
        left->kind != F2C_EXPR_NAME || target == NULL || !target->allocatable ||
        target->rank == 0U || source == NULL || source->rank != target->rank ||
        !assignment_types_compatible(target->type, source->type) ||
        (derived && target->derived_type != source->derived_type))
        return 0;
    if (character) {
        source_length = f2c_symbol_character_length(unit, source);
        if (source_length == NULL)
            return 0;
    }

    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const %s *f2c_assign_source = %s;\n",
                      f2c_symbol_c_type(source), f2c_symbol_c_name(unit, source));
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_assign_source == NULL) abort();\n");
    if (character) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_assign_source_character_length = (size_t)(%s);\n",
                          source_length);
        indent(&context->output, depth + 1);
        if (deferred_character) {
            f2c_buffer_append(&context->output, "const size_t f2c_assign_character_length = "
                                                "f2c_assign_source_character_length;\n");
        } else {
            char *target_length = f2c_symbol_character_length(unit, target);
            if (target_length == NULL)
                goto failed;
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_assign_character_length = (size_t)(%s);\n",
                              target_length);
            free(target_length);
        }
    }
    for (dimension = 0U; dimension < target->rank; ++dimension) {
        if (!emit_source_descriptor(context, unit, source, dimension, depth + 1)) {
            goto failed;
        }
    }
    emit_count(context, target, depth + 1);
    if (derived) {
        char *old_count = f2c_symbol_element_count(unit, target);
        const char *name = f2c_symbol_c_name(unit, target);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_assign_count > SIZE_MAX / sizeof(%s)) abort();\n",
                          f2c_symbol_c_type(target));
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s *f2c_assign_destination = (%s *)calloc("
                          "f2c_assign_count == 0U ? 1U : f2c_assign_count, "
                          "sizeof(*f2c_assign_destination));\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target));
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_assign_destination == NULL) abort();\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "for (size_t i = 0U; i < f2c_assign_count; ++i) "
                          "f2c_copy_%s(&f2c_assign_destination[i], &f2c_assign_source[i]);\n",
                          target->derived_type->c_name);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (%s != NULL) { f2c_destroy_array_%s(%s, (size_t)(%s), %zuU); "
                          "free(%s); }\n",
                          name, target->derived_type->c_name, name,
                          old_count != NULL ? old_count : "0U", target->rank, name);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_assign_destination;\n", name);
        for (dimension = 0U; dimension < target->rank; ++dimension) {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = f2c_assign_lower_%zu; "
                              "%s_extent_%zu = (int32_t)f2c_assign_extent_%zu;\n",
                              name, dimension + 1U, dimension + 1U, name, dimension + 1U,
                              dimension + 1U);
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(old_count);
        free(source_length);
        return 1;
    }
    emit_reallocation_test(context, unit, target, deferred_character, depth + 1);
    if (character)
        emit_character_copy(context, unit, target, depth + 1);
    else
        emit_intrinsic_copy(context, unit, target, depth + 1);
    emit_descriptor_commit(context, unit, target, deferred_character, depth + 1);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(source_length);
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(source_length);
    return 0;
}

int f2c_emit_move_alloc_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth) {
    const F2cExpr *from_expression = move_alloc_actual(statement, 0U);
    const F2cExpr *to_expression = move_alloc_actual(statement, 1U);
    const F2cExpr *status_expression = move_alloc_actual(statement, 2U);
    Symbol *from = from_expression != NULL ? from_expression->symbol : NULL;
    Symbol *to = to_expression != NULL ? to_expression->symbol : NULL;
    const char *from_name;
    const char *to_name;
    char *status = NULL;
    size_t dimension;
    size_t output_start;
    if (context == NULL || unit == NULL || statement == NULL || statement->item_count != 4U ||
        from_expression == NULL || to_expression == NULL ||
        from_expression->kind != F2C_EXPR_NAME || to_expression->kind != F2C_EXPR_NAME ||
        from == NULL || to == NULL || !from->allocatable || !to->allocatable || from == to ||
        from->type != to->type || from->rank != to->rank)
        return 0;
    status = emit_scalar_expression(unit, status_expression);
    if (status_expression != NULL && status == NULL)
        return 0;
    output_start = context->output.length;
    from_name = f2c_symbol_c_name(unit, from);
    to_name = f2c_symbol_c_name(unit, to);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (to->type == TYPE_DERIVED && to->derived_type != NULL) {
        char *count = f2c_symbol_element_count(unit, to);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(
            &context->output, "if (%s != NULL) f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
            to_name, to->derived_type->c_name, to_name, count != NULL ? count : "0U", to->rank);
        free(count);
    }
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "free(%s);\n", to_name);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s = %s;\n", to_name, from_name);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s = NULL;\n", from_name);
    if (from->deferred_character) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_char_len_%s;\n", to_name,
                          from_name);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_char_len_%s = 0U;\n", from_name);
    }
    for (dimension = 0U; dimension < from->rank; ++dimension) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s_lower_%zu = %s_lower_%zu;\n", to_name,
                          dimension + 1U, from_name, dimension + 1U);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s_extent_%zu = %s_extent_%zu;\n", to_name,
                          dimension + 1U, from_name, dimension + 1U);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s_lower_%zu = 1;\n", from_name, dimension + 1U);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s_extent_%zu = 0;\n", from_name, dimension + 1U);
    }
    if (status != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = 0;\n", status);
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(status);
    if (!context->output.failed)
        return 1;
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    return 0;
}
