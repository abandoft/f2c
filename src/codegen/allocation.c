#include "codegen/allocation/private.h"
#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

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

static char *allocation_target_storage(Unit *unit, const F2cExpr *target, Buffer *prelude) {
    Buffer result = {0};
    char *base;
    if (target == NULL || target->symbol == NULL)
        return NULL;
    if (target->kind == F2C_EXPR_NAME || target->kind == F2C_EXPR_ARRAY_REFERENCE)
        return f2c_strdup(f2c_symbol_c_name(unit, target->symbol));
    if (target->kind != F2C_EXPR_COMPONENT || target->child_count == 0U || prelude == NULL ||
        target->children[0] == NULL || target->children[0]->derived_type == NULL)
        return NULL;
    int supported = 0;
    base = f2c_expression_storage_designator(unit, target->children[0], &supported);
    if (!supported) {
        free(base);
        return NULL;
    }
    if (base == NULL)
        return NULL;
    f2c_buffer_printf(prelude, "%s *f2c_allocation_object = &(%s);\n",
                      target->children[0]->derived_type->c_name, base);
    f2c_buffer_printf(&result, "f2c_allocation_object->%s",
                      f2c_symbol_c_name(unit, target->symbol));
    free(base);
    return f2c_buffer_take(&result);
}

static void allocation_store(Buffer *output, Unit *unit, const F2cStorageReference *reference,
                             F2cObjectStateProperty property, size_t dimension, const char *binding,
                             const char *value, int depth) {
    if (!f2c_storage_emit_store(output, unit, reference, property, dimension, binding, value,
                                depth))
        output->failed = 1;
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

typedef struct AllocationControls {
    char *status;
    char *message_pointer;
    char *message_length;
} AllocationControls;

static void allocation_controls_free(AllocationControls *controls) {
    if (controls == NULL)
        return;
    free(controls->status);
    free(controls->message_pointer);
    free(controls->message_length);
    memset(controls, 0, sizeof(*controls));
}

static int allocation_controls_init(Unit *unit, const F2cStatement *statement,
                                    AllocationControls *controls) {
    const F2cExpr *status_expression = keyword_value(statement, "stat");
    const F2cExpr *message_expression = keyword_value(statement, "errmsg");
    char *message_code = NULL;
    if (controls == NULL)
        return 0;
    memset(controls, 0, sizeof(*controls));
    if (status_expression != NULL) {
        controls->status = emit_expression(unit, status_expression);
        if (controls->status == NULL)
            return 0;
    }
    if (message_expression == NULL)
        return 1;
    message_code = emit_expression(unit, message_expression);
    controls->message_length = f2c_character_length_expression(unit, message_expression);
    controls->message_pointer =
        message_code != NULL ? f2c_character_source_pointer(unit, message_expression, message_code)
                             : NULL;
    free(message_code);
    if (controls->message_pointer == NULL || controls->message_length == NULL) {
        allocation_controls_free(controls);
        return 0;
    }
    return 1;
}

static void emit_operation_failure(Buffer *output, const char *success,
                                   const AllocationControls *controls, const char *message,
                                   int depth) {
    indent(output, depth);
    if (controls->status != NULL) {
        f2c_buffer_printf(output, "if (!%s) { %s = 1;", success, controls->status);
        if (controls->message_pointer != NULL && controls->message_length != NULL)
            f2c_buffer_printf(output, " f2c_store_message(%s, (size_t)(%s), \"%s\");",
                              controls->message_pointer, controls->message_length, message);
        f2c_buffer_append(output, " }\n");
    } else {
        f2c_buffer_printf(output, "if (!%s) abort();\n", success);
    }
}

int f2c_emit_allocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth) {
    const F2cExpr *source_expression = keyword_value(statement, "source");
    const F2cExpr *mold_expression = keyword_value(statement, "mold");
    const F2cExpr *model_expression =
        source_expression != NULL ? source_expression : mold_expression;
    AllocationControls controls;
    F2cAllocationModel model = {0};
    const int has_model = model_expression != NULL;
    const int outer_depth = depth;
    char *target_name = NULL;
    char *target_binding = NULL;
    size_t i;
    if (statement->arguments == NULL && statement->item_count != 0U)
        return 0;
    if (!allocation_controls_init(unit, statement, &controls))
        return 0;
    if (has_model) {
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
    }
    if (controls.status != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = 0;\n", controls.status);
    }
    if (!f2c_allocation_model_prepare(context, unit, statement, model_expression,
                                      source_expression != NULL, depth, &model)) {
        f2c_allocation_model_clear(unit, &model);
        allocation_controls_free(&controls);
        return 0;
    }
    if (model.guarded)
        ++depth;
    if (has_model) {
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "bool f2c_alloc_statement_ok = true;\n");
    }
    for (i = 0U; i < statement->item_count; ++i) {
        const F2cExpr *target = statement->arguments[i];
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
        const F2cStorageReference reference = f2c_ir_storage_reference(target);
        target_binding = allocation_target_storage(unit, target, &target_prelude);
        target_name =
            f2c_storage_bound_property(unit, &reference, F2C_OBJECT_DATA, 0U, target_binding, 1);
        if (target_binding == NULL || target_name == NULL) {
            free(target_prelude.data);
            goto failed;
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
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
            char *length =
                symbol->deferred_character
                    ? (statement->allocation_character_length != NULL
                           ? emit_expression(unit, statement->allocation_character_length)
                           : f2c_allocation_model_character_length(unit, &model))
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
                allocation_store(&context->output, unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U,
                                 target_binding, "true", depth + 2);
            }
            if (symbol->deferred_character) {
                allocation_store(&context->output, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH,
                                 0U, target_binding, "f2c_alloc_char_len", depth + 2);
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
                allocation_store(&context->output, unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U,
                                 target_binding, "true", depth + 2);
            }
        }
        for (d = 0U; d < symbol->rank; ++d) {
            Buffer lower = {0};
            Buffer extent = {0};
            f2c_buffer_printf(&lower, "(int32_t)f2c_alloc_lower_%zu", d + 1U);
            f2c_buffer_printf(&extent, "(int32_t)f2c_alloc_extent_%zu", d + 1U);
            allocation_store(&context->output, unit, &reference, F2C_OBJECT_LOWER, d,
                             target_binding, lower.data, depth + 2);
            allocation_store(&context->output, unit, &reference, F2C_OBJECT_EXTENT, d,
                             target_binding, extent.data, depth + 2);
            free(lower.data);
            free(extent.data);
            if (symbol->pointer || reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR) {
                if (d == 0U)
                    allocation_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, d,
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
                    allocation_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, d,
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
            emit_operation_failure(&context->output, "(!f2c_alloc_attempt || f2c_alloc_ok)",
                                   &controls, "allocation failed", depth + 1);
        } else {
            emit_operation_failure(&context->output, "f2c_alloc_ok", &controls, "allocation failed",
                                   depth + 1);
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
        emit_operation_failure(&context->output, model.availability, &controls,
                               "SOURCE/MOLD object is not allocated or associated", depth + 1);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_allocation_model_clear(unit, &model);
    if (has_model) {
        indent(&context->output, outer_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    allocation_controls_free(&controls);
    return !context->output.failed;
failed:
    free(target_name);
    free(target_binding);
    f2c_allocation_model_clear(unit, &model);
    allocation_controls_free(&controls);
    return 0;
}

int f2c_emit_deallocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth) {
    AllocationControls controls;
    char *target_name = NULL;
    char *target_binding = NULL;
    size_t i;
    if (statement->arguments == NULL && statement->item_count != 0U)
        return 0;
    if (!allocation_controls_init(unit, statement, &controls))
        return 0;
    if (controls.status != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = 0;\n", controls.status);
    }
    for (i = 0U; i < statement->item_count; ++i) {
        const F2cExpr *target = statement->arguments[i];
        Symbol *symbol;
        Buffer target_prelude = {0};
        size_t d;
        if (is_allocation_option(target))
            continue;
        symbol = target != NULL ? target->symbol : NULL;
        if (symbol == NULL || (!symbol->allocatable && !symbol->pointer))
            continue;
        const F2cStorageReference reference = f2c_ir_storage_reference(target);
        target_binding = allocation_target_storage(unit, target, &target_prelude);
        target_name =
            f2c_storage_bound_property(unit, &reference, F2C_OBJECT_DATA, 0U, target_binding, 1);
        if (target_binding == NULL || target_name == NULL) {
            free(target_name);
            free(target_binding);
            free(target_prelude.data);
            allocation_controls_free(&controls);
            return 0;
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        if (target_prelude.data != NULL) {
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, target_prelude.data);
        }
        free(target_prelude.data);
        indent(&context->output, depth + 1);
        if (symbol->pointer) {
            char *deallocatable = f2c_storage_bound_property(
                unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U, target_binding, 0);
            f2c_buffer_printf(&context->output,
                              "const bool f2c_dealloc_ok = %s != NULL && "
                              "%s;\n",
                              target_name, deallocatable != NULL ? deallocatable : "false");
            if (deallocatable == NULL)
                context->output.failed = 1;
            free(deallocatable);
        } else
            f2c_buffer_printf(&context->output, "const bool f2c_dealloc_ok = %s != NULL;\n",
                              target_name);
        indent(&context->output, depth + 1);
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            Buffer count = {0};
            for (d = 0U; d < symbol->rank; ++d) {
                char *extent = f2c_storage_bound_property(unit, &reference, F2C_OBJECT_EXTENT, d,
                                                          target_binding, 0);
                if (extent == NULL)
                    context->output.failed = 1;
                else
                    f2c_buffer_printf(&count, "%s(size_t)(%s)", d == 0U ? "" : " * ", extent);
                free(extent);
            }
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
        f2c_buffer_append(&context->output, "if (f2c_dealloc_ok) {\n");
        if (symbol->pointer || reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR)
            allocation_store(&context->output, unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U,
                             target_binding, "false", depth + 2);
        if (symbol->deferred_character) {
            allocation_store(&context->output, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH, 0U,
                             target_binding, "0U", depth + 2);
        }
        for (d = 0U; d < symbol->rank; ++d) {
            allocation_store(&context->output, unit, &reference, F2C_OBJECT_LOWER, d,
                             target_binding, "1", depth + 2);
            allocation_store(&context->output, unit, &reference, F2C_OBJECT_EXTENT, d,
                             target_binding, "0", depth + 2);
            if (symbol->pointer || reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR)
                allocation_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, d,
                                 target_binding, symbol->pointer || d != 0U ? "0" : "1", depth + 2);
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        emit_operation_failure(&context->output, "f2c_dealloc_ok", &controls,
                               "object is not deallocatable", depth + 1);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(target_name);
        free(target_binding);
    }
    allocation_controls_free(&controls);
    return !context->output.failed;
}
