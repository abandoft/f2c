#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static char *emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static const F2cExpr *keyword_value(const F2cStatement *statement, const char *name) {
    size_t i;
    for (i = 0U; i < statement->item_count; ++i) {
        const F2cExpr *argument = statement->arguments != NULL ? statement->arguments[i] : NULL;
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            argument->text != NULL && strcmp(argument->text, name) == 0 &&
            argument->child_count == 1U)
            return argument->children[0];
    }
    return NULL;
}

static int is_allocation_option(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT;
}

static char *allocation_target_storage(Unit *unit, const F2cExpr *target) {
    Buffer result = {0};
    char *base;
    if (target == NULL || target->symbol == NULL)
        return NULL;
    if (target->kind == F2C_EXPR_NAME || target->kind == F2C_EXPR_ARRAY_REFERENCE)
        return f2c_strdup(f2c_symbol_c_name(unit, target->symbol));
    if (target->kind != F2C_EXPR_COMPONENT || target->child_count == 0U)
        return NULL;
    base = emit_expression(unit, target->children[0]);
    if (base == NULL)
        return NULL;
    f2c_buffer_printf(&result, "(%s).%s", base, f2c_symbol_c_name(unit, target->symbol));
    free(base);
    return f2c_buffer_take(&result);
}

static char *emit_lower_bound(Unit *unit, const F2cExpr *bound) {
    if (bound != NULL && bound->kind == F2C_EXPR_ARRAY_SECTION && bound->child_count == 3U) {
        const F2cExpr *lower = bound->children[0];
        return lower->kind == F2C_EXPR_INVALID ? f2c_strdup("1") : emit_expression(unit, lower);
    }
    return f2c_strdup("1");
}

static char *emit_upper_bound(Unit *unit, const F2cExpr *bound) {
    if (bound != NULL && bound->kind == F2C_EXPR_ARRAY_SECTION && bound->child_count == 3U) {
        const F2cExpr *upper = bound->children[1];
        return upper->kind == F2C_EXPR_INVALID ? NULL : emit_expression(unit, upper);
    }
    return emit_expression(unit, bound);
}

static void emit_allocation_failure(Buffer *output, const char *status, int depth) {
    indent(output, depth);
    if (status != NULL)
        f2c_buffer_printf(output, "if (!f2c_alloc_ok) %s = 1;\n", status);
    else
        f2c_buffer_append(output, "if (!f2c_alloc_ok) abort();\n");
}

static Symbol *whole_array_symbol(const F2cExpr *expression) {
    if (expression == NULL || expression->kind != F2C_EXPR_NAME || expression->symbol == NULL ||
        expression->rank == 0U)
        return NULL;
    return expression->symbol;
}

static int emit_source_initialization(Context *context, Unit *unit, const Symbol *target,
                                      const F2cExpr *source, int depth) {
    Symbol *source_symbol;
    char *source_code;
    if (source == NULL)
        return 1;
    source_symbol = source->kind == F2C_EXPR_NAME ? source->symbol : NULL;
    if (source->rank != 0U) {
        if (source_symbol == NULL)
            return 0;
        if (target->type == TYPE_CHARACTER) {
            char *source_length = f2c_symbol_character_length(unit, source_symbol);
            if (source_length == NULL)
                return 0;
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_alloc_source_length = (size_t)(%s);\n",
                              source_length);
            indent(&context->output, depth);
            f2c_buffer_append(&context->output,
                              "const size_t f2c_alloc_copy_length = "
                              "F2C_MIN(f2c_alloc_char_len, f2c_alloc_source_length);\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output,
                              "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                              "f2c_alloc_count; ++f2c_alloc_index) {\n");
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_alloc_copy_length != 0U) "
                              "memmove(f2c_alloc_storage + f2c_alloc_index * "
                              "f2c_alloc_char_len, %s + f2c_alloc_index * "
                              "f2c_alloc_source_length, f2c_alloc_copy_length);\n",
                              f2c_symbol_c_name(unit, source_symbol));
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_alloc_char_len > f2c_alloc_copy_length) "
                                                "memset(f2c_alloc_storage + f2c_alloc_index * "
                                                "f2c_alloc_char_len + f2c_alloc_copy_length, ' ', "
                                                "f2c_alloc_char_len - f2c_alloc_copy_length);\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(source_length);
            return 1;
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                                            "f2c_alloc_count; ++f2c_alloc_index)\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "f2c_alloc_storage[f2c_alloc_index] = (%s)%s[f2c_alloc_index];\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_name(unit, source_symbol));
        return 1;
    }
    source_code = emit_expression(unit, source);
    if (source_code == NULL)
        return 0;
    if (target->type == TYPE_CHARACTER) {
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_alloc_count != 0U) {\n");
        if (!f2c_emit_character_storage_assignment(context, unit, "f2c_alloc_storage",
                                                   "f2c_alloc_char_len", source, source_code,
                                                   depth + 1)) {
            free(source_code);
            return 0;
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_alloc_index = 1U; f2c_alloc_index < "
                          "f2c_alloc_count; ++f2c_alloc_index) "
                          "if (f2c_alloc_char_len != 0U) "
                          "memmove(f2c_alloc_storage + f2c_alloc_index * "
                          "f2c_alloc_char_len, f2c_alloc_storage, f2c_alloc_char_len);\n");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const %s f2c_alloc_source_value = (%s)(%s);\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target), source_code);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                                            "f2c_alloc_count; ++f2c_alloc_index)\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "f2c_alloc_storage[f2c_alloc_index] = f2c_alloc_source_value;\n");
    }
    free(source_code);
    return 1;
}

int f2c_emit_allocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth) {
    const F2cExpr *status_expression = keyword_value(statement, "stat");
    const F2cExpr *source_expression = keyword_value(statement, "source");
    const F2cExpr *mold_expression = keyword_value(statement, "mold");
    const F2cExpr *model_expression =
        source_expression != NULL ? source_expression : mold_expression;
    Symbol *model_symbol = whole_array_symbol(model_expression);
    char *status = status_expression != NULL ? emit_expression(unit, status_expression) : NULL;
    size_t i;
    if (statement->arguments == NULL && statement->item_count != 0U)
        return 0;
    if (status != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = 0;\n", status);
    }
    for (i = 0U; i < statement->item_count; ++i) {
        const F2cExpr *target = statement->arguments[i];
        Symbol *symbol;
        char *target_name;
        size_t d;
        if (is_allocation_option(target))
            continue;
        symbol = target != NULL ? target->symbol : NULL;
        if (target == NULL || symbol == NULL || !symbol->allocatable ||
            (target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
             target->kind != F2C_EXPR_COMPONENT))
            continue;
        target_name = allocation_target_storage(unit, target);
        if (target_name == NULL)
            continue;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "bool f2c_alloc_ok = %s == NULL;\n", target_name);
        if (model_expression != NULL && model_expression->symbol != NULL &&
            model_expression->symbol->allocatable &&
            (model_expression->rank != 0U || model_expression->symbol->deferred_character)) {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output, "if (%s == NULL) f2c_alloc_ok = false;\n",
                              f2c_symbol_c_name(unit, model_expression->symbol));
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "size_t f2c_alloc_count = 1U;\n");
        for (d = 0U; d < symbol->rank; ++d) {
            const F2cExpr *bound =
                target->kind == F2C_EXPR_ARRAY_REFERENCE && d < target->child_count
                    ? target->children[d]
                : target->kind == F2C_EXPR_COMPONENT && d + 1U < target->child_count
                    ? target->children[d + 1U]
                    : NULL;
            char *lower = bound != NULL ? emit_lower_bound(unit, bound)
                                        : f2c_symbol_dimension_lower(unit, model_symbol, d);
            char *upper = bound != NULL ? emit_upper_bound(unit, bound)
                                        : f2c_symbol_dimension_upper(unit, model_symbol, d);
            if (lower == NULL || upper == NULL) {
                free(lower);
                free(upper);
                free(status);
                return 0;
            }
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const int64_t f2c_alloc_lower_%zu = (int64_t)(%s);\n", d + 1U,
                              lower);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const int64_t f2c_alloc_upper_%zu = (int64_t)(%s);\n", d + 1U,
                              upper);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const uint64_t f2c_alloc_span_%zu = f2c_alloc_upper_%zu >= "
                              "f2c_alloc_lower_%zu ? (uint64_t)f2c_alloc_upper_%zu - "
                              "(uint64_t)f2c_alloc_lower_%zu : UINT64_C(0);\n",
                              d + 1U, d + 1U, d + 1U, d + 1U, d + 1U);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_alloc_extent_%zu = f2c_alloc_upper_%zu >= "
                              "f2c_alloc_lower_%zu && f2c_alloc_span_%zu < (uint64_t)INT32_MAX ? "
                              "(size_t)(f2c_alloc_span_%zu + UINT64_C(1)) : 0U;\n",
                              d + 1U, d + 1U, d + 1U, d + 1U, d + 1U);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(
                &context->output,
                "if (f2c_alloc_lower_%zu < INT32_MIN || f2c_alloc_lower_%zu > INT32_MAX || "
                "(f2c_alloc_upper_%zu >= f2c_alloc_lower_%zu && f2c_alloc_extent_%zu == 0U)) "
                "f2c_alloc_ok = false;\n",
                d + 1U, d + 1U, d + 1U, d + 1U, d + 1U);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_alloc_ok && !f2c_size_multiply(f2c_alloc_count, "
                              "f2c_alloc_extent_%zu, &f2c_alloc_count)) f2c_alloc_ok = false;\n",
                              d + 1U);
            if (source_expression != NULL && source_expression->rank != 0U && bound != NULL) {
                char *source_extent =
                    f2c_symbol_dimension_extent(unit, source_expression->symbol, d);
                if (source_extent == NULL) {
                    free(lower);
                    free(upper);
                    free(status);
                    return 0;
                }
                indent(&context->output, depth + 1);
                f2c_buffer_printf(&context->output,
                                  "if (f2c_alloc_extent_%zu != (size_t)(%s)) "
                                  "f2c_alloc_ok = false;\n",
                                  d + 1U, source_extent);
                free(source_extent);
            }
            free(lower);
            free(upper);
        }
        if (symbol->type == TYPE_CHARACTER) {
            char *length =
                symbol->deferred_character
                    ? (statement->allocation_character_length != NULL
                           ? emit_expression(unit, statement->allocation_character_length)
                           : f2c_character_length_expression(unit, model_expression))
                    : f2c_symbol_character_length(unit, symbol);
            if (length == NULL) {
                free(status);
                return 0;
            }
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const int64_t f2c_alloc_char_len_value = (int64_t)(%s);\n", length);
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output,
                              "const size_t f2c_alloc_char_len = f2c_alloc_char_len_value > 0 ? "
                              "(size_t)f2c_alloc_char_len_value : 0U;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output,
                              "if (f2c_alloc_char_len != 0U && f2c_alloc_count > "
                              "SIZE_MAX / f2c_alloc_char_len) f2c_alloc_ok = false;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output,
                              "size_t f2c_alloc_bytes = f2c_alloc_ok ? f2c_alloc_count * "
                              "f2c_alloc_char_len : 0U;\n");
            if (symbol->rank == 0U) {
                indent(&context->output, depth + 1);
                f2c_buffer_append(&context->output,
                                  "if (f2c_alloc_bytes == SIZE_MAX) f2c_alloc_ok = false;\n");
                indent(&context->output, depth + 1);
                f2c_buffer_append(&context->output, "if (f2c_alloc_ok) ++f2c_alloc_bytes;\n");
            }
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output,
                              "char *f2c_alloc_storage = f2c_alloc_ok ? (char *)calloc("
                              "f2c_alloc_bytes == 0U ? 1U : f2c_alloc_bytes, 1U) : NULL;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_alloc_ok && f2c_alloc_storage == NULL) "
                                                "f2c_alloc_ok = false;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_alloc_ok) {\n");
            if (!emit_source_initialization(context, unit, symbol, source_expression, depth + 2)) {
                free(length);
                free(status);
                return 0;
            }
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "%s = f2c_alloc_storage;\n", target_name);
            if (symbol->deferred_character) {
                indent(&context->output, depth + 2);
                if (target->kind == F2C_EXPR_COMPONENT)
                    f2c_buffer_printf(&context->output,
                                      "%s_character_length = f2c_alloc_char_len;\n", target_name);
                else
                    f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_alloc_char_len;\n",
                                      target_name);
            }
            free(length);
        } else {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(
                &context->output,
                "%s *f2c_alloc_storage = f2c_alloc_ok ? (%s *)calloc("
                "f2c_alloc_count == 0U ? 1U : f2c_alloc_count, sizeof(*%s)) : NULL;\n",
                f2c_symbol_c_type(symbol), f2c_symbol_c_type(symbol), target_name);
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_alloc_ok && f2c_alloc_storage == NULL) "
                                                "f2c_alloc_ok = false;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_alloc_ok) {\n");
            if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
                indent(&context->output, depth + 2);
                f2c_buffer_printf(&context->output,
                                  "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                                  "f2c_alloc_count; ++f2c_alloc_index) "
                                  "f2c_initialize_%s(&f2c_alloc_storage[f2c_alloc_index]);\n",
                                  symbol->derived_type->c_name);
            }
            if (!emit_source_initialization(context, unit, symbol, source_expression, depth + 2)) {
                free(status);
                return 0;
            }
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "%s = f2c_alloc_storage;\n", target_name);
        }
        for (d = 0U; d < symbol->rank; ++d) {
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "%s_lower_%zu = (int32_t)f2c_alloc_lower_%zu;\n",
                              target_name, d + 1U, d + 1U);
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "%s_extent_%zu = (int32_t)f2c_alloc_extent_%zu;\n",
                              target_name, d + 1U, d + 1U);
            if (target->kind == F2C_EXPR_NAME && symbol->argument &&
                f2c_symbol_uses_descriptor(symbol)) {
                indent(&context->output, depth + 2);
                if (d == 0U)
                    f2c_buffer_printf(&context->output, "%s_stride_1 = 1;\n", target_name);
                else
                    f2c_buffer_printf(&context->output,
                                      "%s_stride_%zu = f2c_descriptor_stride_extent(%s_stride_%zu, "
                                      "(size_t)%s_extent_%zu);\n",
                                      target_name, d + 1U, target_name, d, target_name, d);
            }
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        emit_allocation_failure(&context->output, status, depth + 1);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(target_name);
    }
    free(status);
    return 1;
}

int f2c_emit_deallocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth) {
    const F2cExpr *status_expression = keyword_value(statement, "stat");
    char *status = status_expression != NULL ? emit_expression(unit, status_expression) : NULL;
    size_t i;
    if (statement->arguments == NULL && statement->item_count != 0U)
        return 0;
    if (status != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = 0;\n", status);
    }
    for (i = 0U; i < statement->item_count; ++i) {
        const F2cExpr *target = statement->arguments[i];
        Symbol *symbol;
        char *target_name;
        size_t d;
        if (is_allocation_option(target))
            continue;
        symbol = target != NULL ? target->symbol : NULL;
        if (symbol == NULL || !symbol->allocatable)
            continue;
        target_name = allocation_target_storage(unit, target);
        if (target_name == NULL)
            continue;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const bool f2c_dealloc_ok = %s != NULL;\n",
                          target_name);
        indent(&context->output, depth + 1);
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            Buffer count = {0};
            for (d = 0U; d < symbol->rank; ++d)
                f2c_buffer_printf(&count, "%s(size_t)(%s_extent_%zu)", d == 0U ? "" : " * ",
                                  target_name, d + 1U);
            f2c_buffer_printf(&context->output,
                              "if (f2c_dealloc_ok) f2c_destroy_array_%s(%s, (size_t)(%s), "
                              "%zuU);\n",
                              symbol->derived_type->c_name, target_name,
                              count.data != NULL ? count.data : "1U", symbol->rank);
            indent(&context->output, depth + 1);
            free(count.data);
        }
        f2c_buffer_printf(&context->output, "if (f2c_dealloc_ok) { free(%s); %s = NULL; }\n",
                          target_name, target_name);
        indent(&context->output, depth + 1);
        if (status != NULL)
            f2c_buffer_printf(&context->output, "if (!f2c_dealloc_ok) %s = 1;\n", status);
        else
            f2c_buffer_append(&context->output, "if (!f2c_dealloc_ok) abort();\n");
        if (symbol->deferred_character) {
            indent(&context->output, depth + 1);
            if (target->kind == F2C_EXPR_COMPONENT)
                f2c_buffer_printf(&context->output,
                                  "if (f2c_dealloc_ok) %s_character_length = 0U;\n", target_name);
            else
                f2c_buffer_printf(&context->output, "if (f2c_dealloc_ok) f2c_char_len_%s = 0U;\n",
                                  target_name);
        }
        for (d = 0U; d < symbol->rank; ++d) {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_dealloc_ok) { %s_lower_%zu = 1; %s_extent_%zu = 0;",
                              target_name, d + 1U, target_name, d + 1U);
            if (target->kind == F2C_EXPR_NAME && symbol->argument &&
                f2c_symbol_uses_descriptor(symbol))
                f2c_buffer_printf(&context->output, " %s_stride_%zu = %s;", target_name, d + 1U,
                                  d == 0U ? "1" : "0");
            f2c_buffer_append(&context->output, " }\n");
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(target_name);
    }
    free(status);
    return 1;
}
