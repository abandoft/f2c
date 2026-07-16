#include "internal/f2c.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct LoweredCall {
    char **arguments;
    unsigned char *owned_transfers;
    size_t argument_count;
    Buffer prelude;
    Buffer postlude;
    int has_transfers;
    int has_descriptors;
    size_t array_conversion_count;
} LoweredCall;

static void emit_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static void free_string_list(char **items, size_t count) {
    size_t i;
    if (items == NULL)
        return;
    for (i = 0U; i < count; ++i)
        free(items[i]);
    free(items);
}

static char *lower_scalar_actual(Unit *unit, const char *text, const F2cExpr *ast) {
    char *copy = f2c_strdup(text);
    char *clean = copy != NULL ? f2c_trim(copy) : NULL;
    size_t consumed = 0U;
    char *name = clean != NULL ? f2c_identifier(clean, &consumed) : NULL;
    Symbol *base_symbol = name != NULL ? f2c_find_symbol(unit, name) : NULL;
    Symbol *symbol = base_symbol != NULL && clean[consumed] == '\0' ? base_symbol : NULL;
    char *result = NULL;
    if (ast != NULL && ast->kind == F2C_EXPR_KEYWORD_ARGUMENT && ast->child_count == 1U)
        ast = ast->children[0];
    if (ast != NULL && ast->kind == F2C_EXPR_ABSENT_ARGUMENT) {
        result = f2c_strdup("NULL");
        goto done;
    }
    if (ast != NULL) {
        int supported = 0;
        char *code = f2c_emit_expression_ast(unit, ast, &supported);
        Symbol *ast_symbol = ast->symbol;
        Buffer lowered = {0};
        if (!supported || code == NULL) {
            free(code);
            goto done;
        }
        if (ast->kind == F2C_EXPR_NAME && ast_symbol != NULL) {
            if (ast_symbol->external && ast_symbol->external_declared) {
                result = f2c_strdup(f2c_symbol_c_name(unit, ast_symbol));
            } else if (ast_symbol->parameter) {
                if (ast_symbol->type == TYPE_CHARACTER)
                    result = f2c_strdup(code);
                else
                    f2c_buffer_printf(&lowered, "&(%s){%s}", f2c_symbol_c_type(ast_symbol), code);
            } else if (ast_symbol->argument || ast_symbol->rank != 0U ||
                       (ast_symbol->type == TYPE_CHARACTER &&
                        ast_symbol->character_length != NULL)) {
                result = f2c_strdup(f2c_symbol_c_name(unit, ast_symbol));
            } else {
                f2c_buffer_printf(&lowered, "&%s", f2c_symbol_c_name(unit, ast_symbol));
            }
        } else if (ast->kind == F2C_EXPR_ARRAY_REFERENCE || ast->kind == F2C_EXPR_SUBSTRING) {
            if (ast->kind == F2C_EXPR_SUBSTRING && code[0] == '(' && code[1] == '&')
                result = f2c_strdup(code);
            else
                f2c_buffer_printf(&lowered, "&%s", code);
        } else if (ast->type == TYPE_CHARACTER) {
            result = f2c_strdup(code);
        } else {
            f2c_buffer_printf(&lowered, "&(%s){%s}",
                              ast->type != TYPE_UNKNOWN ? f2c_expression_c_type(ast)
                                                        : f2c_c_type(TYPE_REAL),
                              code);
        }
        if (result == NULL)
            result = f2c_buffer_take(&lowered);
        else
            free(f2c_buffer_take(&lowered));
        free(code);
        goto done;
    }
    if (clean == NULL)
        goto done;
    if (symbol != NULL) {
        if (symbol->external && symbol->external_declared) {
            result = f2c_strdup(f2c_symbol_c_name(unit, symbol));
        } else if (symbol->parameter && symbol->initializer != NULL) {
            char *value = f2c_translate_expression(unit, symbol->name);
            if (symbol->type == TYPE_CHARACTER) {
                result = value;
            } else {
                Buffer temporary = {0};
                f2c_buffer_printf(&temporary, "&(%s){%s}", f2c_symbol_c_type(symbol), value);
                free(value);
                result = f2c_buffer_take(&temporary);
            }
        } else if (symbol->argument || symbol->rank != 0U ||
                   (symbol->type == TYPE_CHARACTER && symbol->character_length != NULL)) {
            result = f2c_strdup(f2c_symbol_c_name(unit, symbol));
        } else {
            Buffer address = {0};
            f2c_buffer_printf(&address, "&%s", f2c_symbol_c_name(unit, symbol));
            result = f2c_buffer_take(&address);
        }
    } else {
        char *expression = f2c_translate_expression(unit, clean);
        if (expression[0] == '"' || strncmp(expression, "(char[", 6U) == 0 ||
            (base_symbol != NULL && base_symbol->type == TYPE_CHARACTER &&
             (expression[0] == '&' || strchr(clean, ':') != NULL))) {
            result = expression;
        } else {
            Buffer temporary = {0};
            const char *after_name = name != NULL ? clean + consumed : clean;
            int array_element = 0;
            Type type = f2c_expression_type(unit, clean);
            while (isspace((unsigned char)*after_name))
                ++after_name;
            if (base_symbol != NULL && base_symbol->rank != 0U && *after_name == '(') {
                const char *cursor = after_name + 1;
                int nesting = 1;
                int quote = 0;
                while (*cursor != '\0' && nesting != 0) {
                    if ((*cursor == '\'' || *cursor == '"') &&
                        (quote == 0 || quote == (unsigned char)*cursor))
                        quote = quote == 0 ? (unsigned char)*cursor : 0;
                    else if (quote == 0 && *cursor == '(')
                        ++nesting;
                    else if (quote == 0 && *cursor == ')')
                        --nesting;
                    ++cursor;
                }
                while (isspace((unsigned char)*cursor))
                    ++cursor;
                array_element = nesting == 0 && *cursor == '\0';
            }
            if (type == TYPE_UNKNOWN && base_symbol != NULL)
                type = base_symbol->type;
            if (type == TYPE_UNKNOWN)
                type = strpbrk(clean, ".eEdD") != NULL ? TYPE_DOUBLE : TYPE_INTEGER;
            if (array_element)
                f2c_buffer_printf(&temporary, "&%s", expression);
            else
                f2c_buffer_printf(&temporary, "&(%s){%s}", f2c_c_type(type), expression);
            free(expression);
            result = f2c_buffer_take(&temporary);
        }
    }

done:
    free(name);
    free(copy);
    return result;
}

static char *lower_transfer_actual(LoweredCall *call, Unit *unit, const char *text, size_t index,
                                   int depth) {
    char *copy = f2c_strdup(text);
    char *clean = copy != NULL ? f2c_trim(copy) : NULL;
    const char *open = clean != NULL ? strchr(clean, '(') : NULL;
    char **arguments = NULL;
    size_t count = 0U;
    char *source_name = NULL;
    char *source_reference_text = NULL;
    char *source_reference = NULL;
    char *element_count = NULL;
    char *result = NULL;
    Symbol *source_symbol;
    Type target_type;
    size_t consumed = 0U;
    if (clean == NULL || !f2c_starts_word(clean, "transfer") || open == NULL)
        goto done;
    arguments = f2c_split_arguments(open, &count);
    if (arguments == NULL || count < 3U || strstr(arguments[1], "(/") == NULL)
        goto done;
    source_name = f2c_identifier(f2c_trim(arguments[0]), &consumed);
    source_symbol = source_name != NULL ? f2c_find_symbol(unit, source_name) : NULL;
    if (source_symbol == NULL || source_symbol->rank == 0U ||
        (source_symbol->type != TYPE_REAL && source_symbol->type != TYPE_DOUBLE))
        goto done;
    {
        char *section_open = strchr(arguments[0], '(');
        char *colon = section_open != NULL ? strchr(section_open + 1, ':') : NULL;
        char *lower;
        Buffer reference = {0};
        if (section_open == NULL || colon == NULL)
            goto done;
        lower = f2c_strdup_n(section_open + 1, (size_t)(colon - section_open - 1));
        if (lower == NULL)
            goto done;
        f2c_buffer_printf(&reference, "%s(%s)", source_name, f2c_trim(lower));
        free(lower);
        source_reference_text = f2c_buffer_take(&reference);
    }
    source_reference = f2c_translate_expression(unit, source_reference_text);
    element_count = f2c_translate_expression(unit, f2c_trim(arguments[2]));
    target_type = source_symbol->type == TYPE_DOUBLE ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX;
    emit_indent(&call->prelude, depth);
    f2c_buffer_printf(&call->prelude,
                      "%s *f2c_transfer_%zu = (%s *)malloc(sizeof(%s) * "
                      "(size_t)F2C_MAX(1, (%s)));\n",
                      f2c_c_type(target_type), index, f2c_c_type(target_type),
                      f2c_c_type(target_type), element_count);
    emit_indent(&call->prelude, depth);
    f2c_buffer_printf(&call->prelude, "if (f2c_transfer_%zu == NULL) abort();\n", index);
    emit_indent(&call->prelude, depth);
    f2c_buffer_printf(&call->prelude, "memcpy(f2c_transfer_%zu, &%s, sizeof(%s) * (size_t)(%s));\n",
                      index, source_reference, f2c_c_type(target_type), element_count);
    {
        Buffer name = {0};
        f2c_buffer_printf(&name, "f2c_transfer_%zu", index);
        result = f2c_buffer_take(&name);
    }

done:
    free(source_reference_text);
    free(source_reference);
    free(element_count);
    free(source_name);
    free_string_list(arguments, count);
    free(copy);
    return result;
}

static int lower_call(LoweredCall *lowered, Unit *unit, char *const *arguments,
                      F2cExpr *const *argument_expressions, size_t count, int depth) {
    size_t i;
    lowered->argument_count = count;
    if (count == 0U)
        return 1;
    lowered->arguments = (char **)calloc(count, sizeof(*lowered->arguments));
    lowered->owned_transfers = (unsigned char *)calloc(count, sizeof(*lowered->owned_transfers));
    if (lowered->arguments == NULL || lowered->owned_transfers == NULL)
        return 0;
    for (i = 0U; i < count; ++i) {
        lowered->arguments[i] = lower_transfer_actual(lowered, unit, arguments[i], i, depth + 1);
        lowered->owned_transfers[i] = lowered->arguments[i] != NULL;
        lowered->has_transfers |= lowered->owned_transfers[i];
        if (lowered->arguments[i] == NULL)
            lowered->arguments[i] = lower_scalar_actual(
                unit, arguments[i], argument_expressions != NULL ? argument_expressions[i] : NULL);
        if (lowered->arguments[i] == NULL)
            return 0;
    }
    return 1;
}

static int prepare_array_conversions(LoweredCall *call, Unit *unit, F2cExpr *expression,
                                     int depth) {
    size_t i;
    if (expression == NULL)
        return 1;
    if (expression->kind == F2C_EXPR_CALL && expression->child_count == 1U &&
        (strcmp(expression->text, "real") == 0 || strcmp(expression->text, "float") == 0 ||
         strcmp(expression->text, "dble") == 0) &&
        expression->children[0]->kind == F2C_EXPR_NAME && expression->children[0]->symbol != NULL &&
        expression->children[0]->symbol->rank != 0U) {
        Symbol *source = expression->children[0]->symbol;
        char *count = f2c_symbol_element_count(unit, source);
        const size_t index = call->array_conversion_count++;
        Buffer name = {0};
        if (count == NULL)
            return 0;
        f2c_buffer_printf(&name, "f2c_array_conversion_%zu", index);
        expression->lowered_c = f2c_buffer_take(&name);
        if (expression->lowered_c == NULL) {
            free(count);
            return 0;
        }
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude, "size_t f2c_array_conversion_count_%zu = (size_t)(%s);\n",
                          index, count);
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude,
                          "if (f2c_array_conversion_count_%zu > SIZE_MAX / sizeof(%s)) abort();\n",
                          index, f2c_expression_c_type(expression));
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude,
                          "%s *%s = (%s *)malloc(sizeof(%s) * "
                          "F2C_MAX((size_t)1U, f2c_array_conversion_count_%zu));\n",
                          f2c_expression_c_type(expression), expression->lowered_c,
                          f2c_expression_c_type(expression), f2c_expression_c_type(expression),
                          index);
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude, "if (%s == NULL) abort();\n", expression->lowered_c);
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude,
                          "for (size_t f2c_array_conversion_i_%zu = 0U; "
                          "f2c_array_conversion_i_%zu < f2c_array_conversion_count_%zu; "
                          "++f2c_array_conversion_i_%zu) %s[f2c_array_conversion_i_%zu] = "
                          "(%s)%s[f2c_array_conversion_i_%zu];\n",
                          index, index, index, index, expression->lowered_c, index,
                          f2c_expression_c_type(expression), f2c_symbol_c_name(unit, source),
                          index);
        call->has_transfers = 1;
        free(count);
        return 1;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        if (!prepare_array_conversions(call, unit, expression->children[i], depth))
            return 0;
    }
    return 1;
}

static void lowered_call_free(LoweredCall *call) {
    free_string_list(call->arguments, call->argument_count);
    free(call->owned_transfers);
    free(f2c_buffer_take(&call->prelude));
    free(f2c_buffer_take(&call->postlude));
    memset(call, 0, sizeof(*call));
}

static int prepare_allocatable_descriptors(LoweredCall *call, Unit *unit, const Symbol *callee,
                                           F2cExpr *const *argument_expressions, size_t count,
                                           int depth) {
    size_t i;
    if (callee == NULL)
        return 1;
    for (i = 0U; i < count && i < callee->external_parameter_count; ++i) {
        const F2cExpr *expression = argument_expressions != NULL ? argument_expressions[i] : NULL;
        Symbol *actual;
        const char *name;
        char *character_length = NULL;
        size_t dimension;
        if (!callee->external_parameter_allocatable[i] && !callee->external_parameter_pointer[i])
            continue;
        if (expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            expression->child_count == 1U)
            expression = expression->children[0];
        if (expression != NULL && expression->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            free(call->arguments[i]);
            call->arguments[i] = f2c_strdup("NULL");
            if (call->arguments[i] == NULL)
                return 0;
            continue;
        }
        actual =
            expression != NULL && expression->kind == F2C_EXPR_NAME ? expression->symbol : NULL;
        if (actual == NULL || (callee->external_parameter_allocatable[i] && !actual->allocatable) ||
            (callee->external_parameter_pointer[i] && !actual->pointer))
            return 0;
        name = f2c_symbol_c_name(unit, actual);
        if (actual->type == TYPE_CHARACTER)
            character_length = f2c_symbol_character_length(unit, actual);
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude,
                          "f2c_descriptor f2c_call_descriptor_%zu = {.data = %s, .element_size = "
                          "sizeof(%s), .rank = %zuU, .character_length = (size_t)(%s)};\n",
                          i, name, f2c_symbol_c_type(actual), actual->rank,
                          character_length != NULL ? character_length : "0U");
        for (dimension = 0U; dimension < actual->rank; ++dimension) {
            emit_indent(&call->prelude, depth);
            f2c_buffer_printf(&call->prelude,
                              "f2c_call_descriptor_%zu.lower[%zu] = %s_lower_%zu;\n", i, dimension,
                              name, dimension + 1U);
            emit_indent(&call->prelude, depth);
            f2c_buffer_printf(&call->prelude,
                              "f2c_call_descriptor_%zu.extent[%zu] = %s_extent_%zu;\n", i,
                              dimension, name, dimension + 1U);
        }
        free(character_length);
        free(call->arguments[i]);
        {
            Buffer reference = {0};
            f2c_buffer_printf(&reference, "&f2c_call_descriptor_%zu", i);
            call->arguments[i] = f2c_buffer_take(&reference);
        }
        if (call->arguments[i] == NULL)
            return 0;
        emit_indent(&call->postlude, depth);
        f2c_buffer_printf(&call->postlude, "%s = (%s *)f2c_call_descriptor_%zu.data;\n", name,
                          f2c_symbol_c_type(actual), i);
        if (actual->deferred_character) {
            emit_indent(&call->postlude, depth);
            f2c_buffer_printf(&call->postlude,
                              "f2c_char_len_%s = f2c_call_descriptor_%zu.character_length;\n", name,
                              i);
        }
        for (dimension = 0U; dimension < actual->rank; ++dimension) {
            emit_indent(&call->postlude, depth);
            f2c_buffer_printf(&call->postlude,
                              "%s_lower_%zu = (int32_t)f2c_call_descriptor_%zu.lower[%zu];\n", name,
                              dimension + 1U, i, dimension);
            emit_indent(&call->postlude, depth);
            f2c_buffer_printf(&call->postlude,
                              "%s_extent_%zu = (int32_t)f2c_call_descriptor_%zu.extent[%zu];\n",
                              name, dimension + 1U, i, dimension);
        }
        call->has_descriptors = 1;
    }
    return 1;
}

void f2c_emit_call_with_signature(Buffer *output, Unit *unit, const char *name,
                                  const Symbol *explicit_callee, char *const *arguments,
                                  F2cExpr *const *argument_expressions, size_t count, int depth) {
    size_t i;
    LoweredCall call;
    const Symbol *callee;
    int has_scope;
    if (name == NULL)
        return;
    memset(&call, 0, sizeof(call));
    for (i = 0U; i < count; ++i) {
        if (argument_expressions != NULL &&
            !prepare_array_conversions(&call, unit, argument_expressions[i], depth + 1)) {
            lowered_call_free(&call);
            return;
        }
    }
    if (!lower_call(&call, unit, arguments, argument_expressions, count, depth)) {
        lowered_call_free(&call);
        return;
    }
    callee = explicit_callee != NULL ? explicit_callee : f2c_find_symbol(unit, name);
    if (callee == NULL) {
        for (i = 0U; i < unit->symbol_count; ++i) {
            if (strcmp(f2c_symbol_c_name(unit, &unit->symbols[i]), name) == 0) {
                callee = &unit->symbols[i];
                break;
            }
        }
    }
    if (callee != NULL) {
        for (i = 0U; i < count && i < callee->external_parameter_count; ++i) {
            const F2cExpr *actual = argument_expressions != NULL ? argument_expressions[i] : NULL;
            F2cDerivedType *expected = callee->external_parameter_derived_types[i];
            Buffer cast = {0};
            if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                actual->child_count == 1U)
                actual = actual->children[0];
            if (!callee->external_parameter_polymorphic[i] || expected == NULL || actual == NULL ||
                actual->derived_type == NULL || actual->derived_type == expected)
                continue;
            f2c_buffer_printf(&cast, "(%s *)(%s)", expected->c_name, call.arguments[i]);
            free(call.arguments[i]);
            call.arguments[i] = f2c_buffer_take(&cast);
            if (call.arguments[i] == NULL) {
                lowered_call_free(&call);
                return;
            }
        }
    }
    if (!prepare_allocatable_descriptors(&call, unit, callee, argument_expressions, count,
                                         depth + 1)) {
        lowered_call_free(&call);
        return;
    }
    has_scope = call.has_transfers || call.has_descriptors;
    if (has_scope) {
        emit_indent(output, depth);
        f2c_buffer_append(output, "{\n");
        f2c_buffer_append(output, call.prelude.data != NULL ? call.prelude.data : "");
    }
    emit_indent(output, depth + (has_scope ? 1 : 0));
    f2c_buffer_printf(output, "%s(",
                      strcmp(name, "random_number") == 0 ? "F2C_RANDOM_NUMBER" : name);
    for (i = 0U; i < count; ++i)
        f2c_buffer_printf(output, "%s%s", i == 0U ? "" : ", ", call.arguments[i]);
    for (i = 0U; i < count; ++i) {
        F2cExpr *temporary = NULL;
        const F2cExpr *expression = argument_expressions != NULL ? argument_expressions[i] : NULL;
        char *length;
        if (expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            expression->child_count == 1U)
            expression = expression->children[0];
        if (expression == NULL) {
            temporary = f2c_parse_expression_ast(unit, arguments[i], NULL);
            expression = temporary;
        }
        if (expression == NULL || expression->type != TYPE_CHARACTER ||
            (callee != NULL && i < callee->external_parameter_count &&
             (callee->external_parameter_allocatable[i] ||
              callee->external_parameter_pointer[i])) ||
            (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
             expression->symbol->external)) {
            f2c_expr_free(temporary);
            continue;
        }
        length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(output, ", %s", length != NULL ? length : "1U");
        free(length);
        f2c_expr_free(temporary);
    }
    f2c_buffer_append(output, ");\n");
    if (has_scope) {
        if (call.postlude.data != NULL)
            f2c_buffer_append(output, call.postlude.data);
        for (i = 0U; i < count; ++i) {
            if (!call.owned_transfers[i])
                continue;
            emit_indent(output, depth + 1);
            f2c_buffer_printf(output, "free(f2c_transfer_%zu);\n", i);
        }
        for (i = 0U; i < call.array_conversion_count; ++i) {
            emit_indent(output, depth + 1);
            f2c_buffer_printf(output, "free(f2c_array_conversion_%zu);\n", i);
        }
        emit_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    }
    lowered_call_free(&call);
}

void f2c_emit_call(Buffer *output, Unit *unit, const char *name, char *const *arguments,
                   F2cExpr *const *argument_expressions, size_t count, int depth) {
    f2c_emit_call_with_signature(output, unit, name, NULL, arguments, argument_expressions, count,
                                 depth);
}
