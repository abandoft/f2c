#include "internal/f2c.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int identifier_start(char value) { return isalpha((unsigned char)value) || value == '_'; }

static int identifier_continue(char value) { return isalnum((unsigned char)value) || value == '_'; }

static int token_character_equal(char left, char right) {
    return tolower((unsigned char)left) == tolower((unsigned char)right);
}

void f2c_token_stream_init(F2cTokenStream *stream, const char *source, size_t line,
                           size_t base_column) {
    memset(stream, 0, sizeof(*stream));
    stream->source = source != NULL ? source : "";
    stream->cursor = stream->source;
    stream->line = line;
    stream->base_column = base_column;
    stream->token.kind = F2C_TOKEN_END;
    stream->token.begin = stream->source;
    stream->token.line = line;
    stream->token.column = base_column;
    stream->token.span.begin.line = line;
    stream->token.span.begin.column = base_column;
    stream->token.span.end = stream->token.span.begin;
}

static void set_error(F2cTokenStream *lexer, const char *at) {
    if (lexer->error_at == NULL)
        lexer->error_at = at;
}

static int valid_real_fraction(const char *cursor) {
    const char next = cursor[1];
    if (!isalpha((unsigned char)next))
        return 1;
    if (next == 'e' || next == 'E' || next == 'd' || next == 'D') {
        const char exponent = cursor[2];
        return isdigit((unsigned char)exponent) ||
               ((exponent == '+' || exponent == '-') && isdigit((unsigned char)cursor[3]));
    }
    return (next == 'f' || next == 'F') && !identifier_continue(cursor[2]);
}

static int scan_hollerith(F2cTokenStream *lexer, const char *begin, const char *digits_end) {
    char *end = NULL;
    unsigned long long count;
    const char *payload;
    size_t available;
    if ((*digits_end != 'h' && *digits_end != 'H') || digits_end == begin)
        return 0;
    errno = 0;
    count = strtoull(begin, &end, 10);
    if (errno != 0 || end != digits_end || count > (unsigned long long)SIZE_MAX) {
        set_error(lexer, begin);
        lexer->cursor = digits_end + 1;
        lexer->token.kind = F2C_TOKEN_INVALID;
        return 1;
    }
    payload = digits_end + 1;
    available = strlen(payload);
    if ((size_t)count > available) {
        set_error(lexer, begin);
        lexer->cursor = payload + available;
        lexer->token.kind = F2C_TOKEN_INVALID;
        return 1;
    }
    lexer->cursor = payload + (size_t)count;
    lexer->token.kind = F2C_TOKEN_HOLLERITH;
    return 1;
}

static void scan_number(F2cTokenStream *lexer, const char *begin) {
    const char *digits_end;
    int valid = 1;
    if (*lexer->cursor == '.')
        ++lexer->cursor;
    while (isdigit((unsigned char)*lexer->cursor))
        ++lexer->cursor;
    digits_end = lexer->cursor;
    if (*begin != '.' && scan_hollerith(lexer, begin, digits_end))
        return;
    if (*lexer->cursor == '.' && valid_real_fraction(lexer->cursor)) {
        ++lexer->cursor;
        while (isdigit((unsigned char)*lexer->cursor))
            ++lexer->cursor;
    }
    if (*lexer->cursor == 'e' || *lexer->cursor == 'E' || *lexer->cursor == 'd' ||
        *lexer->cursor == 'D' || *lexer->cursor == 'q' || *lexer->cursor == 'Q') {
        const char *exponent = lexer->cursor++;
        if (*lexer->cursor == '+' || *lexer->cursor == '-')
            ++lexer->cursor;
        if (!isdigit((unsigned char)*lexer->cursor)) {
            valid = 0;
            set_error(lexer, exponent);
        }
        while (isdigit((unsigned char)*lexer->cursor))
            ++lexer->cursor;
    }
    if (*lexer->cursor == '_') {
        const char *kind = lexer->cursor++;
        if (!identifier_continue(*lexer->cursor)) {
            valid = 0;
            set_error(lexer, kind);
        }
        while (identifier_continue(*lexer->cursor))
            ++lexer->cursor;
    }
    if (*lexer->cursor == 'f' || *lexer->cursor == 'F')
        ++lexer->cursor;
    lexer->token.kind = valid ? F2C_TOKEN_NUMBER : F2C_TOKEN_INVALID;
}

static void scan_string(F2cTokenStream *lexer, const char *begin) {
    const char quote = *lexer->cursor++;
    int closed = 0;
    while (*lexer->cursor != '\0') {
        if (*lexer->cursor++ == quote) {
            if (*lexer->cursor == quote) {
                ++lexer->cursor;
            } else {
                closed = 1;
                break;
            }
        }
    }
    lexer->token.kind = closed ? F2C_TOKEN_STRING : F2C_TOKEN_INVALID;
    if (!closed)
        set_error(lexer, begin);
}

static int scan_boz(F2cTokenStream *lexer, const char *begin) {
    const char prefix = (char)tolower((unsigned char)begin[0]);
    const char quote = begin[1];
    const char *cursor;
    if ((prefix != 'b' && prefix != 'o' && prefix != 'z' && prefix != 'x') ||
        (quote != '\'' && quote != '"'))
        return 0;
    cursor = begin + 2;
    while (*cursor != '\0' && *cursor != quote)
        ++cursor;
    if (*cursor != quote) {
        lexer->cursor = cursor;
        lexer->token.kind = F2C_TOKEN_INVALID;
        set_error(lexer, begin);
        return 1;
    }
    lexer->cursor = cursor + 1;
    lexer->token.kind = F2C_TOKEN_BOZ;
    return 1;
}

void f2c_token_stream_next(F2cTokenStream *lexer) {
    const char *begin;
    while (isspace((unsigned char)*lexer->cursor))
        ++lexer->cursor;
    begin = lexer->cursor;
    lexer->token.begin = begin;
    lexer->token.length = 0U;
    lexer->token.line = lexer->line;
    lexer->token.column = lexer->base_column + (size_t)(begin - lexer->source);
    lexer->token.span.begin.line = lexer->token.line;
    lexer->token.span.begin.column = lexer->token.column;
    lexer->token.span.end = lexer->token.span.begin;
    if (*begin == '\0' || *begin == '!') {
        lexer->token.kind = F2C_TOKEN_END;
        if (*begin == '!')
            lexer->cursor = begin + strlen(begin);
        return;
    }
    if (identifier_start(*begin)) {
        if (scan_boz(lexer, begin)) {
            lexer->token.length = (size_t)(lexer->cursor - begin);
            lexer->token.span.end.column = lexer->token.column + lexer->token.length;
            return;
        }
        ++lexer->cursor;
        while (identifier_continue(*lexer->cursor))
            ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_IDENTIFIER;
    } else if (isdigit((unsigned char)*begin) ||
               (*begin == '.' && isdigit((unsigned char)begin[1]))) {
        scan_number(lexer, begin);
    } else if (*begin == '\'' || *begin == '"') {
        scan_string(lexer, begin);
    } else if (*begin == '(' && begin[1] == '/') {
        lexer->cursor += 2;
        lexer->token.kind = F2C_TOKEN_ARRAY_BEGIN;
    } else if (*begin == '/' && begin[1] == ')') {
        lexer->cursor += 2;
        lexer->token.kind = F2C_TOKEN_ARRAY_END;
    } else if (*begin == '(') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_LEFT_PAREN;
    } else if (*begin == ')') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_RIGHT_PAREN;
    } else if (*begin == '[') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_LEFT_BRACKET;
    } else if (*begin == ']') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_RIGHT_BRACKET;
    } else if (*begin == ',') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_COMMA;
    } else if (*begin == ':' && begin[1] == ':') {
        lexer->cursor += 2;
        lexer->token.kind = F2C_TOKEN_DOUBLE_COLON;
    } else if (*begin == ':') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_COLON;
    } else if (*begin == ';') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_SEMICOLON;
    } else if (*begin == '%') {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_PERCENT;
    } else if (*begin == '.') {
        ++lexer->cursor;
        while (isspace((unsigned char)*lexer->cursor))
            ++lexer->cursor;
        while (isalpha((unsigned char)*lexer->cursor))
            ++lexer->cursor;
        while (isspace((unsigned char)*lexer->cursor))
            ++lexer->cursor;
        if (*lexer->cursor == '.') {
            ++lexer->cursor;
            lexer->token.kind = F2C_TOKEN_OPERATOR;
        } else {
            lexer->token.kind = F2C_TOKEN_INVALID;
            set_error(lexer, begin);
        }
    } else if (strchr("+-*/=<>~", (unsigned char)*begin) != NULL) {
        ++lexer->cursor;
        if ((begin[0] == '*' && begin[1] == '*') || (begin[0] == '/' && begin[1] == '/') ||
            (begin[0] == '/' && begin[1] == '=') || (begin[0] == '=' && begin[1] == '=') ||
            (begin[0] == '=' && begin[1] == '>') || (begin[0] == '<' && begin[1] == '=') ||
            (begin[0] == '>' && begin[1] == '=') || (begin[0] == '<' && begin[1] == '>') ||
            (begin[0] == '/' && begin[1] == '/'))
            ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_OPERATOR;
    } else {
        ++lexer->cursor;
        lexer->token.kind = F2C_TOKEN_INVALID;
        set_error(lexer, begin);
    }
    lexer->token.length = (size_t)(lexer->cursor - begin);
    lexer->token.span.end.column = lexer->token.column + lexer->token.length;
}

int f2c_token_equals(const F2cToken *token, const char *text) {
    const char *cursor;
    const char *end;
    if (token == NULL || text == NULL)
        return 0;
    cursor = token->begin;
    end = token->begin + token->length;
    while (cursor < end || *text != '\0') {
        while (cursor < end && isspace((unsigned char)*cursor))
            ++cursor;
        if (cursor == end || *text == '\0')
            return cursor == end && *text == '\0';
        if (!token_character_equal(*cursor++, *text++))
            return 0;
    }
    return 1;
}

char *f2c_token_text(const F2cToken *token) {
    return token != NULL ? f2c_strdup_n(token->begin, token->length) : NULL;
}

int f2c_hollerith_payload(const char *text, const char **payload, size_t *length) {
    const char *cursor = text;
    unsigned long long count = 0ULL;
    if (text == NULL || payload == NULL || length == NULL || !isdigit((unsigned char)*cursor))
        return 0;
    while (isdigit((unsigned char)*cursor)) {
        if (count > (ULLONG_MAX - (unsigned long long)(*cursor - '0')) / 10ULL)
            return 0;
        count = count * 10ULL + (unsigned long long)(*cursor - '0');
        ++cursor;
    }
    if ((*cursor != 'h' && *cursor != 'H') || count > (unsigned long long)SIZE_MAX ||
        (size_t)count > strlen(cursor + 1))
        return 0;
    *payload = cursor + 1;
    *length = (size_t)count;
    return 1;
}

static int push_token(Context *context, Line *line, size_t *capacity, F2cToken token) {
    F2cToken *replacement;
    if (context->limits.max_tokens != 0U && context->token_count >= context->limits.max_tokens) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &token.span, 1,
                                 "token limit of %zu exceeded", context->limits.max_tokens);
        return 0;
    }
    if (line->token_count == *capacity) {
        const size_t next = *capacity == 0U ? 16U : *capacity * 2U;
        if (next < *capacity || next > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement = (F2cToken *)realloc(line->tokens, next * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        line->tokens = replacement;
        *capacity = next;
    }
    line->tokens[line->token_count++] = token;
    ++context->token_count;
    return 1;
}

int f2c_tokenize_lines(Context *context) {
    size_t index;
    for (index = 0U; index < context->lines.count; ++index) {
        Line *line = &context->lines.items[index];
        F2cTokenStream lexer;
        size_t capacity = 0U;
        free(line->tokens);
        line->tokens = NULL;
        line->token_count = 0U;
        f2c_token_stream_init(&lexer, line->text, line->number, 1U);
        for (;;) {
            f2c_token_stream_next(&lexer);
            if (lexer.token.kind == F2C_TOKEN_END)
                break;
            lexer.token.span = f2c_line_source_span(line, (size_t)(lexer.token.begin - line->text),
                                                    lexer.token.length);
            lexer.token.line = lexer.token.span.begin.line;
            lexer.token.column = lexer.token.span.begin.column;
            if (!push_token(context, line, &capacity, lexer.token)) {
                if (context->result.error_count == 0U)
                    f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                        "out of memory tokenizing source");
                return 0;
            }
            if (lexer.token.kind == F2C_TOKEN_INVALID) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INVALID_TOKEN, &lexer.token.span,
                                         1, "invalid token in Fortran source");
                break;
            }
        }
    }
    return 1;
}
