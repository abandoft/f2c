#include "internal/f2c.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t string_literal_length(const char *text) {
    const size_t source_length = text != NULL ? strlen(text) : 0U;
    const char quote = source_length != 0U ? text[0] : '\0';
    const char *payload;
    size_t payload_length;
    size_t length = 0U;
    size_t i;
    if (f2c_hollerith_payload(text, &payload, &payload_length))
        return payload_length;
    if ((quote != '\'' && quote != '"') || source_length < 2U)
        return 0U;
    for (i = 1U; i + 1U < source_length; ++i) {
        if (text[i] == quote && i + 1U < source_length - 1U && text[i + 1U] == quote)
            ++i;
        ++length;
    }
    return length;
}

static int character_element_count(Unit *unit, const Symbol *symbol, size_t *count) {
    size_t result = 1U;
    size_t dimension;
    if (symbol == NULL || count == NULL)
        return 0;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        int64_t lower;
        int64_t upper;
        unsigned long long extent;
        if (!(symbol->dimensions[dimension].lower_expression != NULL
                  ? f2c_evaluate_integer_constant(
                        unit, symbol->dimensions[dimension].lower_expression, &lower)
                  : f2c_evaluate_integer_text(unit, symbol->dimensions[dimension].lower, &lower)) ||
            !(symbol->dimensions[dimension].upper_expression != NULL
                  ? f2c_evaluate_integer_constant(
                        unit, symbol->dimensions[dimension].upper_expression, &upper)
                  : f2c_evaluate_integer_text(unit, symbol->dimensions[dimension].upper, &upper)))
            return 0;
        if (upper >= lower) {
            const uint64_t difference = (uint64_t)upper - (uint64_t)lower;
            if (difference == UINT64_MAX)
                return 0;
            extent = difference + 1ULL;
        } else {
            extent = 0ULL;
        }
        if (extent > SIZE_MAX || (extent != 0ULL && result > SIZE_MAX / (size_t)extent))
            return 0;
        result *= (size_t)extent;
    }
    *count = result;
    return 1;
}

static char *character_literal_bytes(const char *text, size_t *length) {
    const char *cursor = text;
    const char *payload;
    size_t payload_length;
    char quote;
    Buffer bytes = {0};
    if (text == NULL || length == NULL)
        return NULL;
    if (f2c_hollerith_payload(text, &payload, &payload_length)) {
        *length = payload_length;
        return f2c_strdup_n(payload, payload_length);
    }
    while (isspace((unsigned char)*cursor))
        ++cursor;
    quote = *cursor;
    if (quote != '\'' && quote != '"')
        return NULL;
    ++cursor;
    while (*cursor != '\0') {
        if (*cursor == quote) {
            if (cursor[1] == quote) {
                f2c_buffer_printf(&bytes, "%c", quote);
                cursor += 2;
                continue;
            }
            ++cursor;
            while (isspace((unsigned char)*cursor))
                ++cursor;
            if (*cursor != '\0') {
                free(f2c_buffer_take(&bytes));
                return NULL;
            }
            *length = bytes.length;
            return f2c_buffer_take(&bytes);
        }
        f2c_buffer_printf(&bytes, "%c", *cursor++);
    }
    free(f2c_buffer_take(&bytes));
    return NULL;
}

static void append_c_string_byte(Buffer *output, unsigned char value) {
    if (value == '"' || value == '\\') {
        f2c_buffer_printf(output, "\\%c", value);
    } else if (value >= 32U && value <= 126U) {
        f2c_buffer_printf(output, "%c", value);
    } else {
        f2c_buffer_printf(output, "\\%03o", (unsigned int)value);
    }
}

static void append_c_character_constant(Buffer *output, unsigned char value, size_t index) {
    if (index != 0U)
        f2c_buffer_append(output, ", ");
    if (value == '\'' || value == '\\') {
        f2c_buffer_printf(output, "'\\%c'", value);
    } else if (value >= 32U && value <= 126U) {
        f2c_buffer_printf(output, "'%c'", value);
    } else {
        f2c_buffer_printf(output, "0x%02X", (unsigned int)value);
    }
}

char *f2c_character_declaration_initializer(Unit *unit, const Symbol *symbol, int *supported) {
    char *copy = NULL;
    char *constructor = NULL;
    char **values = NULL;
    size_t value_count = 0U;
    size_t element_count;
    size_t element_length;
    int64_t evaluated_length;
    size_t element;
    size_t output_index = 0U;
    Buffer result = {0};
    if (supported != NULL)
        *supported = 0;
    if (symbol == NULL || symbol->type != TYPE_CHARACTER || symbol->initializer == NULL ||
        !(symbol->character_length_expression != NULL
              ? f2c_evaluate_integer_constant(unit, symbol->character_length_expression,
                                              &evaluated_length)
              : f2c_evaluate_integer_text(
                    unit, symbol->character_length != NULL ? symbol->character_length : "1",
                    &evaluated_length)) ||
        !character_element_count(unit, symbol, &element_count))
        return NULL;
    if (evaluated_length > 0 && (uint64_t)evaluated_length > SIZE_MAX)
        return NULL;
    element_length = evaluated_length > 0 ? (size_t)evaluated_length : 0U;
    copy = f2c_strdup(symbol->initializer);
    if (copy == NULL)
        return NULL;
    {
        char *clean = f2c_trim(copy);
        const size_t length = strlen(clean);
        if (symbol->rank != 0U && length >= 2U && clean[0] == '[' && clean[length - 1U] == ']') {
            constructor = f2c_strdup_n(clean + 1, length - 2U);
        } else if (symbol->rank != 0U && length >= 4U && clean[0] == '(' && clean[1] == '/' &&
                   clean[length - 2U] == '/' && clean[length - 1U] == ')') {
            constructor = f2c_strdup_n(clean + 2, length - 4U);
        } else {
            constructor = f2c_strdup(clean);
        }
    }
    free(copy);
    if (constructor == NULL)
        return NULL;
    if (symbol->rank != 0U && (symbol->initializer[0] == '[' ||
                               (symbol->initializer[0] == '(' && symbol->initializer[1] == '/'))) {
        values = f2c_split_arguments(constructor, &value_count);
    } else {
        values = (char **)calloc(1U, sizeof(*values));
        if (values != NULL) {
            values[0] = f2c_strdup(constructor);
            value_count = values[0] != NULL ? 1U : 0U;
        }
    }
    free(constructor);
    if (values == NULL || value_count == 0U || (value_count != 1U && value_count != element_count))
        goto cleanup;
    f2c_buffer_append(&result, symbol->rank == 0U ? "\"" : "{");
    for (element = 0U; element < element_count; ++element) {
        const size_t value_index = value_count == 1U ? 0U : element;
        size_t literal_length = 0U;
        char *literal = character_literal_bytes(values[value_index], &literal_length);
        size_t offset;
        if (literal == NULL)
            goto cleanup;
        for (offset = 0U; offset < element_length; ++offset) {
            const unsigned char byte =
                offset < literal_length ? (unsigned char)literal[offset] : (unsigned char)' ';
            if (symbol->rank == 0U)
                append_c_string_byte(&result, byte);
            else
                append_c_character_constant(&result, byte, output_index);
            ++output_index;
        }
        free(literal);
    }
    f2c_buffer_append(&result, symbol->rank == 0U ? "\"" : "}");
    if (supported != NULL)
        *supported = 1;

cleanup:
    while (value_count != 0U)
        free(values[--value_count]);
    free(values);
    if (supported == NULL || !*supported) {
        free(f2c_buffer_take(&result));
        return NULL;
    }
    return f2c_buffer_take(&result);
}

char *f2c_symbol_character_length(Unit *unit, const Symbol *symbol) {
    Buffer result = {0};
    if (symbol == NULL || symbol->type != TYPE_CHARACTER)
        return NULL;
    if (symbol->deferred_character) {
        f2c_buffer_printf(&result, "f2c_char_len_%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (symbol->automatic_character) {
        f2c_buffer_printf(&result, "f2c_char_len_%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (symbol->argument && symbol->character_length != NULL &&
        strcmp(symbol->character_length, "*") == 0) {
        f2c_buffer_printf(&result, "f2c_len_%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (symbol->character_length != NULL)
        return f2c_emit_cached_expression(unit, symbol->character_length_expression,
                                          symbol->character_length);
    if (symbol->parameter && symbol->initializer != NULL &&
        (symbol->initializer[0] == '\'' || symbol->initializer[0] == '"')) {
        f2c_buffer_printf(&result, "%zuU", string_literal_length(symbol->initializer));
        return f2c_buffer_take(&result);
    }
    return f2c_strdup("1U");
}

char *f2c_character_length_expression(Unit *unit, const F2cExpr *expression) {
    Buffer result = {0};
    if (expression == NULL || expression->type != TYPE_CHARACTER)
        return NULL;
    if (expression->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return f2c_strdup("0U");
    if (expression->kind == F2C_EXPR_STRING_LITERAL) {
        f2c_buffer_printf(&result, "%zuU", string_literal_length(expression->text));
        return f2c_buffer_take(&result);
    }
    if (expression->kind == F2C_EXPR_NAME || expression->kind == F2C_EXPR_ARRAY_REFERENCE)
        return f2c_symbol_character_length(unit, expression->symbol);
    if (expression->kind == F2C_EXPR_COMPONENT && expression->symbol != NULL &&
        expression->symbol->deferred_character && expression->child_count != 0U) {
        int supported = 0;
        char *owner = f2c_emit_expression_ast(unit, expression->children[0], &supported);
        if (supported && owner != NULL) {
            f2c_buffer_printf(&result, "(size_t)(%s).%s_character_length", owner,
                              f2c_symbol_c_name(unit, expression->symbol));
            free(owner);
            return f2c_buffer_take(&result);
        }
        free(owner);
        return NULL;
    }
    if (expression->kind == F2C_EXPR_COMPONENT)
        return f2c_symbol_character_length(unit, expression->symbol);
    if (expression->kind == F2C_EXPR_CALL) {
        if (expression->text != NULL && strcmp(expression->text, "char") == 0)
            return f2c_strdup("1U");
        if (expression->text != NULL && expression->child_count != 0U &&
            (strcmp(expression->text, "reshape") == 0 || strcmp(expression->text, "pack") == 0 ||
             strcmp(expression->text, "unpack") == 0 || strcmp(expression->text, "spread") == 0 ||
             strcmp(expression->text, "cshift") == 0 || strcmp(expression->text, "eoshift") == 0)) {
            const F2cExpr *source = expression->children[0];
            if (source != NULL && source->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                source->child_count == 1U)
                source = source->children[0];
            return f2c_character_length_expression(unit, source);
        }
        return f2c_symbol_character_length(unit, expression->symbol);
    }
    if (expression->kind == F2C_EXPR_BINARY && expression->text != NULL &&
        strcmp(expression->text, "//") == 0 && expression->child_count == 2U) {
        char *left = f2c_character_length_expression(unit, expression->children[0]);
        char *right = f2c_character_length_expression(unit, expression->children[1]);
        if (left == NULL || right == NULL) {
            free(left);
            free(right);
            return NULL;
        }
        f2c_buffer_printf(&result, "((size_t)(%s) + (size_t)(%s))", left, right);
        free(left);
        free(right);
        return f2c_buffer_take(&result);
    }
    if (expression->kind == F2C_EXPR_SUBSTRING && expression->symbol != NULL &&
        expression->child_count == 1U) {
        const F2cExpr *selector = expression->children[0];
        const F2cExpr *lower_expression = NULL;
        const F2cExpr *upper_expression = NULL;
        char *lower;
        char *upper;
        char *declared_length;
        int supported = 1;
        if (selector->kind == F2C_EXPR_ARRAY_SECTION && selector->child_count >= 2U) {
            if (selector->children[0]->kind != F2C_EXPR_INVALID)
                lower_expression = selector->children[0];
            if (selector->children[1]->kind != F2C_EXPR_INVALID)
                upper_expression = selector->children[1];
        } else {
            lower_expression = selector;
            upper_expression = selector;
        }
        lower = lower_expression != NULL
                    ? f2c_emit_expression_ast(unit, lower_expression, &supported)
                    : f2c_strdup("1");
        upper = upper_expression != NULL
                    ? f2c_emit_expression_ast(unit, upper_expression, &supported)
                    : f2c_symbol_character_length(unit, expression->symbol);
        declared_length = f2c_symbol_character_length(unit, expression->symbol);
        if (!supported || lower == NULL || upper == NULL || declared_length == NULL) {
            free(lower);
            free(upper);
            free(declared_length);
            return NULL;
        }
        f2c_buffer_printf(&result,
                          "f2c_substring_length((size_t)(%s), (int64_t)(%s), "
                          "(int64_t)(%s))",
                          declared_length, lower, upper);
        free(lower);
        free(upper);
        free(declared_length);
        return f2c_buffer_take(&result);
    }
    return f2c_strdup("1U");
}

static void emit_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static char *character_target_length(Unit *unit, const Symbol *symbol) {
    Buffer result = {0};
    if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
        unit->result_name != NULL && strcmp(symbol->name, unit->result_name) == 0)
        return f2c_strdup("f2c_result_len");
    if (symbol->deferred_character)
        return f2c_symbol_character_length(unit, symbol);
    if (symbol->automatic_character)
        return f2c_symbol_character_length(unit, symbol);
    if (symbol->argument && symbol->character_length != NULL &&
        strcmp(symbol->character_length, "*") == 0) {
        f2c_buffer_printf(&result, "f2c_len_%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (symbol->character_length != NULL)
        return f2c_emit_cached_expression(unit, symbol->character_length_expression,
                                          symbol->character_length);
    return f2c_strdup("1U");
}

char *f2c_character_source_pointer(Unit *unit, const F2cExpr *right, const char *right_code) {
    Buffer result = {0};
    const Symbol *symbol = right != NULL ? right->symbol : NULL;
    if (right == NULL || right_code == NULL)
        return NULL;
    if (right->kind == F2C_EXPR_STRING_LITERAL ||
        (right->kind == F2C_EXPR_NAME && symbol != NULL && symbol->parameter))
        return f2c_strdup(right_code);
    if (strncmp(right_code, "(char[", 6U) == 0)
        return f2c_strdup(right_code);
    if (right->kind == F2C_EXPR_NAME && symbol != NULL) {
        if (symbol->argument || symbol->rank != 0U || symbol->character_length != NULL)
            return f2c_strdup(f2c_symbol_c_name(unit, symbol));
        f2c_buffer_printf(&result, "&%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (right->kind == F2C_EXPR_COMPONENT) {
        if (symbol != NULL && symbol->rank != 0U && right->child_count > 1U)
            f2c_buffer_printf(&result, "&%s", right_code);
        else
            f2c_buffer_append(&result, right_code);
        return f2c_buffer_take(&result);
    }
    if (right->kind == F2C_EXPR_SUBSTRING && strncmp(right_code, "(&", 2U) == 0)
        return f2c_strdup(right_code);
    if (right->kind == F2C_EXPR_ARRAY_REFERENCE || right->kind == F2C_EXPR_SUBSTRING) {
        f2c_buffer_printf(&result, "&%s", right_code);
        return f2c_buffer_take(&result);
    }
    if (right->kind == F2C_EXPR_CALL && right->type == TYPE_CHARACTER) {
        if (right->text != NULL && strcmp(right->text, "char") == 0) {
            f2c_buffer_printf(&result, "&(char){%s}", right_code);
            return f2c_buffer_take(&result);
        }
        return f2c_strdup(right_code);
    }
    if (right->kind == F2C_EXPR_BINARY && right->type == TYPE_CHARACTER && right->text != NULL &&
        strcmp(right->text, "//") == 0)
        return f2c_strdup(right_code);
    f2c_buffer_printf(&result, "&(char){%s}", right_code);
    return f2c_buffer_take(&result);
}

char *f2c_emit_character_comparison(Unit *unit, const F2cExpr *left, const char *left_code,
                                    const char *operator_text, const F2cExpr *right,
                                    const char *right_code) {
    Buffer result = {0};
    char normalized[16];
    size_t normalized_length = 0U;
    const char *cursor;
    const char *comparison = NULL;
    char *left_pointer;
    char *right_pointer;
    char *left_length;
    char *right_length;
    if (left == NULL || right == NULL || left->type != TYPE_CHARACTER ||
        right->type != TYPE_CHARACTER || left_code == NULL || right_code == NULL ||
        operator_text == NULL)
        return NULL;
    for (cursor = operator_text; *cursor != '\0' && normalized_length + 1U < sizeof(normalized);
         ++cursor) {
        if (*cursor != ' ' && *cursor != '\t')
            normalized[normalized_length++] = *cursor;
    }
    normalized[normalized_length] = '\0';
    if (strcmp(normalized, ".eq.") == 0 || strcmp(normalized, "==") == 0)
        comparison = "==";
    else if (strcmp(normalized, ".ne.") == 0 || strcmp(normalized, "/=") == 0)
        comparison = "!=";
    else if (strcmp(normalized, ".lt.") == 0 || strcmp(normalized, "<") == 0)
        comparison = "<";
    else if (strcmp(normalized, ".le.") == 0 || strcmp(normalized, "<=") == 0)
        comparison = "<=";
    else if (strcmp(normalized, ".gt.") == 0 || strcmp(normalized, ">") == 0)
        comparison = ">";
    else if (strcmp(normalized, ".ge.") == 0 || strcmp(normalized, ">=") == 0)
        comparison = ">=";
    if (comparison == NULL)
        return NULL;
    left_pointer = f2c_character_source_pointer(unit, left, left_code);
    right_pointer = f2c_character_source_pointer(unit, right, right_code);
    left_length = f2c_character_length_expression(unit, left);
    right_length = f2c_character_length_expression(unit, right);
    if (left_pointer == NULL || right_pointer == NULL || left_length == NULL ||
        right_length == NULL) {
        free(left_pointer);
        free(right_pointer);
        free(left_length);
        free(right_length);
        return NULL;
    }
    f2c_buffer_printf(&result, "(f2c_character_compare(%s, (size_t)(%s), %s, (size_t)(%s)) %s 0)",
                      left_pointer, left_length, right_pointer, right_length, comparison);
    free(left_pointer);
    free(right_pointer);
    free(left_length);
    free(right_length);
    return f2c_buffer_take(&result);
}

char *f2c_emit_character_concatenation(Unit *unit, const F2cExpr *expression, const char *left_code,
                                       const char *right_code) {
    Buffer result = {0};
    char *left_pointer;
    char *right_pointer;
    char *left_length;
    char *right_length;
    if (expression == NULL || expression->kind != F2C_EXPR_BINARY ||
        expression->type != TYPE_CHARACTER || expression->text == NULL ||
        strcmp(expression->text, "//") != 0 || expression->child_count != 2U ||
        expression->temporary_index == SIZE_MAX)
        return NULL;
    left_pointer = f2c_character_source_pointer(unit, expression->children[0], left_code);
    right_pointer = f2c_character_source_pointer(unit, expression->children[1], right_code);
    left_length = f2c_character_length_expression(unit, expression->children[0]);
    right_length = f2c_character_length_expression(unit, expression->children[1]);
    if (left_pointer == NULL || right_pointer == NULL || left_length == NULL ||
        right_length == NULL) {
        free(left_pointer);
        free(right_pointer);
        free(left_length);
        free(right_length);
        return NULL;
    }
    f2c_buffer_printf(&result,
                      "(f2c_character_result_%zu = f2c_character_concatenation_resize("
                      "f2c_character_result_%zu, (size_t)(%s), (size_t)(%s)), "
                      "memmove(f2c_character_result_%zu, %s, (size_t)(%s)), "
                      "memmove(f2c_character_result_%zu + (size_t)(%s), %s, (size_t)(%s)), "
                      "f2c_character_result_%zu[(size_t)(%s) + (size_t)(%s)] = '\\0', "
                      "f2c_character_result_%zu)",
                      expression->temporary_index, expression->temporary_index, left_length,
                      right_length, expression->temporary_index, left_pointer, left_length,
                      expression->temporary_index, left_length, right_pointer, right_length,
                      expression->temporary_index, left_length, right_length,
                      expression->temporary_index);
    free(left_pointer);
    free(right_pointer);
    free(left_length);
    free(right_length);
    return f2c_buffer_take(&result);
}

static void emit_character_copy(Context *context, const char *target_pointer,
                                const char *target_length, const char *source_pointer,
                                const char *source_length, int depth) {
    emit_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emit_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_dst_len = (size_t)(%s);\n",
                      target_length);
    emit_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_src_len = (size_t)(%s);\n",
                      source_length);
    emit_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "const size_t f2c_copy_len = F2C_MIN(f2c_dst_len, f2c_src_len);\n");
    emit_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "memmove(%s, %s, f2c_copy_len);\n", target_pointer,
                      source_pointer);
    emit_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_dst_len > f2c_copy_len) memset(%s + f2c_copy_len, ' ', "
                      "f2c_dst_len - f2c_copy_len);\n",
                      target_pointer);
    emit_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

int f2c_emit_character_storage_assignment(Context *context, Unit *unit, const char *target_pointer,
                                          const char *target_length, const F2cExpr *right,
                                          const char *right_code, int depth) {
    char *source_length;
    char *source_pointer;
    if (context == NULL || unit == NULL || target_pointer == NULL || target_length == NULL ||
        right == NULL || right->type != TYPE_CHARACTER || right_code == NULL)
        return 0;
    source_length = f2c_character_length_expression(unit, right);
    source_pointer = f2c_character_source_pointer(unit, right, right_code);
    if (source_length == NULL || source_pointer == NULL) {
        free(source_length);
        free(source_pointer);
        return 0;
    }
    emit_character_copy(context, target_pointer, target_length, source_pointer, source_length,
                        depth);
    free(source_length);
    free(source_pointer);
    return 1;
}

int f2c_emit_character_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                  const F2cExpr *left, const F2cExpr *right, const char *left_code,
                                  const char *right_code, int depth) {
    char *target_length;
    char *source_length;
    char *source_pointer;
    char *target_pointer = NULL;
    Buffer target = {0};
    if (left_symbol == NULL || left_symbol->type != TYPE_CHARACTER || left == NULL ||
        (left->kind != F2C_EXPR_NAME && left->kind != F2C_EXPR_SUBSTRING &&
         left->kind != F2C_EXPR_ARRAY_REFERENCE && left->kind != F2C_EXPR_COMPONENT) ||
        !left->definable || right == NULL || right->type != TYPE_CHARACTER || left_code == NULL)
        return 0;
    if (left_symbol->deferred_character && left_symbol->rank == 0U && left->kind == F2C_EXPR_NAME) {
        const char *name = f2c_symbol_c_name(unit, left_symbol);
        source_length = f2c_character_length_expression(unit, right);
        source_pointer = f2c_character_source_pointer(unit, right, right_code);
        if (source_length == NULL || source_pointer == NULL) {
            free(source_length);
            free(source_pointer);
            return 0;
        }
        emit_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        emit_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_deferred_length = (size_t)(%s);\n",
                          source_length);
        emit_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_deferred_length == SIZE_MAX) abort();\n");
        emit_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "char *f2c_deferred_value = (char *)malloc(f2c_deferred_length + "
                          "1U);\n");
        emit_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_deferred_value == NULL) abort();\n");
        emit_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_deferred_length != 0U) memmove(f2c_deferred_value, %s, "
                          "f2c_deferred_length);\n",
                          source_pointer);
        emit_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "f2c_deferred_value[f2c_deferred_length] = '\\0';\n");
        emit_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "free(%s);\n", name);
        emit_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_deferred_value;\n", name);
        emit_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_deferred_length;\n", name);
        emit_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(source_length);
        free(source_pointer);
        return 1;
    }
    target_length = left->kind == F2C_EXPR_SUBSTRING || left->kind == F2C_EXPR_ARRAY_REFERENCE ||
                            left->kind == F2C_EXPR_COMPONENT
                        ? f2c_character_length_expression(unit, left)
                        : character_target_length(unit, left_symbol);
    source_length = f2c_character_length_expression(unit, right);
    source_pointer = f2c_character_source_pointer(unit, right, right_code);
    if (target_length == NULL || source_length == NULL || source_pointer == NULL) {
        free(target_length);
        free(source_length);
        free(source_pointer);
        return 0;
    }
    if (left->kind == F2C_EXPR_COMPONENT && left->rank == 0U) {
        if (left_symbol->rank != 0U && left->child_count > 1U)
            f2c_buffer_printf(&target, "&%s", left_code);
        else
            target_pointer = f2c_strdup(left_code);
    } else if (left->kind == F2C_EXPR_SUBSTRING || left->kind == F2C_EXPR_ARRAY_REFERENCE) {
        if (strncmp(left_code, "(&", 2U) == 0)
            target_pointer = f2c_strdup(left_code);
        else
            f2c_buffer_printf(&target, "&%s", left_code);
    } else if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
               unit->result_name != NULL && strcmp(left_symbol->name, unit->result_name) == 0) {
        target_pointer = f2c_strdup("f2c_result");
    } else if (left_symbol->argument || left_symbol->character_length != NULL) {
        target_pointer = f2c_strdup(f2c_symbol_c_name(unit, left_symbol));
    } else {
        f2c_buffer_printf(&target, "&%s", f2c_symbol_c_name(unit, left_symbol));
    }
    if (target_pointer == NULL)
        target_pointer = f2c_buffer_take(&target);
    if (target_pointer == NULL) {
        free(target_length);
        free(source_length);
        free(source_pointer);
        return 0;
    }
    emit_character_copy(context, target_pointer, target_length, source_pointer, source_length,
                        depth);
    if (left->kind == F2C_EXPR_NAME && !left_symbol->argument &&
        !(unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
          unit->result_name != NULL && strcmp(left_symbol->name, unit->result_name) == 0) &&
        left_symbol->character_length != NULL) {
        emit_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s[(size_t)(%s)] = '\\0';\n",
                          f2c_symbol_c_name(unit, left_symbol), target_length);
    }
    free(target_length);
    free(source_length);
    free(source_pointer);
    free(target_pointer);
    return 1;
}
