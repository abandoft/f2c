#include "frontend/preprocessor/condition/private.h"

void f2c_condition_error_code(ExpressionParser *parser, F2cDiagnosticCode code, const char *at,
                              const char *message) {
    if (parser->failed)
        return;
    parser->failed = 1;
    f2c_preprocessor_condition_diagnose(parser->preprocessor, code, parser->line,
                                        parser->column + (size_t)(at - parser->text), message);
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
    if (condition_identifier_continue(*cursor)) {
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

IntegerValue f2c_condition_atom(ExpressionParser *parser, int evaluate) {
    const char *begin;
    expression_space(parser);
    begin = parser->cursor;
    if (*begin == '\'' || ((*begin == 'L' || *begin == 'u' || *begin == 'U') && begin[1] == '\'')) {
        if (*begin != '\'')
            ++begin;
        return parse_character_literal(parser, begin);
    }
    if (condition_identifier_start(*begin)) {
        const char *end = begin + 1;
        while (condition_identifier_continue(*end))
            ++end;
        parser->cursor = end;
        if (condition_word_equal(begin, (size_t)(end - begin), "defined")) {
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
            if (!condition_identifier_start(*name)) {
                expression_error(parser, "expected a name after defined");
                return signed_integer(0);
            }
            ++parser->cursor;
            while (condition_identifier_continue(*parser->cursor))
                ++parser->cursor;
            length = (size_t)(parser->cursor - name);
            if (parenthesized && !expression_consume(parser, ")"))
                expression_error(parser, "expected ')' after defined operand");
            return signed_integer(evaluate && f2c_preprocessor_find_macro(
                                                  parser->preprocessor, name, length) != SIZE_MAX);
        }
        return signed_integer(0);
    }
    if (isdigit((unsigned char)*begin) != 0)
        return parse_integer_literal(parser);
    expression_error(parser, "expected an integer preprocessor expression");
    return signed_integer(0);
}
