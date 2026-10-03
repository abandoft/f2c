#include "codegen/allocation/context.h"
#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

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

static int is_allocation_option(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT;
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

int f2c_emit_allocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth) {
    F2cAllocationContext allocation = {0};
    F2cAllocationModel model = {0};
    char *target_name = NULL;
    char *target_binding = NULL;
    size_t i;
    if (!f2c_allocation_context_begin(&allocation, context, unit, statement, depth))
        return 0;
    ++depth;
    const F2cExpr *source_expression = f2c_allocation_keyword(&allocation, "source");
    const F2cExpr *mold_expression = f2c_allocation_keyword(&allocation, "mold");
    const F2cExpr *model_expression =
        source_expression != NULL ? source_expression : mold_expression;
    const int has_model = model_expression != NULL;
    if (!f2c_allocation_model_prepare(context, unit, statement, model_expression,
                                      source_expression != NULL, depth, &model)) {
        f2c_allocation_model_clear(unit, &model);
        f2c_allocation_context_clear(&allocation);
        return 0;
    }
    if (model.guarded)
        ++depth;
    if (!f2c_allocation_capture(&allocation, &model.cleanup, depth))
        goto failed;
    if (has_model) {
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "bool f2c_alloc_statement_ok = true;\n");
    }
    for (i = 0U; i < statement->item_count; ++i) {
        F2cExpr *target = allocation.arguments[i];
        Symbol *symbol;
        Buffer target_prelude = {0};
        size_t d;
        if (is_allocation_option(target))
            continue;
        symbol = target != NULL ? target->symbol : NULL;
        if (target == NULL || symbol == NULL || (!symbol->allocatable && !symbol->pointer) ||
            (target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
             target->kind != F2C_EXPR_COMPONENT))
            continue;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        if (!f2c_allocation_prepare_value(&allocation, target, 1, depth + 1))
            goto failed;
        const F2cStorageReference reference = f2c_ir_storage_reference(target);
        target_binding = f2c_allocation_target_storage(unit, target, &target_prelude);
        target_name =
            f2c_storage_bound_property(unit, &reference, F2C_OBJECT_DATA, 0U, target_binding, 1);
        if (target_binding == NULL || target_name == NULL) {
            free(target_prelude.data);
            goto failed;
        }
        if (target_prelude.data != NULL) {
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, target_prelude.data);
        }
        free(target_prelude.data);
        indent(&context->output, depth + 1);
        if (has_model) {
            f2c_buffer_append(&context->output,
                              "const bool f2c_alloc_attempt = f2c_alloc_statement_ok;\n");
            indent(&context->output, depth + 1);
            if (symbol->pointer)
                f2c_buffer_append(&context->output, "bool f2c_alloc_ok = f2c_alloc_attempt;\n");
            else
                f2c_buffer_printf(&context->output,
                                  "bool f2c_alloc_ok = f2c_alloc_attempt && %s == NULL;\n",
                                  target_name);
        } else if (symbol->pointer) {
            f2c_buffer_append(&context->output, "bool f2c_alloc_ok = true;\n");
        } else {
            f2c_buffer_printf(&context->output, "bool f2c_alloc_ok = %s == NULL;\n", target_name);
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
                                        : f2c_allocation_model_lower(unit, &model, d);
            char *upper = bound != NULL ? emit_upper_bound(unit, bound)
                                        : f2c_allocation_model_upper(unit, &model, d);
            if (lower == NULL || upper == NULL) {
                free(lower);
                free(upper);
                goto failed;
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
                char *source_extent = f2c_allocation_model_extent(&model, d);
                if (source_extent == NULL) {
                    free(lower);
                    free(upper);
                    goto failed;
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
            char *length = allocation.length_value != NULL ? f2c_strdup(allocation.length_value)
                           : symbol->deferred_character
                               ? f2c_allocation_model_character_length(unit, &model)
                               : f2c_symbol_character_length(unit, symbol);
            if (length == NULL) {
                goto failed;
            }
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const int64_t f2c_alloc_char_len_value = (int64_t)(%s);\n", length);
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "size_t f2c_alloc_char_len = 0U;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output,
                              "if (!f2c_character_parameter_size(f2c_alloc_char_len_value, "
                              "&f2c_alloc_char_len)) f2c_alloc_ok = false;\n");
            if (!symbol->deferred_character &&
                (allocation.length_value != NULL || model.expression != NULL)) {
                char *declared = f2c_symbol_character_length(unit, symbol);
                char *specified = allocation.length_value != NULL
                                      ? f2c_strdup("f2c_alloc_char_len")
                                      : f2c_allocation_model_character_length(unit, &model);
                if (declared == NULL || specified == NULL) {
                    free(declared);
                    free(specified);
                    free(length);
                    goto failed;
                }
                indent(&context->output, depth + 1);
                f2c_buffer_printf(&context->output,
                                  "if ((size_t)(%s) != (size_t)(%s)) f2c_alloc_ok = false;\n",
                                  specified, declared);
                free(declared);
                free(specified);
            }
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
            if (!f2c_allocation_model_emit_source(context, symbol, &model, depth + 2)) {
                free(length);
                goto failed;
            }
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "%s = f2c_alloc_storage;\n", target_name);
            if (symbol->pointer) {
                f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_DEALLOCATABLE,
                                     0U, target_binding, "true", depth + 2);
            }
            if (symbol->deferred_character) {
                f2c_allocation_store(&context->output, unit, &reference,
                                     F2C_OBJECT_CHARACTER_LENGTH, 0U, target_binding,
                                     "f2c_alloc_char_len", depth + 2);
            }
            free(length);
        } else {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s *f2c_alloc_storage = f2c_alloc_ok ? (%s *)calloc("
                              "f2c_alloc_count == 0U ? 1U : f2c_alloc_count, sizeof(%s)) : NULL;\n",
                              f2c_symbol_c_type(symbol), f2c_symbol_c_type(symbol),
                              f2c_symbol_c_type(symbol));
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
            if (!f2c_allocation_model_emit_source(context, symbol, &model, depth + 2)) {
                goto failed;
            }
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "%s = f2c_alloc_storage;\n", target_name);
            if (symbol->pointer) {
                f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_DEALLOCATABLE,
                                     0U, target_binding, "true", depth + 2);
            }
        }
        for (d = 0U; d < symbol->rank; ++d) {
            Buffer lower = {0};
            Buffer extent = {0};
            f2c_buffer_printf(&lower, "(int32_t)f2c_alloc_lower_%zu", d + 1U);
            f2c_buffer_printf(&extent, "(int32_t)f2c_alloc_extent_%zu", d + 1U);
            f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_LOWER, d,
                                 target_binding, lower.data, depth + 2);
            f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_EXTENT, d,
                                 target_binding, extent.data, depth + 2);
            free(lower.data);
            free(extent.data);
            if (symbol->pointer || reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR) {
                if (d == 0U)
                    f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, d,
                                         target_binding, "1", depth + 2);
                else {
                    char *prior = f2c_storage_bound_property(unit, &reference, F2C_OBJECT_STRIDE,
                                                             d - 1U, target_binding, 0);
                    Buffer stride = {0};
                    if (prior != NULL)
                        f2c_buffer_printf(&stride,
                                          "f2c_descriptor_stride_extent((ptrdiff_t)(%s), "
                                          "f2c_alloc_extent_%zu)",
                                          prior, d);
                    f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, d,
                                         target_binding, stride.data, depth + 2);
                    free(prior);
                    free(stride.data);
                }
            }
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        if (has_model) {
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_alloc_attempt && !f2c_alloc_ok) "
                                                "f2c_alloc_statement_ok = false;\n");
            f2c_allocation_failure(&allocation, "(!f2c_alloc_attempt || f2c_alloc_ok)",
                                   "allocation failed", depth + 1);
        } else {
            f2c_allocation_failure(&allocation, "f2c_alloc_ok", "allocation failed", depth + 1);
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(target_name);
        free(target_binding);
        target_name = NULL;
        target_binding = NULL;
    }
    f2c_allocation_model_emit_cleanup(context, unit, &model, depth);
    if (model.guarded) {
        --depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "} else {\n");
        f2c_allocation_failure(&allocation, model.availability,
                               "SOURCE/MOLD object is not allocated or associated", depth + 1);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_allocation_model_clear(unit, &model);
    const int emitted = f2c_allocation_context_finish(&allocation);
    f2c_allocation_context_clear(&allocation);
    return emitted;
failed:
    free(target_name);
    free(target_binding);
    f2c_allocation_model_clear(unit, &model);
    f2c_allocation_context_clear(&allocation);
    return 0;
}
