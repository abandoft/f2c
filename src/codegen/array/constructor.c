#include "codegen/array/copy.h"
#include "codegen/array/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/loop/control.h"
#include "codegen/lowering/private.h"
#include "codegen/result/retention.h"
#include "codegen/storage/private.h"
#include "codegen/value/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConstructorSubstitution {
    const Symbol *symbol;
    const char *name;
    const char *replacement;
} ConstructorSubstitution;

typedef struct ConstructorEmitter {
    Context *context;
    Unit *unit;
    Symbol *target;
    const char *storage;
    const char *count;
    const char *index;
    const char *capacity;
    const char *character_length;
    const char *character_length_set;
    ConstructorSubstitution *substitutions;
    size_t substitution_count;
    size_t substitution_capacity;
    size_t next_temporary;
    F2cResultRetentionScope retention;
    int character;
    int dynamic;
    int infer_character_length;
} ConstructorEmitter;

static int push_constructor_substitution(ConstructorEmitter *emitter, const Symbol *symbol,
                                         const char *name, const char *replacement_text) {
    ConstructorSubstitution *replacement;
    size_t capacity;
    if (emitter->substitution_count == emitter->substitution_capacity) {
        capacity = emitter->substitution_capacity == 0U ? 8U : emitter->substitution_capacity * 2U;
        if (capacity < emitter->substitution_capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement = (ConstructorSubstitution *)realloc(emitter->substitutions,
                                                         capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        emitter->substitutions = replacement;
        emitter->substitution_capacity = capacity;
    }
    emitter->substitutions[emitter->substitution_count].symbol = symbol;
    emitter->substitutions[emitter->substitution_count].name = name;
    emitter->substitutions[emitter->substitution_count].replacement = replacement_text;
    ++emitter->substitution_count;
    return 1;
}

static void release_constructor_emitter(ConstructorEmitter *emitter) {
    f2c_result_retention_clear(&emitter->retention);
    free(emitter->substitutions);
    emitter->substitutions = NULL;
    emitter->substitution_count = 0U;
    emitter->substitution_capacity = 0U;
}

static void emit_constructor_capacity(ConstructorEmitter *emitter, int depth) {
    if (!emitter->dynamic) {
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "if (%s >= %s) abort();\n", emitter->index,
                          emitter->count);
        return;
    }
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output, "if (%s == %s) {\n", emitter->index,
                      emitter->capacity);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "size_t f2c_constructor_new_capacity = %s < 8U ? 8U : %s;\n",
                      emitter->capacity, emitter->capacity);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "if (%s >= 8U) {\n", emitter->capacity);
    f2c_array_indent(&emitter->context->output, depth + 2);
    f2c_buffer_append(&emitter->context->output,
                      "if (f2c_constructor_new_capacity > SIZE_MAX / 2U) abort();\n");
    f2c_array_indent(&emitter->context->output, depth + 2);
    f2c_buffer_append(&emitter->context->output, "f2c_constructor_new_capacity *= 2U;\n");
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "}\n");
    f2c_array_indent(&emitter->context->output, depth + 1);
    if (emitter->character) {
        f2c_buffer_printf(&emitter->context->output,
                          "if ((size_t)(%s) != 0U && f2c_constructor_new_capacity > "
                          "SIZE_MAX / (size_t)(%s)) abort();\n",
                          emitter->character_length, emitter->character_length);
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "const size_t f2c_constructor_new_bytes = "
                          "f2c_constructor_new_capacity * (size_t)(%s);\n",
                          emitter->character_length);
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "char *f2c_constructor_replacement = (char *)realloc(%s, "
                          "f2c_constructor_new_bytes == 0U ? 1U : "
                          "f2c_constructor_new_bytes);\n",
                          emitter->storage);
    } else {
        f2c_buffer_printf(&emitter->context->output,
                          "if (f2c_constructor_new_capacity > SIZE_MAX / sizeof(*%s)) abort();\n",
                          emitter->storage);
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "%s *f2c_constructor_replacement = (%s *)realloc(%s, "
                          "f2c_constructor_new_capacity * sizeof(*%s));\n",
                          f2c_symbol_c_type(emitter->target), f2c_symbol_c_type(emitter->target),
                          emitter->storage, emitter->storage);
    }
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output,
                      "if (f2c_constructor_replacement == NULL) abort();\n");
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = f2c_constructor_replacement;\n",
                      emitter->storage);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = f2c_constructor_new_capacity;\n",
                      emitter->capacity);
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
}

static int emit_constructor_character_length(ConstructorEmitter *emitter, const F2cExpr *expression,
                                             int depth) {
    char *length;
    const size_t temporary = emitter->next_temporary++;
    if (!emitter->character || !emitter->infer_character_length)
        return 1;
    length = f2c_character_length_expression(emitter->unit, expression);
    if (length == NULL)
        return 0;
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "const size_t f2c_constructor_item_length_%zu = (size_t)(%s);\n", temporary,
                      length);
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output, "if (!%s) {\n", emitter->character_length_set);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = f2c_constructor_item_length_%zu;\n",
                      emitter->character_length, temporary);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = true;\n", emitter->character_length_set);
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "} else if (%s != f2c_constructor_item_length_%zu) {\n",
                      emitter->character_length, temporary);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "abort();\n");
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    free(length);
    return 1;
}

static const char *constructor_substitution(const ConstructorEmitter *emitter,
                                            const F2cExpr *expression) {
    size_t i;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME)
        return NULL;
    for (i = emitter->substitution_count; i != 0U; --i) {
        const ConstructorSubstitution *substitution = &emitter->substitutions[i - 1U];
        if ((expression->symbol != NULL && expression->symbol == substitution->symbol) ||
            (expression->text != NULL && substitution->name != NULL &&
             strcmp(expression->text, substitution->name) == 0))
            return substitution->replacement;
    }
    return NULL;
}

static F2cExpr *clone_constructor_expression(const ConstructorEmitter *emitter,
                                             const F2cExpr *expression) {
    F2cExpr *clone;
    const char *replacement;
    size_t i;
    if (expression == NULL)
        return NULL;
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    *clone = *expression;
    clone->text = expression->text != NULL ? f2c_strdup(expression->text) : NULL;
    clone->source = expression->source != NULL ? f2c_strdup(expression->source) : NULL;
    replacement = constructor_substitution(emitter, expression);
    clone->children = NULL;
    clone->child_count = 0U;
    clone->child_capacity = 0U;
    if ((expression->text != NULL && clone->text == NULL) ||
        (expression->source != NULL && clone->source == NULL) ||
        !f2c_lowering_clone(emitter->unit, clone, expression) ||
        (replacement != NULL && !f2c_lowering_copy_code(emitter->unit, clone, replacement)))
        goto failed;
    if (replacement != NULL) {
        clone->kind = F2C_EXPR_NAME;
        clone->type = TYPE_INTEGER;
        clone->rank = 0U;
        clone->definable = 1;
        clone->value_category = F2C_VALUE_VARIABLE;
        /* The ac-do-variable is a construct entity with its inherited kind but
         * no storage attributes of the same-named enclosing object. */
        clone->storage_qualifiers = 0U;
        clone->symbol = NULL;
        return clone;
    }
    if (expression->child_count != 0U) {
        clone->children = (F2cExpr **)calloc(expression->child_count, sizeof(*clone->children));
        if (clone->children == NULL)
            goto failed;
        clone->child_capacity = expression->child_count;
        for (i = 0U; i < expression->child_count; ++i) {
            clone->children[i] = clone_constructor_expression(emitter, expression->children[i]);
            if (clone->children[i] == NULL)
                goto failed;
            ++clone->child_count;
        }
    }
    return clone;

failed:
    f2c_codegen_expression_free(emitter->unit, clone);
    return NULL;
}

static int emit_constructor_value(ConstructorEmitter *emitter, const F2cExpr *expression,
                                  int depth);

static int prepare_constructor_expression(ConstructorEmitter *emitter, F2cExpr *expression,
                                          int depth) {
    F2cArrayCleanupList cleanup = {0};
    int success = 1;
    if (f2c_array_contains_unmaterialized_value(emitter->unit, expression))
        success = f2c_array_materialize_constructors(emitter->context, emitter->unit, expression,
                                                     expression->lifetime_statement_index,
                                                     "constructor_item", &emitter->next_temporary,
                                                     &emitter->context->output, &cleanup, depth) &&
                  f2c_result_retention_capture(&emitter->retention, &cleanup,
                                               &emitter->context->output, depth);
    if (!success)
        f2c_diagnostic_span_code(emitter->context, F2C_DIAGNOSTIC_INTERNAL, &expression->span, 1,
                                 "constructor item could not retain its typed result instances");
    f2c_array_cleanup_clear(&cleanup);
    return success;
}

static int emit_constructor_scalar(ConstructorEmitter *emitter, const F2cExpr *expression,
                                   int depth) {
    F2cExpr *substituted = clone_constructor_expression(emitter, expression);
    char *code = NULL;
    char *converted = NULL;
    Buffer target = {0};
    int result = 0;
    if (substituted == NULL)
        goto cleanup;
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "{\n");
    ++depth;
    if (!prepare_constructor_expression(emitter, substituted, depth) ||
        (code = f2c_array_emit_expression(emitter->unit, substituted)) == NULL)
        goto cleanup;
    if ((emitter->character && substituted->type != TYPE_CHARACTER) ||
        (!emitter->character && substituted->type == TYPE_CHARACTER))
        goto cleanup;
    if (emitter->target->type == TYPE_DERIVED &&
        (substituted->type != TYPE_DERIVED || substituted->derived_type == NULL ||
         substituted->derived_type != emitter->target->derived_type))
        goto cleanup;
    if (!emit_constructor_character_length(emitter, substituted, depth))
        goto cleanup;
    emit_constructor_capacity(emitter, depth);
    if (emitter->character) {
        f2c_buffer_printf(&target, "%s + %s * %s", emitter->storage, emitter->index,
                          emitter->character_length);
        if (!f2c_emit_character_storage_assignment(emitter->context, emitter->unit, target.data,
                                                   emitter->character_length, substituted, code,
                                                   depth))
            goto cleanup;
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "++%s;\n", emitter->index);
    } else if (emitter->target->type == TYPE_DERIVED) {
        const size_t temporary = emitter->next_temporary++;
        Buffer destination = {0};
        f2c_buffer_printf(&destination, "%s[%s]", emitter->storage, emitter->index);
        if (destination.data == NULL || !f2c_emit_derived_clone_expression(
                                            &emitter->context->output, emitter->unit, substituted,
                                            destination.data, "constructor", temporary, depth)) {
            free(destination.data);
            goto cleanup;
        }
        free(destination.data);
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "++%s;\n", emitter->index);
    } else {
        converted = f2c_emit_numeric_conversion(code, substituted->type, emitter->target->type);
        if (converted == NULL)
            goto cleanup;
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "%s[%s++] = %s;\n", emitter->storage,
                          emitter->index, converted);
    }
    result = 1;
    f2c_array_indent(&emitter->context->output, depth - 1);
    f2c_buffer_append(&emitter->context->output, "}\n");

cleanup:
    free(f2c_buffer_take(&target));
    free(converted);
    free(code);
    f2c_codegen_expression_free(emitter->unit, substituted);
    return result;
}

static int emit_constructor_whole_array(ConstructorEmitter *emitter, const F2cExpr *expression,
                                        int depth) {
    F2cExpr *source = clone_constructor_expression(emitter, expression);
    F2cExpr *element = NULL;
    Buffer extent_names = {0};
    Buffer stride = {0};
    Buffer ordinal_codes[F2C_MAX_RANK] = {{0}};
    const char *ordinals[F2C_MAX_RANK] = {0};
    const size_t temporary = emitter->next_temporary++;
    int success = 0;
    if (source == NULL || source->rank == 0U || source->rank > F2C_MAX_RANK)
        goto cleanup;
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "{\n");
    ++depth;
    if (!prepare_constructor_expression(emitter, source, depth))
        goto cleanup;
    f2c_buffer_append(&stride, "1U");
    for (size_t dimension = 0U; dimension < source->rank; ++dimension) {
        char *extent = f2c_array_expression_extent(emitter->unit, source, dimension);
        if (extent == NULL)
            goto cleanup;
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output,
                          "const size_t f2c_constructor_extent_%zu_%zu = (size_t)(%s);\n",
                          temporary, dimension, extent);
        free(extent);
        f2c_buffer_printf(&extent_names, "%sf2c_constructor_extent_%zu_%zu",
                          dimension == 0U ? "" : ", ", temporary, dimension);
        f2c_buffer_printf(&ordinal_codes[dimension],
                          "((f2c_constructor_source_%zu / (%s)) %% "
                          "f2c_constructor_extent_%zu_%zu)",
                          temporary, stride.data, temporary, dimension);
        ordinals[dimension] = ordinal_codes[dimension].data;
        f2c_buffer_printf(&stride, " * f2c_constructor_extent_%zu_%zu", temporary, dimension);
    }
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "const size_t f2c_constructor_source_count_%zu = "
                      "f2c_inquiry_size(%zuU, (const size_t[]){%s});\n",
                      temporary, source->rank, extent_names.data);
    element = f2c_array_element_expression(emitter->unit, source, source->rank, ordinals);
    if (element == NULL)
        goto cleanup;
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "for (size_t f2c_constructor_source_%zu = 0U; "
                      "f2c_constructor_source_%zu < f2c_constructor_source_count_%zu; "
                      "++f2c_constructor_source_%zu) {\n",
                      temporary, temporary, temporary, temporary);
    if (!emit_constructor_scalar(emitter, element, depth + 1))
        goto cleanup;
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    f2c_array_indent(&emitter->context->output, depth - 1);
    f2c_buffer_append(&emitter->context->output, "}\n");
    success = 1;

cleanup:
    for (size_t dimension = 0U; dimension < F2C_MAX_RANK; ++dimension)
        free(ordinal_codes[dimension].data);
    free(extent_names.data);
    free(stride.data);
    f2c_codegen_expression_free(emitter->unit, element);
    f2c_codegen_expression_free(emitter->unit, source);
    return success;
}

static int emit_constructor_implied_do(ConstructorEmitter *emitter, const F2cExpr *expression,
                                       int depth) {
    F2cExpr *controls[3] = {NULL, NULL, NULL};
    const char *roles[3] = {"start", "limit", "step"};
    Buffer iterator_name = {0};
    const size_t value_count = expression->child_count >= 3U ? expression->child_count - 3U : 0U;
    const size_t temporary = emitter->next_temporary++;
    const char *c_type = expression->symbol != NULL ? f2c_symbol_c_type(expression->symbol) : NULL;
    const char *suffix =
        expression->symbol != NULL ? f2c_integer_loop_suffix(expression->symbol->kind) : NULL;
    Buffer advance = {0};
    char *prefix = f2c_loop_local_prefix(emitter->unit, "f2c_constructor", temporary);
    int result = 0;
    if (value_count == 0U || c_type == NULL || suffix == NULL || prefix == NULL)
        goto cleanup;
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "{\n");
    for (size_t index = 0U; index < 3U; ++index) {
        char *code;
        controls[index] =
            clone_constructor_expression(emitter, expression->children[value_count + index]);
        if (controls[index] == NULL ||
            !prepare_constructor_expression(emitter, controls[index], depth + 1))
            goto cleanup;
        code = f2c_array_emit_expression(emitter->unit, controls[index]);
        if (code == NULL)
            goto cleanup;
        f2c_loop_emit_parameter(&emitter->context->output, prefix, temporary, roles[index],
                                expression->symbol->kind, controls[index], code, depth + 1);
        free(code);
    }
    f2c_loop_emit_state(&emitter->context->output, prefix, temporary, NULL, 0, depth + 1);
    f2c_buffer_printf(&iterator_name, "%s_value_%zu", prefix, temporary);
    if (iterator_name.failed)
        goto cleanup;
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s %s = %s_start_%zu;\n", c_type,
                      iterator_name.data, prefix, temporary);
    f2c_buffer_printf(&advance, "%s = F2C_LOOP_%s(%s, %s_step_%zu)", iterator_name.data, suffix,
                      iterator_name.data, prefix, temporary);
    if (advance.failed)
        goto cleanup;
    f2c_loop_emit_header(&emitter->context->output, prefix, temporary, advance.data, NULL,
                         depth + 1);
    if (!push_constructor_substitution(emitter, expression->symbol, expression->text,
                                       iterator_name.data))
        goto cleanup;
    for (size_t index = 0U; index < value_count; ++index) {
        if (!emit_constructor_value(emitter, expression->children[index], depth + 2)) {
            --emitter->substitution_count;
            goto cleanup;
        }
    }
    --emitter->substitution_count;
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "}\n");
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    result = !emitter->context->output.failed;

cleanup:
    free(advance.data);
    free(iterator_name.data);
    free(prefix);
    for (size_t index = 0U; index < 3U; ++index)
        f2c_codegen_expression_free(emitter->unit, controls[index]);
    return result;
}

static int emit_constructor_value(ConstructorEmitter *emitter, const F2cExpr *expression,
                                  int depth) {
    size_t i;
    if (expression == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (i = 0U; i < expression->child_count; ++i) {
            if (!emit_constructor_value(emitter, expression->children[i], depth))
                return 0;
        }
        return 1;
    }
    if (expression->kind == F2C_EXPR_IMPLIED_DO)
        return emit_constructor_implied_do(emitter, expression, depth);
    if (expression->rank != 0U)
        return emit_constructor_whole_array(emitter, expression, depth);
    return emit_constructor_scalar(emitter, expression, depth);
}

int f2c_array_emit_constructor_values(Context *context, Unit *unit, Symbol *target,
                                      const F2cExpr *constructor, const char *storage,
                                      const char *count, const char *capacity,
                                      const char *character_length,
                                      const char *character_length_set, int character, int dynamic,
                                      int infer_character_length,
                                      F2cResultRetentionScope *retention, int depth) {
    ConstructorEmitter emitter;
    int result;
    if (context == NULL || unit == NULL || target == NULL || constructor == NULL ||
        storage == NULL || count == NULL || retention == NULL)
        return 0;
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = target;
    emitter.storage = storage;
    emitter.index = count;
    emitter.capacity = capacity;
    emitter.character_length = character_length;
    emitter.character_length_set = character_length_set;
    emitter.character = character;
    emitter.dynamic = dynamic;
    emitter.infer_character_length = infer_character_length;
    result = f2c_result_retention_begin(&emitter.retention, unit, constructor, storage,
                                        &context->output, depth) &&
             emit_constructor_value(&emitter, constructor, depth);
    if (result) {
        *retention = emitter.retention;
        memset(&emitter.retention, 0, sizeof(emitter.retention));
    }
    release_constructor_emitter(&emitter);
    return result;
}

int f2c_array_emit_numeric_constructor(Context *context, Unit *unit, Symbol *left_symbol,
                                       const F2cExpr *constructor, const char *element_count,
                                       int depth) {
    const size_t output_start = context->output.length;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR ||
        element_count == NULL)
        return 0;
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = left_symbol;
    emitter.storage = "f2c_constructor_values";
    emitter.count = "f2c_constructor_count";
    emitter.index = "f2c_constructor_index";
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_constructor_count = (size_t)(%s);\n",
                      element_count);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_constructor_count > SIZE_MAX / sizeof(%s)) abort();\n",
                      f2c_symbol_c_type(left_symbol));
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_constructor_values = f2c_constructor_count == 0U ? NULL : "
                      "(%s *)malloc(f2c_constructor_count * "
                      "sizeof(*f2c_constructor_values));\n",
                      f2c_symbol_c_type(left_symbol), f2c_symbol_c_type(left_symbol));
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_count != 0U && "
                                        "f2c_constructor_values == NULL) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_index = 0U;\n");
    if (!f2c_result_retention_begin(&emitter.retention, unit, constructor, emitter.storage,
                                    &context->output, depth + 1) ||
        !emit_constructor_value(&emitter, constructor, depth + 1)) {
        if (context->output.data != NULL && output_start <= context->output.length) {
            context->output.length = output_start;
            context->output.data[output_start] = '\0';
        }
        release_constructor_emitter(&emitter);
        return 0;
    }
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index != f2c_constructor_count) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    if (left_symbol->type == TYPE_DERIVED && left_symbol->derived_type != NULL) {
        f2c_buffer_printf(&context->output,
                          "f2c_destroy_array_%s(%s, f2c_constructor_count, %zuU);\n",
                          left_symbol->derived_type->c_name, f2c_symbol_c_name(unit, left_symbol),
                          left_symbol->rank);
        f2c_array_indent(&context->output, depth + 1);
    }
    f2c_array_copy_to_symbol(&context->output, unit, left_symbol, "f2c_constructor_values",
                             "f2c_constructor_count", 0);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    if (!f2c_result_retention_release(&emitter.retention, &context->output, depth + 1)) {
        release_constructor_emitter(&emitter);
        return 0;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    release_constructor_emitter(&emitter);
    return 1;
}

static int emit_allocatable_numeric_constructor(Context *context, Unit *unit, Symbol *target,
                                                const F2cExpr *constructor,
                                                const F2cStorageReference *reference,
                                                const char *binding, int depth) {
    const size_t output_start = context->output.length;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || target == NULL ||
        binding == NULL || !target->allocatable || target->rank != 1U ||
        target->type == TYPE_CHARACTER)
        return 0;
    char *name = f2c_storage_bound_property(unit, reference, F2C_OBJECT_DATA, 0U, binding, 1);
    char *extent = f2c_storage_bound_property(unit, reference, F2C_OBJECT_EXTENT, 0U, binding, 0);
    if (name == NULL || extent == NULL) {
        free(name);
        free(extent);
        return 0;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = target;
    emitter.storage = "f2c_constructor_values";
    emitter.index = "f2c_constructor_index";
    emitter.capacity = "f2c_constructor_capacity";
    emitter.dynamic = 1;

    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s *f2c_constructor_values = NULL;\n",
                      f2c_symbol_c_type(target));
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_index = 0U;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_capacity = 0U;\n");
    if (!f2c_result_retention_begin(&emitter.retention, unit, constructor, emitter.storage,
                                    &context->output, depth + 1) ||
        !emit_constructor_value(&emitter, constructor, depth + 1))
        goto failed;
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index > (size_t)INT32_MAX) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_constructor_reallocate = %s == NULL || "
                      "(size_t)(%s) != f2c_constructor_index;\n",
                      name, extent);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_reallocate) {\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) {\n");
    f2c_array_indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output,
                      "f2c_constructor_values = malloc(sizeof(*f2c_constructor_values));\n");
    f2c_array_indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) abort();\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 2);
    if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_buffer_printf(&context->output,
                          "if (%s != NULL) f2c_destroy_array_%s(%s, "
                          "(size_t)(%s), 1U);\n",
                          name, target->derived_type->c_name, name, extent);
        f2c_array_indent(&context->output, depth + 2);
    }
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s = f2c_constructor_values;\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "f2c_constructor_values = NULL;\n");
    if (!f2c_storage_emit_contiguous_dimension(&context->output, unit, reference, 0U, binding, "1",
                                               "(int32_t)f2c_constructor_index", depth + 2))
        goto failed;
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else if (f2c_constructor_index != 0U) {\n");
    f2c_array_indent(&context->output, depth + 2);
    if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_buffer_printf(&context->output,
                          "f2c_destroy_array_%s(%s, f2c_constructor_index, 1U);\n",
                          target->derived_type->c_name, name);
        f2c_array_indent(&context->output, depth + 2);
    }
    f2c_buffer_printf(&context->output,
                      "memmove(%s, f2c_constructor_values, f2c_constructor_index * "
                      "sizeof(*f2c_constructor_values));\n",
                      name);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    if (!f2c_result_retention_release(&emitter.retention, &context->output, depth + 1))
        goto failed;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    release_constructor_emitter(&emitter);
    free(name);
    free(extent);
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    release_constructor_emitter(&emitter);
    free(name);
    free(extent);
    return 0;
}

int f2c_array_emit_allocatable_numeric_constructor(Context *context, Unit *unit, Symbol *target,
                                                   const F2cExpr *constructor, int depth) {
    if (context == NULL || unit == NULL || target == NULL)
        return 0;
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(target);
    return emit_allocatable_numeric_constructor(context, unit, target, constructor, &reference,
                                                f2c_symbol_c_name(unit, target), depth);
}

int f2c_array_emit_allocatable_component_constructor(Context *context, Unit *unit,
                                                     const F2cExpr *target,
                                                     const F2cExpr *constructor, int depth) {
    Symbol *component = target != NULL ? target->symbol : NULL;
    char *designator;
    int supported = 0;
    int result;
    if (context == NULL || unit == NULL || target == NULL || target->kind != F2C_EXPR_COMPONENT ||
        component == NULL || !component->allocatable || component->rank != 1U ||
        component->type == TYPE_CHARACTER || constructor == NULL ||
        constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR)
        return 0;
    designator = f2c_descriptor_storage_designator(unit, target);
    supported = designator != NULL;
    if (!supported || designator == NULL) {
        free(designator);
        return 0;
    }
    const F2cStorageReference reference = f2c_ir_storage_reference(target);
    result = emit_allocatable_numeric_constructor(context, unit, component, constructor, &reference,
                                                  designator, depth);
    free(designator);
    return result;
}

int f2c_array_emit_allocatable_character_constructor(Context *context, Unit *unit, Symbol *target,
                                                     const F2cExpr *constructor, int depth) {
    const size_t output_start = context->output.length;
    char *name;
    char *extent;
    char *fixed_length = NULL;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || target == NULL ||
        !target->allocatable || target->rank != 1U || target->type != TYPE_CHARACTER)
        return 0;
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(target);
    if (!target->deferred_character) {
        fixed_length = f2c_symbol_character_length(unit, target);
        if (fixed_length == NULL)
            return 0;
    }
    name = f2c_storage_write_property(unit, &reference, F2C_OBJECT_DATA, 0U);
    extent = f2c_symbol_dimension_extent(unit, target, 0U);
    if (name == NULL || extent == NULL) {
        free(name);
        free(extent);
        free(fixed_length);
        return 0;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = target;
    emitter.storage = "f2c_constructor_values";
    emitter.index = "f2c_constructor_index";
    emitter.capacity = "f2c_constructor_capacity";
    emitter.character_length = "f2c_constructor_character_length";
    emitter.character_length_set = "f2c_constructor_character_length_set";
    emitter.character = 1;
    emitter.dynamic = 1;
    emitter.infer_character_length = target->deferred_character;

    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "char *f2c_constructor_values = NULL;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_index = 0U;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_capacity = 0U;\n");
    f2c_array_indent(&context->output, depth + 1);
    if (target->deferred_character) {
        f2c_buffer_append(&context->output, "size_t f2c_constructor_character_length = 0U;\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "bool f2c_constructor_character_length_set = false;\n");
    } else {
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_constructor_character_length = (size_t)(%s);\n",
                          fixed_length);
    }
    if (!f2c_result_retention_begin(&emitter.retention, unit, constructor, emitter.storage,
                                    &context->output, depth + 1) ||
        !emit_constructor_value(&emitter, constructor, depth + 1))
        goto failed;
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index > (size_t)INT32_MAX) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_character_length != 0U && "
                                        "f2c_constructor_index > SIZE_MAX / "
                                        "f2c_constructor_character_length) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "const size_t f2c_constructor_bytes = f2c_constructor_index * "
                      "f2c_constructor_character_length;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_constructor_reallocate = %s == NULL || "
                      "(size_t)(%s) != f2c_constructor_index",
                      name, extent);
    if (target->deferred_character) {
        char *length = f2c_symbol_character_length(unit, target);
        if (length == NULL)
            goto failed;
        f2c_buffer_printf(&context->output, " || (%s) != f2c_constructor_character_length", length);
        free(length);
    }
    f2c_buffer_append(&context->output, ";\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_reallocate) {\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) {\n");
    f2c_array_indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "f2c_constructor_values = (char *)malloc(1U);\n");
    f2c_array_indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) abort();\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s = f2c_constructor_values;\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "f2c_constructor_values = NULL;\n");
    if (!f2c_storage_emit_contiguous_dimension(&context->output, unit, &reference, 0U, NULL, "1",
                                               "(int32_t)f2c_constructor_index", depth + 2))
        goto failed;
    if (target->deferred_character) {
        if (!f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH,
                                    0U, NULL, "f2c_constructor_character_length", depth + 2))
            goto failed;
    }
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else if (f2c_constructor_bytes != 0U) {\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output,
                      "memmove(%s, f2c_constructor_values, f2c_constructor_bytes);\n", name);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    if (!f2c_result_retention_release(&emitter.retention, &context->output, depth + 1))
        goto failed;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(fixed_length);
    free(name);
    free(extent);
    release_constructor_emitter(&emitter);
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(fixed_length);
    free(name);
    free(extent);
    release_constructor_emitter(&emitter);
    return 0;
}

int f2c_array_emit_fixed_character_constructor_values(Context *context, Unit *unit, Symbol *target,
                                                      const F2cExpr *constructor,
                                                      const char *storage, const char *count,
                                                      const char *character_length,
                                                      F2cResultRetentionScope *retention,
                                                      int depth) {
    ConstructorEmitter emitter;
    int success;
    if (context == NULL || unit == NULL || target == NULL || constructor == NULL ||
        storage == NULL || count == NULL || character_length == NULL || retention == NULL)
        return 0;
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = target;
    emitter.storage = storage;
    emitter.count = count;
    emitter.index = "f2c_character_constructor_index";
    emitter.character_length = character_length;
    emitter.character = 1;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_character_constructor_index = 0U;\n");
    success = f2c_result_retention_begin(&emitter.retention, unit, constructor, storage,
                                         &context->output, depth) &&
              emit_constructor_value(&emitter, constructor, depth);
    if (success) {
        *retention = emitter.retention;
        memset(&emitter.retention, 0, sizeof(emitter.retention));
    }
    release_constructor_emitter(&emitter);
    if (success) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_character_constructor_index != %s) abort();\n",
                          count);
    }
    return success;
}
