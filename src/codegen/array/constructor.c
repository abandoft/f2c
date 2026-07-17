#include "codegen/array/private.h"

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
    f2c_buffer_append(&emitter->context->output, "if (f2c_constructor_new_capacity != 8U) {\n");
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
    clone->lowered_c =
        replacement != NULL
            ? f2c_strdup(replacement)
            : (expression->lowered_c != NULL ? f2c_strdup(expression->lowered_c) : NULL);
    clone->lowered_extent_c =
        expression->lowered_extent_c != NULL ? f2c_strdup(expression->lowered_extent_c) : NULL;
    clone->lowered_character_length_c = expression->lowered_character_length_c != NULL
                                            ? f2c_strdup(expression->lowered_character_length_c)
                                            : NULL;
    clone->children = NULL;
    clone->child_count = 0U;
    clone->child_capacity = 0U;
    if ((expression->text != NULL && clone->text == NULL) ||
        (expression->source != NULL && clone->source == NULL) ||
        ((replacement != NULL || expression->lowered_c != NULL) && clone->lowered_c == NULL) ||
        (expression->lowered_extent_c != NULL && clone->lowered_extent_c == NULL) ||
        (expression->lowered_character_length_c != NULL &&
         clone->lowered_character_length_c == NULL))
        goto failed;
    if (replacement != NULL) {
        clone->kind = F2C_EXPR_NAME;
        clone->type = TYPE_INTEGER;
        clone->rank = 0U;
        clone->definable = 1;
        clone->value_category = F2C_VALUE_VARIABLE;
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
    f2c_expr_free(clone);
    return NULL;
}

static int emit_constructor_value(ConstructorEmitter *emitter, const F2cExpr *expression,
                                  int depth);

static int emit_constructor_scalar(ConstructorEmitter *emitter, const F2cExpr *expression,
                                   int depth) {
    F2cExpr *substituted = clone_constructor_expression(emitter, expression);
    char *code = substituted != NULL ? f2c_array_emit_expression(emitter->unit, substituted) : NULL;
    Buffer target = {0};
    int result = 0;
    if (substituted == NULL || code == NULL)
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
        const char *name = emitter->target->derived_type->c_name;
        const size_t temporary = emitter->next_temporary++;
        if (substituted->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR ||
            substituted->kind == F2C_EXPR_CALL) {
            f2c_array_indent(&emitter->context->output, depth);
            f2c_buffer_printf(&emitter->context->output, "%s f2c_constructor_derived_%zu = %s;\n",
                              name, temporary, code);
            if (substituted->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
                f2c_array_indent(&emitter->context->output, depth);
                f2c_buffer_printf(&emitter->context->output,
                                  "f2c_initialize_%s(&f2c_constructor_derived_%zu);\n", name,
                                  temporary);
            }
            f2c_array_indent(&emitter->context->output, depth);
            f2c_buffer_printf(&emitter->context->output,
                              "f2c_clone_%s(&%s[%s], &f2c_constructor_derived_%zu);\n", name,
                              emitter->storage, emitter->index, temporary);
            f2c_array_indent(&emitter->context->output, depth);
            f2c_buffer_printf(&emitter->context->output,
                              "f2c_destroy_%s(&f2c_constructor_derived_%zu);\n", name, temporary);
        } else {
            f2c_array_indent(&emitter->context->output, depth);
            f2c_buffer_printf(&emitter->context->output, "f2c_clone_%s(&%s[%s], &(%s));\n", name,
                              emitter->storage, emitter->index, code);
        }
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "++%s;\n", emitter->index);
    } else {
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "%s[%s++] = (%s)(%s);\n", emitter->storage,
                          emitter->index, f2c_symbol_c_type(emitter->target), code);
    }
    result = 1;

cleanup:
    free(f2c_buffer_take(&target));
    free(code);
    f2c_expr_free(substituted);
    return result;
}

static int emit_constructor_whole_array(ConstructorEmitter *emitter, const F2cExpr *expression,
                                        int depth) {
    Symbol *source = expression != NULL ? expression->symbol : NULL;
    char *source_count;
    char *source_length = NULL;
    const size_t temporary = emitter->next_temporary++;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME || source == NULL ||
        source->rank == 0U)
        return 0;
    source_count = f2c_symbol_element_count(emitter->unit, source);
    if (source_count == NULL || *source_count == '\0') {
        free(source_count);
        return 0;
    }
    if (emitter->character) {
        if (source->type != TYPE_CHARACTER) {
            free(source_count);
            return 0;
        }
        source_length = f2c_symbol_character_length(emitter->unit, source);
        if (source_length == NULL) {
            free(source_count);
            return 0;
        }
    }
    if (source->allocatable || source->pointer) {
        f2c_array_indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "if (%s == NULL) abort();\n",
                          f2c_symbol_c_name(emitter->unit, source));
    }
    if (!emit_constructor_character_length(emitter, expression, depth)) {
        free(source_length);
        free(source_count);
        return 0;
    }
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "for (size_t f2c_constructor_source_%zu = 0U; "
                      "f2c_constructor_source_%zu < (size_t)(%s); "
                      "++f2c_constructor_source_%zu) {\n",
                      temporary, temporary, source_count, temporary);
    emit_constructor_capacity(emitter, depth + 1);
    if (emitter->character) {
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "const size_t f2c_constructor_copy_%zu = "
                          "F2C_MIN((size_t)(%s), (size_t)(%s));\n",
                          temporary, emitter->character_length, source_length);
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "if (f2c_constructor_copy_%zu != 0U) "
                          "memmove(%s + %s * %s, %s + f2c_constructor_source_%zu * "
                          "(size_t)(%s), f2c_constructor_copy_%zu);\n",
                          temporary, emitter->storage, emitter->index, emitter->character_length,
                          f2c_symbol_c_name(emitter->unit, source), temporary, source_length,
                          temporary);
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "if ((size_t)(%s) > f2c_constructor_copy_%zu) "
                          "memset(%s + %s * %s + f2c_constructor_copy_%zu, ' ', "
                          "(size_t)(%s) - f2c_constructor_copy_%zu);\n",
                          emitter->character_length, temporary, emitter->storage, emitter->index,
                          emitter->character_length, temporary, emitter->character_length,
                          temporary);
        f2c_array_indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output, "++%s;\n", emitter->index);
    } else {
        if (emitter->target->type == TYPE_DERIVED && source->type == TYPE_DERIVED &&
            source->derived_type == emitter->target->derived_type) {
            f2c_array_indent(&emitter->context->output, depth + 1);
            f2c_buffer_printf(&emitter->context->output,
                              "f2c_clone_%s(&%s[%s++], &%s["
                              "f2c_constructor_source_%zu]);\n",
                              emitter->target->derived_type->c_name, emitter->storage,
                              emitter->index, f2c_symbol_c_name(emitter->unit, source), temporary);
        } else if (!f2c_type_is_numeric(source->type) && source->type != TYPE_LOGICAL) {
            free(source_length);
            free(source_count);
            return 0;
        } else {
            f2c_array_indent(&emitter->context->output, depth + 1);
            f2c_buffer_printf(&emitter->context->output,
                              "%s[%s++] = (%s)%s["
                              "f2c_constructor_source_%zu];\n",
                              emitter->storage, emitter->index, f2c_symbol_c_type(emitter->target),
                              f2c_symbol_c_name(emitter->unit, source), temporary);
        }
    }
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    free(source_length);
    free(source_count);
    return 1;
}

static int emit_constructor_implied_do(ConstructorEmitter *emitter, const F2cExpr *expression,
                                       int depth) {
    F2cExpr *initial_expression = NULL;
    F2cExpr *limit_expression = NULL;
    F2cExpr *step_expression = NULL;
    char *initial = NULL;
    char *limit = NULL;
    char *step = NULL;
    char iterator_name[64];
    const size_t value_count = expression->child_count >= 3U ? expression->child_count - 3U : 0U;
    const size_t temporary = emitter->next_temporary++;
    size_t i;
    int result = 0;
    if (value_count == 0U)
        return 0;
    initial_expression = clone_constructor_expression(emitter, expression->children[value_count]);
    limit_expression =
        clone_constructor_expression(emitter, expression->children[value_count + 1U]);
    step_expression = clone_constructor_expression(emitter, expression->children[value_count + 2U]);
    initial = initial_expression != NULL
                  ? f2c_array_emit_expression(emitter->unit, initial_expression)
                  : NULL;
    limit = limit_expression != NULL ? f2c_array_emit_expression(emitter->unit, limit_expression)
                                     : NULL;
    step =
        step_expression != NULL ? f2c_array_emit_expression(emitter->unit, step_expression) : NULL;
    if (initial == NULL || limit == NULL || step == NULL)
        goto cleanup;
    (void)snprintf(iterator_name, sizeof(iterator_name), "f2c_constructor_value_%zu", temporary);
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "{\n");
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const int64_t f2c_constructor_first_%zu = (int64_t)(%s);\n", temporary,
                      initial);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const int64_t f2c_constructor_last_%zu = (int64_t)(%s);\n", temporary,
                      limit);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const int64_t f2c_constructor_step_%zu = (int64_t)(%s);\n", temporary, step);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "if (f2c_constructor_step_%zu == 0) abort();\n",
                      temporary);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const uint64_t f2c_constructor_iterations_%zu = "
                      "f2c_constructor_step_%zu > 0 ? "
                      "(f2c_constructor_first_%zu <= f2c_constructor_last_%zu ? "
                      "(uint64_t)((f2c_constructor_last_%zu - f2c_constructor_first_%zu) / "
                      "f2c_constructor_step_%zu) + UINT64_C(1) : UINT64_C(0)) : "
                      "(f2c_constructor_first_%zu >= f2c_constructor_last_%zu ? "
                      "(uint64_t)((f2c_constructor_first_%zu - f2c_constructor_last_%zu) / "
                      "(-f2c_constructor_step_%zu)) + UINT64_C(1) : UINT64_C(0));\n",
                      temporary, temporary, temporary, temporary, temporary, temporary, temporary,
                      temporary, temporary, temporary, temporary, temporary);
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "for (uint64_t f2c_constructor_iteration_%zu = UINT64_C(0); "
                      "f2c_constructor_iteration_%zu < f2c_constructor_iterations_%zu; "
                      "++f2c_constructor_iteration_%zu) {\n",
                      temporary, temporary, temporary, temporary);
    f2c_array_indent(&emitter->context->output, depth + 2);
    f2c_buffer_printf(&emitter->context->output,
                      "const int32_t %s = (int32_t)(f2c_constructor_first_%zu + "
                      "(int64_t)f2c_constructor_iteration_%zu * "
                      "f2c_constructor_step_%zu);\n",
                      iterator_name, temporary, temporary, temporary);
    f2c_array_indent(&emitter->context->output, depth + 2);
    f2c_buffer_printf(&emitter->context->output, "(void)%s;\n", iterator_name);
    if (!push_constructor_substitution(emitter, expression->symbol, expression->text,
                                       iterator_name))
        goto cleanup;
    for (i = 0U; i < value_count; ++i) {
        if (!emit_constructor_value(emitter, expression->children[i], depth + 2)) {
            --emitter->substitution_count;
            goto cleanup;
        }
    }
    --emitter->substitution_count;
    f2c_array_indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "}\n");
    f2c_array_indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    result = 1;

cleanup:
    free(initial);
    free(limit);
    free(step);
    f2c_expr_free(initial_expression);
    f2c_expr_free(limit_expression);
    f2c_expr_free(step_expression);
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
    if (expression->kind == F2C_EXPR_NAME && expression->rank != 0U)
        return emit_constructor_whole_array(emitter, expression, depth);
    if (expression->rank != 0U)
        return 0;
    return emit_constructor_scalar(emitter, expression, depth);
}

int f2c_array_emit_constructor_values(Context *context, Unit *unit, Symbol *target,
                                      const F2cExpr *constructor, const char *storage,
                                      const char *count, const char *capacity,
                                      const char *character_length,
                                      const char *character_length_set, int character, int dynamic,
                                      int infer_character_length, int depth) {
    ConstructorEmitter emitter;
    int result;
    if (context == NULL || unit == NULL || target == NULL || constructor == NULL ||
        storage == NULL || count == NULL)
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
    result = emit_constructor_value(&emitter, constructor, depth);
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
    if (!emit_constructor_value(&emitter, constructor, depth + 1)) {
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
    f2c_buffer_printf(&context->output,
                      "if (f2c_constructor_count != 0U) memmove(%s, "
                      "f2c_constructor_values, f2c_constructor_count * "
                      "sizeof(*f2c_constructor_values));\n",
                      f2c_symbol_c_name(unit, left_symbol));
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    release_constructor_emitter(&emitter);
    return 1;
}

int f2c_array_emit_allocatable_numeric_constructor(Context *context, Unit *unit, Symbol *target,
                                                   const F2cExpr *constructor, int depth) {
    const size_t output_start = context->output.length;
    const char *name;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || target == NULL ||
        !target->allocatable || target->rank != 1U || target->type == TYPE_CHARACTER)
        return 0;
    name = f2c_symbol_c_name(unit, target);
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
    if (!emit_constructor_value(&emitter, constructor, depth + 1))
        goto failed;
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index > (size_t)INT32_MAX) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_constructor_reallocate = %s == NULL || "
                      "(size_t)%s_extent_1 != f2c_constructor_index;\n",
                      name, name);
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
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s = f2c_constructor_values;\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "f2c_constructor_values = NULL;\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_lower_1 = 1;\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_extent_1 = (int32_t)f2c_constructor_index;\n", name);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else if (f2c_constructor_index != 0U) {\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output,
                      "memmove(%s, f2c_constructor_values, f2c_constructor_index * "
                      "sizeof(*f2c_constructor_values));\n",
                      name);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    release_constructor_emitter(&emitter);
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    release_constructor_emitter(&emitter);
    return 0;
}

int f2c_array_emit_allocatable_character_constructor(Context *context, Unit *unit, Symbol *target,
                                                     const F2cExpr *constructor, int depth) {
    const size_t output_start = context->output.length;
    const char *name;
    char *fixed_length = NULL;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || target == NULL ||
        !target->allocatable || target->rank != 1U || target->type != TYPE_CHARACTER)
        return 0;
    name = f2c_symbol_c_name(unit, target);
    if (!target->deferred_character) {
        fixed_length = f2c_symbol_character_length(unit, target);
        if (fixed_length == NULL)
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
    if (!emit_constructor_value(&emitter, constructor, depth + 1))
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
                      "(size_t)%s_extent_1 != f2c_constructor_index",
                      name, name);
    if (target->deferred_character)
        f2c_buffer_printf(&context->output,
                          " || f2c_char_len_%s != f2c_constructor_character_length", name);
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
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_lower_1 = 1;\n", name);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_extent_1 = (int32_t)f2c_constructor_index;\n", name);
    if (target->deferred_character) {
        f2c_array_indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_constructor_character_length;\n",
                          name);
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
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(fixed_length);
    release_constructor_emitter(&emitter);
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(fixed_length);
    release_constructor_emitter(&emitter);
    return 0;
}

int f2c_array_emit_whole_character_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                              const F2cExpr *right, Symbol *right_symbol,
                                              const char *element_count, int depth) {
    const size_t output_start = context->output.length;
    const int has_constructor = right != NULL && right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR;
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
    left_length = f2c_symbol_character_length(unit, left_symbol);
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
    f2c_buffer_append(&context->output, "if (f2c_whole_length != 0U && f2c_whole_count > "
                                        "SIZE_MAX / f2c_whole_length) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "const size_t f2c_whole_bytes = f2c_whole_count * f2c_whole_length;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "char *f2c_whole_values = f2c_whole_count == 0U ? NULL : "
                      "(char *)malloc(f2c_whole_bytes == 0U ? 1U : f2c_whole_bytes);\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_whole_count != 0U && f2c_whole_values == NULL) abort();\n");
    if (has_constructor) {
        ConstructorEmitter emitter;
        memset(&emitter, 0, sizeof(emitter));
        emitter.context = context;
        emitter.unit = unit;
        emitter.target = left_symbol;
        emitter.storage = "f2c_whole_values";
        emitter.count = "f2c_whole_count";
        emitter.index = "f2c_whole_index";
        emitter.character_length = "f2c_whole_length";
        emitter.character = 1;
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "size_t f2c_whole_index = 0U;\n");
        if (!emit_constructor_value(&emitter, right, depth + 1)) {
            release_constructor_emitter(&emitter);
            goto cleanup;
        }
        release_constructor_emitter(&emitter);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_whole_index != f2c_whole_count) abort();\n");
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
        f2c_buffer_append(&context->output,
                          "const size_t f2c_whole_copy_length = "
                          "F2C_MIN(f2c_whole_length, f2c_whole_source_length);\n");
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
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_whole_bytes != 0U) memmove(%s, f2c_whole_values, "
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
