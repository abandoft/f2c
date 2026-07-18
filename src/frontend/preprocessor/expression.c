#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExpansionFrame {
    const char *name;
    size_t length;
    const struct ExpansionFrame *parent;
} ExpansionFrame;

typedef PreprocessorMacroArgument ConditionArgument;

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

static size_t quoted_end(const char *text, size_t length, size_t begin) {
    const char quote = text[begin];
    size_t cursor = begin + 1U;
    while (cursor < length) {
        if (text[cursor] == '\\' && cursor + 1U < length) {
            cursor += 2U;
            continue;
        }
        if (text[cursor++] == quote)
            break;
    }
    return cursor;
}

static int reserve_arguments(ConditionArgument **arguments, size_t *capacity, size_t count) {
    ConditionArgument *replacement;
    size_t next_capacity;
    if (count < *capacity)
        return 1;
    next_capacity = *capacity == 0U ? 4U : *capacity * 2U;
    if (next_capacity < *capacity || next_capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement = (ConditionArgument *)realloc(*arguments, next_capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    *arguments = replacement;
    *capacity = next_capacity;
    return 1;
}

static int append_argument(Preprocessor *preprocessor, ConditionArgument **arguments, size_t *count,
                           size_t *capacity, const char *text, size_t begin, size_t end,
                           size_t line, size_t column, const PreprocessorMacro *macro) {
    while (begin < end && isspace((unsigned char)text[begin]) != 0)
        ++begin;
    while (end > begin && isspace((unsigned char)text[end - 1U]) != 0)
        --end;
    if (*count >= preprocessor->context->limits.max_macro_arguments) {
        diagnose_name(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                      "conditional macro argument limit exceeded for", macro->name,
                      macro->name_length);
        return 0;
    }
    if (!reserve_arguments(arguments, capacity, *count)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while parsing conditional macro arguments");
        return 0;
    }
    (*arguments)[*count] = (ConditionArgument){text + begin, end - begin, {NULL, 0U, 0U}};
    ++*count;
    return 1;
}

static int parse_invocation_arguments(Preprocessor *preprocessor, const PreprocessorMacro *macro,
                                      const char *text, size_t length, size_t opening, size_t line,
                                      size_t column, ConditionArgument **arguments,
                                      size_t *argument_count, size_t *after) {
    size_t capacity = 0U;
    size_t begin = opening + 1U;
    size_t index = begin;
    size_t depth = 1U;
    int saw_separator = 0;
    *arguments = NULL;
    *argument_count = 0U;
    while (index < length) {
        if (text[index] == '\'' || text[index] == '"') {
            index = quoted_end(text, length, index);
            continue;
        }
        if (text[index] == '(') {
            if (depth >= preprocessor->context->limits.max_parse_depth) {
                diagnose_name(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column + opening,
                              "conditional macro invocation nesting limit exceeded for",
                              macro->name, macro->name_length);
                goto failure;
            }
            ++depth;
            ++index;
            continue;
        }
        if (text[index] == ')') {
            --depth;
            if (depth == 0U) {
                size_t nonspace = begin;
                while (nonspace < index && isspace((unsigned char)text[nonspace]) != 0)
                    ++nonspace;
                if (saw_separator || nonspace != index || macro->parameter_count != 0U ||
                    macro->variadic) {
                    if (!append_argument(preprocessor, arguments, argument_count, &capacity, text,
                                         begin, index, line, column + opening, macro)) {
                        goto failure;
                    }
                }
                if (macro->variadic ? *argument_count < macro->parameter_count
                                    : *argument_count != macro->parameter_count) {
                    diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + opening,
                                  "conditional macro argument count mismatch for", macro->name,
                                  macro->name_length);
                    goto failure;
                }
                *after = index + 1U;
                return 1;
            }
            ++index;
            continue;
        }
        if (text[index] == ',' && depth == 1U) {
            if (!append_argument(preprocessor, arguments, argument_count, &capacity, text, begin,
                                 index, line, column + opening, macro)) {
                goto failure;
            }
            saw_separator = 1;
            begin = ++index;
            continue;
        }
        ++index;
    }
    diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + opening,
                  "unterminated conditional macro invocation for", macro->name, macro->name_length);

failure:
    free(*arguments);
    *arguments = NULL;
    *argument_count = 0U;
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
            const size_t end = quoted_end(text, length, index);
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
                ConditionArgument *arguments = NULL;
                size_t argument_count = 0U;
                size_t after = end;
                size_t opening = end;
                Buffer operated_replacement = {0};
                const char *replacement_text = macro->value;
                size_t replacement_length = macro->value_length;
                if (macro->function_like) {
                    while (opening < length && isspace((unsigned char)text[opening]) != 0)
                        ++opening;
                    if (opening >= length || text[opening] != '(') {
                        f2c_buffer_append_n(output, text + index, end - index);
                        index = end;
                        continue;
                    }
                }
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
                if (macro->function_like &&
                    !parse_invocation_arguments(preprocessor, macro, text, length, opening, line,
                                                column, &arguments, &argument_count, &after))
                    return 0;
                if (macro->function_like || f2c_preprocessor_replacement_has_operator(macro)) {
                    F2cSourcePosition origin = {preprocessor->current_source_name, line,
                                                column + index};
                    if (!f2c_preprocessor_build_operator_replacement(preprocessor, macro, arguments,
                                                                     argument_count, origin,
                                                                     &operated_replacement)) {
                        free(arguments);
                        free(operated_replacement.data);
                        return 0;
                    }
                    replacement_text =
                        operated_replacement.data != NULL ? operated_replacement.data : "";
                    replacement_length = operated_replacement.length;
                }
                f2c_buffer_append_n(output, " ", 1U);
                if (!append_expanded_condition(preprocessor, replacement_text, replacement_length,
                                               line, column + index, output, &frame, depth + 1U)) {
                    free(arguments);
                    free(operated_replacement.data);
                    return 0;
                }
                free(arguments);
                free(operated_replacement.data);
                f2c_buffer_append_n(output, " ", 1U);
                end = macro->function_like ? after : end;
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

typedef struct IntegerValue {
    uint64_t unsigned_value;
    int64_t signed_value;
    int is_unsigned;
} IntegerValue;

static IntegerValue signed_integer(int64_t value) {
    IntegerValue result = {(uint64_t)value, value, 0};
    return result;
}

static IntegerValue unsigned_integer(uint64_t value) {
    IntegerValue result = {value, 0, 1};
    return result;
}

static uint64_t integer_unsigned(IntegerValue value) {
    return value.is_unsigned ? value.unsigned_value : (uint64_t)value.signed_value;
}

static int64_t signed_from_bits(uint64_t value) {
    if (value <= (uint64_t)INT64_MAX)
        return (int64_t)value;
    return -1 - (int64_t)(UINT64_MAX - value);
}

static int integer_true(IntegerValue value) {
    return value.is_unsigned ? value.unsigned_value != 0U : value.signed_value != 0;
}

static IntegerValue common_zero(IntegerValue left, IntegerValue right) {
    return left.is_unsigned || right.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
}

static int digit_value(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

static IntegerValue parse_integer_literal(ExpressionParser *parser) {
    const char *begin = parser->cursor;
    const char *cursor = begin;
    uint64_t value = 0U;
    int base = 10;
    int explicit_unsigned = 0;
    int long_seen = 0;
    int digits = 0;
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    } else if (cursor[0] == '0') {
        base = 8;
        ++cursor;
        digits = 1;
    }
    while (*cursor != '\0') {
        const int digit = digit_value(*cursor);
        if (digit < 0 || digit >= base)
            break;
        if (value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)base) {
            expression_error_at(parser, begin, "integer constant exceeds uintmax_t");
            return signed_integer(0);
        }
        value = value * (uint64_t)base + (uint64_t)digit;
        ++cursor;
        digits = 1;
    }
    if (!digits || (base == 8 && (*cursor == '8' || *cursor == '9'))) {
        expression_error_at(parser, begin, "invalid integer in preprocessor condition");
        return signed_integer(0);
    }
    while (*cursor == 'u' || *cursor == 'U' || *cursor == 'l' || *cursor == 'L') {
        if (*cursor == 'u' || *cursor == 'U') {
            if (explicit_unsigned) {
                expression_error_at(parser, cursor, "duplicate unsigned integer suffix");
                return signed_integer(0);
            }
            explicit_unsigned = 1;
            ++cursor;
        } else {
            int count = 1;
            const char suffix = *cursor++;
            if (*cursor == suffix) {
                ++cursor;
                ++count;
            }
            if (long_seen || count > 2) {
                expression_error_at(parser, cursor, "invalid long integer suffix");
                return signed_integer(0);
            }
            long_seen = count;
        }
    }
    if (identifier_continue(*cursor)) {
        expression_error_at(parser, cursor, "invalid integer suffix in preprocessor condition");
        return signed_integer(0);
    }
    parser->cursor = cursor;
    if (explicit_unsigned || (base != 10 && value > (uint64_t)INT64_MAX))
        return unsigned_integer(value);
    if (value > (uint64_t)INT64_MAX) {
        expression_error_at(parser, begin, "decimal integer constant exceeds intmax_t");
        return signed_integer(0);
    }
    return signed_integer((int64_t)value);
}

static int parse_escape(ExpressionParser *parser, const char **cursor, uint32_t *value) {
    const char *at = *cursor;
    int digit;
    if (*at != '\\') {
        *value = (unsigned char)*at;
        *cursor = at + 1;
        return 1;
    }
    ++at;
    switch (*at) {
    case '\'':
    case '"':
    case '?':
    case '\\':
        *value = (unsigned char)*at++;
        break;
    case 'a':
        *value = 7U;
        ++at;
        break;
    case 'b':
        *value = 8U;
        ++at;
        break;
    case 'f':
        *value = 12U;
        ++at;
        break;
    case 'n':
        *value = 10U;
        ++at;
        break;
    case 'r':
        *value = 13U;
        ++at;
        break;
    case 't':
        *value = 9U;
        ++at;
        break;
    case 'v':
        *value = 11U;
        ++at;
        break;
    case 'x': {
        uint32_t parsed = 0U;
        int count = 0;
        ++at;
        while ((digit = digit_value(*at)) >= 0) {
            if (parsed > (UINT32_MAX - (uint32_t)digit) / 16U) {
                expression_error_at(parser, at, "hexadecimal character escape is too large");
                return 0;
            }
            parsed = parsed * 16U + (uint32_t)digit;
            ++at;
            ++count;
        }
        if (count == 0) {
            expression_error_at(parser, at, "hexadecimal character escape has no digits");
            return 0;
        }
        *value = parsed;
        break;
    }
    case 'u':
    case 'U': {
        const int count = *at == 'u' ? 4 : 8;
        uint32_t parsed = 0U;
        int index;
        ++at;
        for (index = 0; index < count; ++index) {
            digit = digit_value(*at++);
            if (digit < 0) {
                expression_error_at(parser, at - 1, "invalid universal character escape");
                return 0;
            }
            parsed = parsed * 16U + (uint32_t)digit;
        }
        if (parsed > 0x10FFFFU || (parsed >= 0xD800U && parsed <= 0xDFFFU)) {
            expression_error_at(parser, at - count, "invalid universal character value");
            return 0;
        }
        *value = parsed;
        break;
    }
    default:
        if (*at < '0' || *at > '7') {
            expression_error_at(parser, at, "invalid character escape in preprocessor condition");
            return 0;
        }
        *value = 0U;
        for (digit = 0; digit < 3 && *at >= '0' && *at <= '7'; ++digit)
            *value = *value * 8U + (uint32_t)(*at++ - '0');
        break;
    }
    *cursor = at;
    return 1;
}

static IntegerValue parse_character_literal(ExpressionParser *parser, const char *quote) {
    const char *cursor = quote + 1;
    uint32_t value = 0U;
    if (*cursor == '\0' || *cursor == '\'') {
        expression_error_at(parser, quote, "empty character constant in preprocessor condition");
        return signed_integer(0);
    }
    if (!parse_escape(parser, &cursor, &value))
        return signed_integer(0);
    if (*cursor != '\'') {
        expression_error_at(
            parser, cursor,
            "multi-character constants are not portable in preprocessor conditions");
        return signed_integer(0);
    }
    parser->cursor = cursor + 1;
    return signed_integer((int64_t)value);
}

static IntegerValue parse_conditional(ExpressionParser *parser, int evaluate);

static IntegerValue parse_primary(ExpressionParser *parser, int evaluate) {
    const char *begin;
    expression_space(parser);
    begin = parser->cursor;
    if (*begin == '(') {
        IntegerValue value;
        ++parser->cursor;
        value = parse_conditional(parser, evaluate);
        if (!expression_consume(parser, ")"))
            expression_error(parser, "expected ')' in preprocessor condition");
        return value;
    }
    if (*begin == '\'' || ((*begin == 'L' || *begin == 'u' || *begin == 'U') && begin[1] == '\'')) {
        if (*begin != '\'')
            ++begin;
        return parse_character_literal(parser, begin);
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
                return signed_integer(0);
            }
            ++parser->cursor;
            while (identifier_continue(*parser->cursor))
                ++parser->cursor;
            length = (size_t)(parser->cursor - name);
            if (parenthesized && !expression_consume(parser, ")"))
                expression_error(parser, "expected ')' after defined operand");
            return signed_integer(evaluate &&
                                  find_macro(parser->preprocessor, name, length) != SIZE_MAX);
        }
        return signed_integer(0);
    }
    if (isdigit((unsigned char)*begin) != 0)
        return parse_integer_literal(parser);
    expression_error(parser, "expected an integer preprocessor expression");
    return signed_integer(0);
}

static IntegerValue parse_unary(ExpressionParser *parser, int evaluate) {
    if (expression_consume(parser, "!"))
        return signed_integer(!integer_true(parse_unary(parser, evaluate)));
    if (expression_consume(parser, "~")) {
        IntegerValue value = parse_unary(parser, evaluate);
        if (!evaluate)
            return value.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
        return value.is_unsigned ? unsigned_integer(~value.unsigned_value)
                                 : signed_integer(signed_from_bits(~(uint64_t)value.signed_value));
    }
    if (expression_consume(parser, "+"))
        return parse_unary(parser, evaluate);
    if (expression_consume(parser, "-")) {
        const char *operation_at = parser->cursor - 1;
        IntegerValue value = parse_unary(parser, evaluate);
        if (!evaluate)
            return value.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
        if (value.is_unsigned)
            return unsigned_integer(0U - value.unsigned_value);
        if (value.signed_value == INT64_MIN) {
            expression_error_at(parser, operation_at, "signed negation overflows intmax_t");
            return signed_integer(0);
        }
        return signed_integer(-value.signed_value);
    }
    return parse_primary(parser, evaluate);
}

static int signed_multiply(int64_t left, int64_t right, int64_t *result) {
    if (left == 0 || right == 0) {
        *result = 0;
        return 1;
    }
    if (left == -1 && right == INT64_MIN)
        return 0;
    if (right == -1 && left == INT64_MIN)
        return 0;
    if (left > 0 ? (right > 0 ? left > INT64_MAX / right : right < INT64_MIN / left)
                 : (right > 0 ? left < INT64_MIN / right : left < INT64_MAX / right))
        return 0;
    *result = left * right;
    return 1;
}

static IntegerValue parse_multiplicative(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_unary(parser, evaluate);
    for (;;) {
        char operation = '\0';
        const char *operation_at;
        IntegerValue right;
        const int common_unsigned = left.is_unsigned;
        expression_space(parser);
        operation_at = parser->cursor;
        if (*parser->cursor == '*' || *parser->cursor == '/' || *parser->cursor == '%')
            operation = *parser->cursor++;
        else
            return left;
        right = parse_unary(parser, evaluate);
        if (!evaluate) {
            left = common_zero(left, right);
            continue;
        }
        if (common_unsigned || right.is_unsigned) {
            const uint64_t l = integer_unsigned(left);
            const uint64_t r = integer_unsigned(right);
            if ((operation == '/' || operation == '%') && r == 0U) {
                expression_error_at(parser, operation_at,
                                    "division by zero in preprocessor condition");
                return signed_integer(0);
            }
            left = unsigned_integer(operation == '*' ? l * r : operation == '/' ? l / r : l % r);
        } else {
            int64_t result = 0;
            if ((operation == '/' || operation == '%') && right.signed_value == 0) {
                expression_error_at(parser, operation_at,
                                    "division by zero in preprocessor condition");
                return signed_integer(0);
            }
            if ((operation == '/' || operation == '%') && left.signed_value == INT64_MIN &&
                right.signed_value == -1) {
                expression_error_at(parser, operation_at, "signed division overflows intmax_t");
                return signed_integer(0);
            }
            if (operation == '*' &&
                !signed_multiply(left.signed_value, right.signed_value, &result)) {
                expression_error_at(parser, operation_at,
                                    "signed multiplication overflows intmax_t");
                return signed_integer(0);
            }
            if (operation == '/')
                result = left.signed_value / right.signed_value;
            else if (operation == '%')
                result = left.signed_value % right.signed_value;
            left = signed_integer(result);
        }
    }
}

static IntegerValue parse_additive(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_multiplicative(parser, evaluate);
    for (;;) {
        char operation;
        const char *operation_at;
        IntegerValue right;
        expression_space(parser);
        operation_at = parser->cursor;
        if (*parser->cursor == '+' || *parser->cursor == '-')
            operation = *parser->cursor++;
        else
            return left;
        right = parse_multiplicative(parser, evaluate);
        if (!evaluate) {
            left = common_zero(left, right);
        } else if (left.is_unsigned || right.is_unsigned) {
            const uint64_t l = integer_unsigned(left);
            const uint64_t r = integer_unsigned(right);
            left = unsigned_integer(operation == '+' ? l + r : l - r);
        } else {
            const int64_t l = left.signed_value;
            const int64_t r = right.signed_value;
            if ((operation == '+' &&
                 ((r > 0 && l > INT64_MAX - r) || (r < 0 && l < INT64_MIN - r))) ||
                (operation == '-' &&
                 ((r < 0 && l > INT64_MAX + r) || (r > 0 && l < INT64_MIN + r)))) {
                expression_error_at(parser, operation_at, "signed addition overflows intmax_t");
                return signed_integer(0);
            }
            left = signed_integer(operation == '+' ? l + r : l - r);
        }
    }
}

static IntegerValue parse_shift(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_additive(parser, evaluate);
    for (;;) {
        int right_shift;
        const char *operation_at = parser->cursor;
        IntegerValue right;
        uint64_t count;
        if (expression_consume(parser, "<<"))
            right_shift = 0;
        else if (expression_consume(parser, ">>"))
            right_shift = 1;
        else
            return left;
        right = parse_additive(parser, evaluate);
        if (!evaluate)
            continue;
        if ((!right.is_unsigned && right.signed_value < 0) || integer_unsigned(right) >= 64U) {
            expression_error_at(parser, operation_at,
                                "invalid shift count in preprocessor condition");
            return signed_integer(0);
        }
        count = integer_unsigned(right);
        if (left.is_unsigned) {
            left = unsigned_integer(right_shift ? left.unsigned_value >> count
                                                : left.unsigned_value << count);
        } else if (!right_shift) {
            if (left.signed_value < 0 ||
                (count != 0U && left.signed_value > (INT64_MAX >> count))) {
                expression_error_at(parser, operation_at, "signed left shift overflows intmax_t");
                return signed_integer(0);
            }
            left = signed_integer(left.signed_value << count);
        } else if (left.signed_value >= 0) {
            left = signed_integer(left.signed_value >> count);
        } else {
            left = signed_integer(-1 - ((-1 - left.signed_value) >> count));
        }
    }
}

static int integer_compare(IntegerValue left, IntegerValue right) {
    if (left.is_unsigned || right.is_unsigned) {
        const uint64_t l = integer_unsigned(left);
        const uint64_t r = integer_unsigned(right);
        return l < r ? -1 : l > r;
    }
    return left.signed_value < right.signed_value ? -1 : left.signed_value > right.signed_value;
}

static IntegerValue parse_relational(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_shift(parser, evaluate);
    for (;;) {
        int operation;
        IntegerValue right;
        int comparison;
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
        comparison = evaluate ? integer_compare(left, right) : 0;
        left = signed_integer(evaluate && (operation == 1   ? comparison <= 0
                                           : operation == 2 ? comparison >= 0
                                           : operation == 3 ? comparison < 0
                                                            : comparison > 0));
    }
}

static IntegerValue parse_equality(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_relational(parser, evaluate);
    for (;;) {
        int unequal;
        IntegerValue right;
        int equal;
        if (expression_consume(parser, "=="))
            unequal = 0;
        else if (expression_consume(parser, "!="))
            unequal = 1;
        else
            return left;
        right = parse_relational(parser, evaluate);
        equal = integer_compare(left, right) == 0;
        left = signed_integer(evaluate && (unequal ? !equal : equal));
    }
}

static IntegerValue bitwise_value(IntegerValue left, IntegerValue right, char operation,
                                  int evaluate) {
    if (!evaluate)
        return common_zero(left, right);
    if (left.is_unsigned || right.is_unsigned) {
        const uint64_t l = integer_unsigned(left);
        const uint64_t r = integer_unsigned(right);
        return unsigned_integer(operation == '&' ? l & r : operation == '^' ? l ^ r : l | r);
    }
    {
        const uint64_t l = (uint64_t)left.signed_value;
        const uint64_t r = (uint64_t)right.signed_value;
        const uint64_t bits = operation == '&' ? l & r : operation == '^' ? l ^ r : l | r;
        return signed_integer(signed_from_bits(bits));
    }
}

static IntegerValue parse_bitwise_and(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_equality(parser, evaluate);
    while (expression_single(parser, '&', '&'))
        left = bitwise_value(left, parse_equality(parser, evaluate), '&', evaluate);
    return left;
}

static IntegerValue parse_bitwise_xor(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_bitwise_and(parser, evaluate);
    while (expression_consume(parser, "^"))
        left = bitwise_value(left, parse_bitwise_and(parser, evaluate), '^', evaluate);
    return left;
}

static IntegerValue parse_bitwise_or(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_bitwise_xor(parser, evaluate);
    while (expression_single(parser, '|', '|'))
        left = bitwise_value(left, parse_bitwise_xor(parser, evaluate), '|', evaluate);
    return left;
}

static IntegerValue parse_logical_and(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_bitwise_or(parser, evaluate);
    while (expression_consume(parser, "&&")) {
        const int left_true = integer_true(left);
        const IntegerValue right = parse_bitwise_or(parser, evaluate && left_true);
        left = signed_integer(evaluate && left_true && integer_true(right));
    }
    return left;
}

static IntegerValue parse_logical_or(ExpressionParser *parser, int evaluate) {
    IntegerValue left = parse_logical_and(parser, evaluate);
    while (expression_consume(parser, "||")) {
        const int left_true = integer_true(left);
        const IntegerValue right = parse_logical_and(parser, evaluate && !left_true);
        left = signed_integer(evaluate && (left_true || integer_true(right)));
    }
    return left;
}

static IntegerValue parse_conditional(ExpressionParser *parser, int evaluate) {
    IntegerValue condition = parse_logical_or(parser, evaluate);
    if (expression_consume(parser, "?")) {
        const int selected = integer_true(condition);
        IntegerValue when_true = parse_conditional(parser, evaluate && selected);
        IntegerValue when_false;
        if (!expression_consume(parser, ":")) {
            expression_error(parser, "expected ':' in preprocessor conditional expression");
            return signed_integer(0);
        }
        when_false = parse_conditional(parser, evaluate && !selected);
        condition = selected ? when_true : when_false;
        if (when_true.is_unsigned || when_false.is_unsigned)
            condition = unsigned_integer(integer_unsigned(condition));
        if (!evaluate)
            condition = condition.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
    }
    return condition;
}

int f2c_preprocessor_evaluate_condition(Preprocessor *preprocessor, const char *text, size_t line,
                                        size_t column, int evaluate, int *condition) {
    Buffer expanded = {0};
    ExpressionParser parser;
    IntegerValue value;
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
    *condition = evaluate && integer_true(value);
    return 1;
}
