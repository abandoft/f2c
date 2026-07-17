#include "internal/f2c.h"

#include "codegen/descriptor/private.h"

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

static int actual_designates_const_dummy(const F2cExpr *actual) {
    size_t child;
    if (actual == NULL)
        return 0;
    if (actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        return actual_designates_const_dummy(actual->children[0]);
    if (actual->kind != F2C_EXPR_NAME && actual->kind != F2C_EXPR_ARRAY_REFERENCE &&
        actual->kind != F2C_EXPR_SUBSTRING && actual->kind != F2C_EXPR_COMPONENT)
        return 0;
    if (actual->symbol != NULL && actual->symbol->argument &&
        actual->symbol->intent == F2C_INTENT_IN)
        return 1;
    for (child = 0U; child < actual->child_count; ++child) {
        if (actual_designates_const_dummy(actual->children[child]))
            return 1;
    }
    return 0;
}

char *f2c_bridge_implicit_mutable_actual(const Symbol *callee, size_t parameter,
                                         const F2cExpr *actual, const char *code) {
    Buffer result = {0};
    if (code == NULL)
        return NULL;
    if (callee == NULL || !callee->external || callee->external_signature_explicit ||
        parameter >= callee->external_parameter_count ||
        callee->external_parameter_const[parameter] ||
        callee->external_parameter_allocatable[parameter] ||
        callee->external_parameter_pointer[parameter] ||
        callee->external_parameter_descriptor[parameter] ||
        callee->external_parameter_procedures[parameter] != NULL ||
        !actual_designates_const_dummy(actual))
        return f2c_strdup(code);
    f2c_buffer_printf(&result, "f2c_implicit_mutable_actual(%s)", code);
    return f2c_buffer_take(&result);
}

static char *lower_scalar_actual(Unit *unit, const F2cExpr *ast) {
    char *result = NULL;
    if (ast != NULL && ast->kind == F2C_EXPR_KEYWORD_ARGUMENT && ast->child_count == 1U)
        ast = ast->children[0];
    if (ast == NULL)
        return NULL;
    if (ast->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return f2c_strdup("NULL");
    {
        int supported = 0;
        char *code = f2c_emit_expression_ast(unit, ast, &supported);
        Symbol *ast_symbol = ast->symbol;
        Buffer lowered = {0};
        if (!supported || code == NULL) {
            free(code);
            return NULL;
        }
        if (ast->kind == F2C_EXPR_NAME && ast_symbol != NULL) {
            if (ast_symbol->external && ast_symbol->external_declared) {
                result = f2c_strdup(f2c_symbol_c_name(unit, ast_symbol));
            } else if (ast_symbol->parameter) {
                if (ast_symbol->type == TYPE_CHARACTER)
                    result = f2c_strdup(code);
                else
                    result = f2c_emit_scalar_temporary_address(f2c_symbol_c_type(ast_symbol),
                                                               ast_symbol->type, code);
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
            result = f2c_emit_scalar_temporary_address(
                ast->type != TYPE_UNKNOWN ? f2c_expression_c_type(ast) : f2c_c_type(TYPE_REAL),
                ast->type != TYPE_UNKNOWN ? ast->type : TYPE_REAL, code);
        }
        if (result == NULL)
            result = f2c_buffer_take(&lowered);
        else
            free(f2c_buffer_take(&lowered));
        free(code);
        return result;
    }
}

static char *lower_transfer_actual(LoweredCall *call, Unit *unit, const F2cExpr *expression,
                                   size_t index, int depth) {
    const F2cExpr *source;
    const F2cExpr *mold;
    const F2cExpr *count;
    const F2cExpr *section;
    char *source_reference = NULL;
    char *element_count = NULL;
    char *lower = NULL;
    char *result = NULL;
    Symbol *source_symbol;
    Type target_type;
    int supported = 1;
    if (expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
        expression->child_count == 1U)
        expression = expression->children[0];
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->text == NULL ||
        strcmp(expression->text, "transfer") != 0 || expression->child_count < 3U)
        goto done;
    source = expression->children[0];
    mold = expression->children[1];
    count = expression->children[2];
    source_symbol = source != NULL ? source->symbol : NULL;
    if (source == NULL || source->kind != F2C_EXPR_ARRAY_REFERENCE || source_symbol == NULL ||
        source_symbol->rank != 1U || source->child_count != 1U ||
        source->children[0]->kind != F2C_EXPR_ARRAY_SECTION ||
        source->children[0]->child_count != 3U || mold == NULL ||
        mold->kind != F2C_EXPR_ARRAY_CONSTRUCTOR)
        goto done;
    if (source_symbol->type != TYPE_REAL && source_symbol->type != TYPE_DOUBLE)
        goto done;
    section = source->children[0];
    if (section->children[0]->kind == F2C_EXPR_INVALID)
        lower = f2c_symbol_dimension_lower(unit, source_symbol, 0U);
    else
        lower = f2c_emit_expression_ast(unit, section->children[0], &supported);
    element_count = f2c_emit_expression_ast(unit, count, &supported);
    if (!supported || lower == NULL || element_count == NULL)
        goto done;
    {
        char *indices[1] = {lower};
        source_reference = f2c_emit_array_reference(unit, source_symbol, indices, 1U);
    }
    if (source_reference == NULL ||
        (source_symbol->type != TYPE_REAL && source_symbol->type != TYPE_DOUBLE))
        goto done;
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
    free(lower);
    free(source_reference);
    free(element_count);
    return result;
}

static int lower_call(LoweredCall *lowered, Unit *unit, const Symbol *callee,
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
        F2cExpr *expression = argument_expressions != NULL ? argument_expressions[i] : NULL;
        if (callee != NULL && i < callee->external_parameter_count &&
            callee->external_parameter_descriptor[i]) {
            lowered->arguments[i] = f2c_strdup("NULL");
            if (lowered->arguments[i] == NULL)
                return 0;
            continue;
        }
        lowered->arguments[i] = lower_transfer_actual(lowered, unit, expression, i, depth + 1);
        lowered->owned_transfers[i] = lowered->arguments[i] != NULL;
        lowered->has_transfers |= lowered->owned_transfers[i];
        if (lowered->arguments[i] == NULL)
            lowered->arguments[i] = lower_scalar_actual(unit, expression);
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
        const char *name = NULL;
        const char *c_type;
        char *character_length = NULL;
        F2cDescriptorView view = {0};
        int has_view;
        size_t dimension;
        if (!callee->external_parameter_descriptor[i])
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
        actual = expression != NULL ? expression->symbol : NULL;
        if (expression == NULL || expression->rank != callee->external_parameter_ranks[i] ||
            (callee->external_parameter_allocatable[i] &&
             (expression->kind != F2C_EXPR_NAME || actual == NULL || !actual->allocatable)) ||
            (callee->external_parameter_pointer[i] &&
             (expression->kind != F2C_EXPR_NAME || actual == NULL || !actual->pointer)))
            return 0;
        has_view = f2c_descriptor_view(unit, expression, &view);
        if (!has_view &&
            (callee->external_parameter_allocatable[i] || callee->external_parameter_pointer[i] ||
             !f2c_descriptor_materialize_view(&call->prelude, &call->postlude, unit, expression,
                                              callee->external_parameter_intents[i], i, depth,
                                              &view)))
            return 0;
        if (actual != NULL)
            name = f2c_symbol_c_name(unit, actual);
        c_type = f2c_expression_c_type(expression);
        if (expression->type == TYPE_CHARACTER)
            character_length = view.character_length != NULL
                                   ? f2c_strdup(view.character_length)
                                   : f2c_character_length_expression(unit, expression);
        emit_indent(&call->prelude, depth);
        f2c_buffer_printf(&call->prelude,
                          "f2c_descriptor f2c_call_descriptor_%zu = {.data = %s, .element_size = "
                          "sizeof(%s), .rank = %zuU, .character_length = (size_t)(%s)};\n",
                          i, view.data, c_type, view.rank,
                          character_length != NULL ? character_length : "0U");
        for (dimension = 0U; dimension < view.rank; ++dimension) {
            emit_indent(&call->prelude, depth);
            f2c_buffer_printf(&call->prelude,
                              "f2c_call_descriptor_%zu.lower[%zu] = (int64_t)(%s);\n", i, dimension,
                              view.lower[dimension]);
            emit_indent(&call->prelude, depth);
            f2c_buffer_printf(&call->prelude,
                              "f2c_call_descriptor_%zu.extent[%zu] = "
                              "f2c_descriptor_extent((size_t)(%s));\n",
                              i, dimension, view.extent[dimension]);
            emit_indent(&call->prelude, depth);
            f2c_buffer_printf(&call->prelude,
                              "f2c_call_descriptor_%zu.stride[%zu] = (ptrdiff_t)(%s);\n", i,
                              dimension, view.stride[dimension]);
        }
        free(character_length);
        free(call->arguments[i]);
        {
            Buffer reference = {0};
            f2c_buffer_printf(&reference, "&f2c_call_descriptor_%zu", i);
            call->arguments[i] = f2c_buffer_take(&reference);
        }
        if (call->arguments[i] == NULL)
            goto descriptor_failed;
        if (callee->external_parameter_allocatable[i] || callee->external_parameter_pointer[i]) {
            emit_indent(&call->postlude, depth);
            f2c_buffer_printf(&call->postlude, "%s = (%s *)f2c_call_descriptor_%zu.data;\n", name,
                              c_type, i);
            if (actual->deferred_character) {
                emit_indent(&call->postlude, depth);
                f2c_buffer_printf(&call->postlude,
                                  "f2c_char_len_%s = f2c_call_descriptor_%zu.character_length;\n",
                                  name, i);
            }
            for (dimension = 0U; dimension < actual->rank; ++dimension) {
                emit_indent(&call->postlude, depth);
                f2c_buffer_printf(&call->postlude,
                                  "%s_lower_%zu = (int32_t)f2c_call_descriptor_%zu.lower[%zu];\n",
                                  name, dimension + 1U, i, dimension);
                emit_indent(&call->postlude, depth);
                f2c_buffer_printf(&call->postlude,
                                  "%s_extent_%zu = (int32_t)f2c_call_descriptor_%zu.extent[%zu];\n",
                                  name, dimension + 1U, i, dimension);
                if (actual->argument && f2c_symbol_uses_descriptor(actual)) {
                    emit_indent(&call->postlude, depth);
                    f2c_buffer_printf(&call->postlude,
                                      "%s_stride_%zu = f2c_call_descriptor_%zu.stride[%zu];\n",
                                      name, dimension + 1U, i, dimension);
                }
            }
        }
        f2c_descriptor_view_free(&view);
        call->has_descriptors = 1;
        continue;

    descriptor_failed:
        f2c_descriptor_view_free(&view);
        return 0;
    }
    return 1;
}

void f2c_emit_call_with_signature(Buffer *output, Unit *unit, const char *name,
                                  const Symbol *explicit_callee,
                                  F2cExpr *const *argument_expressions, size_t count, int depth) {
    size_t i;
    LoweredCall call;
    const Symbol *callee;
    int has_scope;
    if (name == NULL)
        return;
    memset(&call, 0, sizeof(call));
    callee = explicit_callee != NULL ? explicit_callee : f2c_find_symbol(unit, name);
    if (callee == NULL) {
        for (i = 0U; i < unit->symbol_count; ++i) {
            if (strcmp(f2c_symbol_c_name(unit, &unit->symbols[i]), name) == 0) {
                callee = &unit->symbols[i];
                break;
            }
        }
    }
    for (i = 0U; i < count; ++i) {
        if (argument_expressions != NULL &&
            !prepare_array_conversions(&call, unit, argument_expressions[i], depth + 1)) {
            lowered_call_free(&call);
            return;
        }
    }
    if (!lower_call(&call, unit, callee, argument_expressions, count, depth)) {
        lowered_call_free(&call);
        return;
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
        for (i = 0U; i < count && i < callee->external_parameter_count; ++i) {
            char *bridged = f2c_bridge_implicit_mutable_actual(
                callee, i, argument_expressions != NULL ? argument_expressions[i] : NULL,
                call.arguments[i]);
            if (bridged == NULL) {
                lowered_call_free(&call);
                return;
            }
            free(call.arguments[i]);
            call.arguments[i] = bridged;
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
        const F2cExpr *expression = argument_expressions != NULL ? argument_expressions[i] : NULL;
        char *length;
        if (expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            expression->child_count == 1U)
            expression = expression->children[0];
        if (expression == NULL || expression->type != TYPE_CHARACTER ||
            (callee != NULL && i < callee->external_parameter_count &&
             callee->external_parameter_descriptor[i]) ||
            (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
             expression->symbol->external)) {
            continue;
        }
        length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(output, ", %s", length != NULL ? length : "1U");
        free(length);
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

void f2c_emit_call(Buffer *output, Unit *unit, const char *name,
                   F2cExpr *const *argument_expressions, size_t count, int depth) {
    f2c_emit_call_with_signature(output, unit, name, NULL, argument_expressions, count, depth);
}
