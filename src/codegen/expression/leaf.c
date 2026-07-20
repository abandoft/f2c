#include "codegen/expression/private.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *f2c_expression_real_literal(const F2cExpr *expression) {
    char *result = f2c_strdup(expression->text != NULL ? expression->text : "0");
    char *kind;
    char *cursor;
    Buffer typed = {0};
    if (result == NULL)
        return NULL;
    kind = strchr(result, '_');
    if (kind != NULL)
        *kind = '\0';
    for (cursor = result; *cursor != '\0'; ++cursor) {
        if (*cursor == 'd' || *cursor == 'D')
            *cursor = 'e';
    }
    if (expression->type_kind == 4) {
        const size_t length = strlen(result);
        if (length == 0U || (result[length - 1U] != 'f' && result[length - 1U] != 'F')) {
            f2c_buffer_printf(&typed, "%sf", result);
            free(result);
            result = f2c_buffer_take(&typed);
        }
    }
    return result;
}

char *f2c_expression_integer_literal(const F2cExpr *expression) {
    const char *text = expression->text != NULL ? expression->text : "0";
    const char *suffix = strchr(text, '_');
    char *digits = suffix != NULL ? f2c_strdup_n(text, (size_t)(suffix - text)) : f2c_strdup(text);
    Buffer result = {0};
    if (digits == NULL)
        return NULL;
    if (expression->type_kind != 0 && expression->type_kind != 4) {
        f2c_buffer_printf(&result, "((%s)(%s))", f2c_expression_c_type(expression), digits);
        free(digits);
        return f2c_buffer_take(&result);
    }
    return digits;
}

char *f2c_expression_string_literal(const char *text) {
    Buffer result = {0};
    const char *quote_begin = f2c_character_literal_quote(text);
    char quote = quote_begin != NULL ? *quote_begin : '\'';
    size_t length = quote_begin != NULL ? strlen(quote_begin) : 0U;
    const char *hollerith = text;
    unsigned long long hollerith_length = 0ULL;
    size_t i;
    f2c_buffer_append(&result, "\"");
    while (hollerith != NULL && isdigit((unsigned char)*hollerith)) {
        hollerith_length = hollerith_length * 10ULL + (unsigned long long)(*hollerith - '0');
        ++hollerith;
    }
    if (hollerith != NULL && (*hollerith == 'h' || *hollerith == 'H') &&
        hollerith_length <= (unsigned long long)SIZE_MAX &&
        (size_t)hollerith_length <= strlen(hollerith + 1)) {
        const unsigned char *payload = (const unsigned char *)(hollerith + 1);
        for (i = 0U; i < (size_t)hollerith_length; ++i) {
            const unsigned char value = payload[i];
            if (value == '\\' || value == '"') {
                const char escaped = (char)value;
                f2c_buffer_append(&result, "\\");
                f2c_buffer_append_n(&result, &escaped, 1U);
            } else if (value >= 32U && value <= 126U) {
                const char printable = (char)value;
                f2c_buffer_append_n(&result, &printable, 1U);
            } else {
                f2c_buffer_printf(&result, "\\%03o", (unsigned int)value);
            }
        }
        f2c_buffer_append(&result, "\"");
        return f2c_buffer_take(&result);
    }
    for (i = 1U; i + 1U < length; ++i) {
        char c = quote_begin[i];
        if (c == quote && i + 1U < length - 1U && quote_begin[i + 1U] == quote)
            ++i;
        if (c == '\\' || c == '"')
            f2c_buffer_append(&result, "\\");
        f2c_buffer_append_n(&result, &c, 1U);
    }
    f2c_buffer_append(&result, "\"");
    return f2c_buffer_take(&result);
}

char *f2c_expression_boz_literal(const char *text) {
    const char prefix = text != NULL ? (char)tolower((unsigned char)text[0]) : '\0';
    const int base = prefix == 'b' ? 2 : (prefix == 'o' ? 8 : 16);
    const char *digits = text != NULL ? text + 2 : "";
    char *copy;
    char *end = NULL;
    unsigned long long value;
    Buffer result = {0};
    size_t length = text != NULL ? strlen(text) : 0U;
    if (length < 3U || (prefix != 'b' && prefix != 'o' && prefix != 'z' && prefix != 'x'))
        return NULL;
    copy = f2c_strdup_n(digits, length - 3U);
    if (copy == NULL)
        return NULL;
    value = strtoull(copy, &end, base);
    if (end == copy || *end != '\0' || value > (unsigned long long)UINT32_MAX) {
        free(copy);
        return NULL;
    }
    f2c_buffer_printf(&result, "((int32_t)UINT32_C(0x%08X))", (unsigned int)value);
    free(copy);
    return f2c_buffer_take(&result);
}

char *f2c_expression_name(Unit *unit, const F2cExpr *expression, int *supported) {
    Symbol *symbol = expression->symbol;
    if (symbol != NULL && symbol->parameter && symbol->initializer != NULL) {
        char *value = f2c_emit_typed_expression(unit, symbol->initializer_expression);
        Buffer constant = {0};
        if (value == NULL) {
            *supported = 0;
            return NULL;
        }
        if (symbol->type == TYPE_CHARACTER || symbol->type == TYPE_COMPLEX ||
            symbol->type == TYPE_DOUBLE_COMPLEX)
            f2c_buffer_printf(&constant, "(%s)", value);
        else
            f2c_buffer_printf(&constant, "((%s)(%s))", f2c_symbol_c_type(symbol), value);
        free(value);
        return f2c_buffer_take(&constant);
    }
    if (symbol != NULL && symbol->equivalence_unaligned && symbol->rank == 0U) {
        char *value = f2c_emit_unaligned_load(unit, symbol, NULL, 0U);
        if (value == NULL)
            *supported = 0;
        return value;
    }
    if (symbol != NULL && symbol->pointer && symbol->rank == 0U && !symbol->external) {
        Buffer dereference = {0};
        f2c_buffer_printf(&dereference, "(*%s)", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&dereference);
    }
    if (symbol != NULL && symbol->argument && symbol->rank == 0U && !symbol->external) {
        Buffer dereference = {0};
        f2c_buffer_printf(&dereference, "(*%s)", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&dereference);
    }
    if (unit->kind == UNIT_FUNCTION && unit->result_name != NULL && expression->text != NULL &&
        strcmp(expression->text, unit->result_name) == 0)
        return f2c_strdup("f2c_result");
    if (strcmp(expression->text != NULL ? expression->text : "", "real64") == 0)
        return f2c_strdup("8");
    if (strcmp(expression->text != NULL ? expression->text : "", "real32") == 0 ||
        strcmp(expression->text != NULL ? expression->text : "", "int32") == 0)
        return f2c_strdup("4");
    return f2c_strdup(symbol != NULL ? f2c_symbol_c_name(unit, symbol)
                                     : (expression->text != NULL ? expression->text : "0"));
}

void f2c_expression_free_arguments(char **arguments, Type *types, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i)
        free(arguments[i]);
    free(arguments);
    free(types);
}

int f2c_expression_children(Unit *unit, const F2cExpr *expression, char ***arguments_out,
                            Type **types_out) {
    char **arguments = expression->child_count != 0U
                           ? (char **)calloc(expression->child_count, sizeof(*arguments))
                           : NULL;
    Type *types = expression->child_count != 0U
                      ? (Type *)malloc(expression->child_count * sizeof(*types))
                      : NULL;
    size_t i;
    if (expression->child_count != 0U && (arguments == NULL || types == NULL)) {
        free(arguments);
        free(types);
        return 0;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        int supported = 1;
        arguments[i] = f2c_expression_emit(unit, expression->children[i], &supported);
        types[i] = expression->children[i]->type;
        if (!supported || arguments[i] == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            return 0;
        }
    }
    *arguments_out = arguments;
    *types_out = types;
    return 1;
}
