#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExpansionFrame {
    const char *name;
    size_t length;
    const struct ExpansionFrame *parent;
} ExpansionFrame;

typedef struct ExpressionParser {
    Preprocessor *preprocessor;
    const char *text;
    const char *cursor;
    size_t line;
    size_t column;
    int failed;
} ExpressionParser;

static int identifier_start(char value) {
    return isalpha((unsigned char)value) != 0 || value == '_';
}

static int identifier_continue(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_';
}

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor) != 0)
        ++cursor;
    return cursor;
}

static int word_equal(const char *begin, size_t length, const char *word) {
    return strlen(word) == length && strncmp(begin, word, length) == 0;
}

static size_t find_macro(const Preprocessor *preprocessor, const char *name, size_t length) {
    size_t index;
    for (index = 0U; index < preprocessor->macro_count; ++index) {
        const PreprocessorMacro *macro = &preprocessor->macros[index];
        if (macro->name_length == length && strncmp(macro->name, name, length) == 0)
            return index;
    }
    return SIZE_MAX;
}

static void diagnose_at(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                        size_t column, const char *message) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    ++span.end.column;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s", message);
}

static void diagnose_name(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                          size_t column, const char *prefix_text, const char *name, size_t length) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    span.end.column += length != 0U ? length : 1U;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s '%.*s'", prefix_text,
                             (int)(length > (size_t)INT32_MAX ? (size_t)INT32_MAX : length), name);
}

static int expansion_contains(const ExpansionFrame *frame, const char *name, size_t length) {
    for (; frame != NULL; frame = frame->parent) {
        if (frame->length == length && strncmp(frame->name, name, length) == 0)
            return 1;
    }
    return 0;
}

static int append_expanded_condition(Preprocessor *preprocessor, const char *text, size_t length,
                                     size_t line, size_t column, Buffer *output,
                                     const ExpansionFrame *parent, size_t depth) {
    size_t index = 0U;
    int defined_operand = 0;
    while (index < length) {
        const char value = text[index];
        if (value == '\'' || value == '"') {
            const char quote = value;
            size_t end = index + 1U;
            while (end < length) {
                if (text[end] == '\\' && end + 1U < length) {
                    end += 2U;
                    continue;
                }
                if (text[end++] == quote)
                    break;
            }
            f2c_buffer_append_n(output, text + index, end - index);
            index = end;
            continue;
        }
        if (identifier_start(value)) {
            size_t end = index + 1U;
            size_t macro_index;
            while (end < length && identifier_continue(text[end]))
                ++end;
            if (word_equal(text + index, end - index, "defined")) {
                defined_operand = 1;
                f2c_buffer_append_n(output, text + index, end - index);
                index = end;
                continue;
            }
            if (defined_operand) {
                defined_operand = 0;
                f2c_buffer_append_n(output, text + index, end - index);
                index = end;
                continue;
            }
            macro_index = find_macro(preprocessor, text + index, end - index);
            if (macro_index != SIZE_MAX) {
                const PreprocessorMacro *macro = &preprocessor->macros[macro_index];
                ExpansionFrame frame;
                if (expansion_contains(parent, text + index, end - index)) {
                    diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + index,
                                  "cyclic conditional macro expansion for", text + index,
                                  end - index);
                    return 0;
                }
                if (depth >= preprocessor->context->limits.max_macro_expansion_depth) {
                    diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column + index,
                                "conditional macro expansion depth limit exceeded");
                    return 0;
                }
                frame.name = macro->name;
                frame.length = macro->name_length;
                frame.parent = parent;
                f2c_buffer_append_n(output, " ", 1U);
                if (!append_expanded_condition(preprocessor, macro->value, macro->value_length,
                                               line, column + index, output, &frame, depth + 1U))
                    return 0;
                f2c_buffer_append_n(output, " ", 1U);
            } else {
                f2c_buffer_append_n(output, text + index, end - index);
            }
            index = end;
            continue;
        }
        f2c_buffer_append_n(output, text + index, 1U);
        ++index;
    }
    if (output->failed || output->limit_exceeded) {
        diagnose_at(
            preprocessor,
            output->limit_exceeded ? F2C_DIAGNOSTIC_RESOURCE_LIMIT : F2C_DIAGNOSTIC_OUT_OF_MEMORY,
            line, column,
            output->limit_exceeded ? "expanded preprocessor expression is too large"
                                   : "out of memory while expanding a preprocessor expression");
        return 0;
    }
    return 1;
}

static void expression_error_at(ExpressionParser *parser, const char *at, const char *message) {
    if (parser->failed)
        return;
    parser->failed = 1;
    diagnose_at(parser->preprocessor, F2C_DIAGNOSTIC_SYNTAX, parser->line,
                parser->column + (size_t)(at - parser->text), message);
}

static void expression_error(ExpressionParser *parser, const char *message) {
    expression_error_at(parser, parser->cursor, message);
}

static void expression_space(ExpressionParser *parser) {
    parser->cursor = skip_space(parser->cursor);
}

static int expression_consume(ExpressionParser *parser, const char *token) {
    const size_t length = strlen(token);
    expression_space(parser);
    if (strncmp(parser->cursor, token, length) != 0)
        return 0;
    parser->cursor += length;
    return 1;
}

static int expression_single(ExpressionParser *parser, char token, char doubled) {
    expression_space(parser);
    if (parser->cursor[0] != token || parser->cursor[1] == doubled)
        return 0;
    ++parser->cursor;
    return 1;
}

static uint64_t parse_conditional(ExpressionParser *parser, int evaluate);

static uint64_t parse_primary(ExpressionParser *parser, int evaluate) {
    const char *begin;
    uint64_t value = 0U;
    expression_space(parser);
    begin = parser->cursor;
    if (*begin == '(') {
        ++parser->cursor;
        value = parse_conditional(parser, evaluate);
        if (!expression_consume(parser, ")"))
            expression_error(parser, "expected ')' in preprocessor condition");
        return value;
    }
    if (identifier_start(*begin)) {
        const char *end = begin + 1;
        while (identifier_continue(*end))
            ++end;
        parser->cursor = end;
        if (word_equal(begin, (size_t)(end - begin), "defined")) {
            const char *name;
            size_t length;
            int parenthesized;
            expression_space(parser);
            parenthesized = *parser->cursor == '(';
            if (parenthesized) {
                ++parser->cursor;
                expression_space(parser);
            }
            name = parser->cursor;
            if (!identifier_start(*name)) {
                expression_error(parser, "expected a name after defined");
                return 0U;
            }
            ++parser->cursor;
            while (identifier_continue(*parser->cursor))
                ++parser->cursor;
            length = (size_t)(parser->cursor - name);
            if (parenthesized && !expression_consume(parser, ")"))
                expression_error(parser, "expected ')' after defined operand");
            return evaluate && find_macro(parser->preprocessor, name, length) != SIZE_MAX ? 1U : 0U;
        }
        return 0U;
    }
    if (isdigit((unsigned char)*begin) != 0) {
        char *end = NULL;
        unsigned long long parsed;
        errno = 0;
        parsed = strtoull(begin, &end, 0);
        if (errno == ERANGE || end == begin) {
            expression_error(parser, "invalid integer in preprocessor condition");
            return 0U;
        }
        while (*end == 'u' || *end == 'U' || *end == 'l' || *end == 'L')
            ++end;
        parser->cursor = end;
        value = (uint64_t)parsed;
        return evaluate ? value : 0U;
    }
    expression_error(parser, "expected an integer preprocessor expression");
    return 0U;
}

static uint64_t parse_unary(ExpressionParser *parser, int evaluate) {
    if (expression_consume(parser, "!"))
        return parse_unary(parser, evaluate) == 0U ? 1U : 0U;
    if (expression_consume(parser, "~"))
        return evaluate ? ~parse_unary(parser, 1) : parse_unary(parser, 0);
    if (expression_consume(parser, "+"))
        return parse_unary(parser, evaluate);
    if (expression_consume(parser, "-"))
        return evaluate ? (uint64_t)(0U - parse_unary(parser, 1)) : parse_unary(parser, 0);
    return parse_primary(parser, evaluate);
}

static uint64_t parse_multiplicative(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_unary(parser, evaluate);
    for (;;) {
        char operation = '\0';
        const char *operation_at;
        uint64_t right;
        expression_space(parser);
        operation_at = parser->cursor;
        if (*parser->cursor == '*' || *parser->cursor == '/' || *parser->cursor == '%')
            operation = *parser->cursor++;
        else
            return left;
        right = parse_unary(parser, evaluate);
        if (!evaluate)
            continue;
        if ((operation == '/' || operation == '%') && right == 0U) {
            expression_error_at(parser, operation_at, "division by zero in preprocessor condition");
            return 0U;
        }
        if (operation == '*')
            left *= right;
        else if (operation == '/')
            left /= right;
        else
            left %= right;
    }
}

static uint64_t parse_additive(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_multiplicative(parser, evaluate);
    for (;;) {
        char operation = '\0';
        uint64_t right;
        expression_space(parser);
        if (*parser->cursor == '+' || *parser->cursor == '-')
            operation = *parser->cursor++;
        else
            return left;
        right = parse_multiplicative(parser, evaluate);
        if (evaluate)
            left = operation == '+' ? left + right : left - right;
    }
}

static uint64_t parse_shift(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_additive(parser, evaluate);
    for (;;) {
        int right_shift;
        const char *operation_at;
        uint64_t right;
        expression_space(parser);
        operation_at = parser->cursor;
        if (expression_consume(parser, "<<"))
            right_shift = 0;
        else if (expression_consume(parser, ">>"))
            right_shift = 1;
        else
            return left;
        right = parse_additive(parser, evaluate);
        if (!evaluate)
            continue;
        if (right >= 64U) {
            expression_error_at(parser, operation_at,
                                "invalid shift count in preprocessor condition");
            return 0U;
        }
        left = right_shift ? left >> right : left << right;
    }
}

static uint64_t parse_relational(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_shift(parser, evaluate);
    for (;;) {
        int operation = 0;
        uint64_t right;
        if (expression_consume(parser, "<="))
            operation = 1;
        else if (expression_consume(parser, ">="))
            operation = 2;
        else if (expression_single(parser, '<', '<'))
            operation = 3;
        else if (expression_single(parser, '>', '>'))
            operation = 4;
        else
            return left;
        right = parse_shift(parser, evaluate);
        if (evaluate) {
            if (operation == 1)
                left = left <= right;
            else if (operation == 2)
                left = left >= right;
            else if (operation == 3)
                left = left < right;
            else
                left = left > right;
        }
    }
}

static uint64_t parse_equality(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_relational(parser, evaluate);
    for (;;) {
        int unequal;
        uint64_t right;
        if (expression_consume(parser, "=="))
            unequal = 0;
        else if (expression_consume(parser, "!="))
            unequal = 1;
        else
            return left;
        right = parse_relational(parser, evaluate);
        if (evaluate)
            left = unequal ? left != right : left == right;
    }
}

static uint64_t parse_bitwise_and(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_equality(parser, evaluate);
    while (expression_single(parser, '&', '&')) {
        const uint64_t right = parse_equality(parser, evaluate);
        if (evaluate)
            left &= right;
    }
    return left;
}

static uint64_t parse_bitwise_xor(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_bitwise_and(parser, evaluate);
    while (expression_consume(parser, "^")) {
        const uint64_t right = parse_bitwise_and(parser, evaluate);
        if (evaluate)
            left ^= right;
    }
    return left;
}

static uint64_t parse_bitwise_or(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_bitwise_xor(parser, evaluate);
    while (expression_single(parser, '|', '|')) {
        const uint64_t right = parse_bitwise_xor(parser, evaluate);
        if (evaluate)
            left |= right;
    }
    return left;
}

static uint64_t parse_logical_and(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_bitwise_or(parser, evaluate);
    while (expression_consume(parser, "&&")) {
        const int evaluate_right = evaluate && left != 0U;
        const uint64_t right = parse_bitwise_or(parser, evaluate_right);
        if (evaluate)
            left = left != 0U && right != 0U;
    }
    return left;
}

static uint64_t parse_logical_or(ExpressionParser *parser, int evaluate) {
    uint64_t left = parse_logical_and(parser, evaluate);
    while (expression_consume(parser, "||")) {
        const int evaluate_right = evaluate && left == 0U;
        const uint64_t right = parse_logical_and(parser, evaluate_right);
        if (evaluate)
            left = left != 0U || right != 0U;
    }
    return left;
}

static uint64_t parse_conditional(ExpressionParser *parser, int evaluate) {
    uint64_t condition = parse_logical_or(parser, evaluate);
    if (expression_consume(parser, "?")) {
        const uint64_t when_true = parse_conditional(parser, evaluate && condition != 0U);
        uint64_t when_false;
        if (!expression_consume(parser, ":")) {
            expression_error(parser, "expected ':' in preprocessor conditional expression");
            return 0U;
        }
        when_false = parse_conditional(parser, evaluate && condition == 0U);
        if (evaluate)
            condition = condition != 0U ? when_true : when_false;
    }
    return condition;
}

int f2c_preprocessor_evaluate_condition(Preprocessor *preprocessor, const char *text, size_t line,
                                        size_t column, int evaluate, int *condition) {
    Buffer expanded = {0};
    ExpressionParser parser;
    uint64_t value;
    expanded.limit = preprocessor->context->limits.max_preprocessed_bytes;
    if (!append_expanded_condition(preprocessor, text, strlen(text), line, column, &expanded, NULL,
                                   0U)) {
        free(expanded.data);
        return 0;
    }
    memset(&parser, 0, sizeof(parser));
    parser.preprocessor = preprocessor;
    parser.text = expanded.data != NULL ? expanded.data : "";
    parser.cursor = parser.text;
    parser.line = line;
    parser.column = column;
    value = parse_conditional(&parser, evaluate);
    expression_space(&parser);
    if (!parser.failed && *parser.cursor != '\0')
        expression_error(&parser, "unexpected token in preprocessor condition");
    free(expanded.data);
    if (parser.failed)
        return 0;
    *condition = evaluate && value != 0U;
    return 1;
}
