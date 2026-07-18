#include "ast/format.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define F2C_FORMAT_MAX_SOURCE_BYTES (16U * 1024U * 1024U)
#define F2C_FORMAT_MAX_NODES 1000000U
#define F2C_FORMAT_MAX_DEPTH 1024U

typedef struct FormatParser {
    const char *text;
    size_t length;
    size_t position;
    F2cSourceSpan span;
    F2cFormatError *error;
    size_t node_count;
    size_t depth;
} FormatParser;

static void discard_node(F2cFormatNode *node) {
    size_t index;
    if (node == NULL)
        return;
    for (index = 0U; index < node->child_count; ++index)
        discard_node(&node->children[index]);
    free(node->children);
    free(node->text);
    free(node->v_list);
    memset(node, 0, sizeof(*node));
}

static void set_error(FormatParser *parser, F2cFormatErrorCode code, size_t offset) {
    if (parser->error != NULL && parser->error->code == F2C_FORMAT_ERROR_NONE) {
        parser->error->code = code;
        parser->error->offset = offset <= parser->length ? offset : parser->length;
    }
}

static int failed(const FormatParser *parser) {
    return parser->error != NULL && parser->error->code != F2C_FORMAT_ERROR_NONE;
}

static void skip_spaces(FormatParser *parser) {
    while (parser->position < parser->length &&
           isspace((unsigned char)parser->text[parser->position]))
        ++parser->position;
}

static int peek(const FormatParser *parser) {
    return parser->position < parser->length ? (unsigned char)parser->text[parser->position] : 0;
}

static F2cSourceSpan item_span(const FormatParser *parser, size_t begin, size_t end) {
    F2cSourceSpan span = parser->span;
    if (span.begin.line != 0U && span.begin.line == span.end.line) {
        span.begin.column += begin;
        span.end.column = parser->span.begin.column + end;
    }
    return span;
}

static int append_child(FormatParser *parser, F2cFormatNode *parent, F2cFormatNode *child) {
    F2cFormatNode *items;
    size_t capacity;
    if (++parser->node_count > F2C_FORMAT_MAX_NODES) {
        set_error(parser, F2C_FORMAT_ERROR_BUDGET, parser->position);
        return 0;
    }
    if (parent->child_count == parent->child_capacity) {
        capacity = parent->child_capacity == 0U ? 8U : parent->child_capacity * 2U;
        if (capacity < parent->child_capacity || capacity > SIZE_MAX / sizeof(*items)) {
            set_error(parser, F2C_FORMAT_ERROR_MEMORY, parser->position);
            return 0;
        }
        items = (F2cFormatNode *)realloc(parent->children, capacity * sizeof(*items));
        if (items == NULL) {
            set_error(parser, F2C_FORMAT_ERROR_MEMORY, parser->position);
            return 0;
        }
        parent->children = items;
        parent->child_capacity = capacity;
    }
    parent->children[parent->child_count++] = *child;
    memset(child, 0, sizeof(*child));
    return 1;
}

static int parse_unsigned(FormatParser *parser, uint32_t *value, int *present) {
    uint32_t result = 0U;
    const size_t begin = parser->position;
    *present = 0;
    while (parser->position < parser->length &&
           isdigit((unsigned char)parser->text[parser->position])) {
        const uint32_t digit = (uint32_t)(parser->text[parser->position] - '0');
        *present = 1;
        if (result > (UINT32_MAX - digit) / 10U) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_NUMBER, begin);
            return 0;
        }
        result = result * 10U + digit;
        ++parser->position;
    }
    *value = result;
    return 1;
}

static int parse_required_unsigned(FormatParser *parser, int *value, int *has_value) {
    uint32_t number;
    int present;
    skip_spaces(parser);
    if (!parse_unsigned(parser, &number, &present))
        return 0;
    if (!present || number > (uint32_t)INT_MAX) {
        set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD, parser->position);
        return 0;
    }
    *value = (int)number;
    *has_value = 1;
    return 1;
}

static int append_byte(char **text, size_t *length, size_t *capacity, char value) {
    char *replacement;
    size_t next;
    if (*length == *capacity) {
        next = *capacity == 0U ? 32U : *capacity * 2U;
        if (next < *capacity)
            return 0;
        replacement = (char *)realloc(*text, next);
        if (replacement == NULL)
            return 0;
        *text = replacement;
        *capacity = next;
    }
    (*text)[(*length)++] = value;
    return 1;
}

static int parse_quoted(FormatParser *parser, char **text, size_t *length) {
    const int quote = peek(parser);
    size_t capacity = 0U;
    if (quote != '\'' && quote != '"')
        return 0;
    ++parser->position;
    while (parser->position < parser->length) {
        const char value = parser->text[parser->position++];
        if ((unsigned char)value == (unsigned char)quote) {
            if (peek(parser) == quote) {
                ++parser->position;
                if (!append_byte(text, length, &capacity, value))
                    goto memory_error;
                continue;
            }
            if (!append_byte(text, length, &capacity, '\0'))
                goto memory_error;
            --*length;
            return 1;
        }
        if (!append_byte(text, length, &capacity, value))
            goto memory_error;
    }
    free(*text);
    *text = NULL;
    *length = 0U;
    set_error(parser, F2C_FORMAT_ERROR_UNTERMINATED_LITERAL, parser->position);
    return 0;

memory_error:
    free(*text);
    *text = NULL;
    *length = 0U;
    set_error(parser, F2C_FORMAT_ERROR_MEMORY, parser->position);
    return 0;
}

static int append_v_list(FormatParser *parser, F2cFormatNode *node, int32_t value) {
    int32_t *replacement;
    const size_t count = node->v_list_count + 1U;
    if (count < node->v_list_count || count > SIZE_MAX / sizeof(*replacement)) {
        set_error(parser, F2C_FORMAT_ERROR_MEMORY, parser->position);
        return 0;
    }
    replacement = (int32_t *)realloc(node->v_list, count * sizeof(*replacement));
    if (replacement == NULL) {
        set_error(parser, F2C_FORMAT_ERROR_MEMORY, parser->position);
        return 0;
    }
    node->v_list = replacement;
    node->v_list[node->v_list_count++] = value;
    return 1;
}

static int parse_dt_v_list(FormatParser *parser, F2cFormatNode *node) {
    int expect_value = 1;
    ++parser->position;
    for (;;) {
        int negative = 0;
        uint32_t magnitude;
        int present;
        skip_spaces(parser);
        if (peek(parser) == ')' && !expect_value) {
            ++parser->position;
            return 1;
        }
        if (peek(parser) == '+' || peek(parser) == '-') {
            negative = peek(parser) == '-';
            ++parser->position;
        }
        if (!parse_unsigned(parser, &magnitude, &present) || !present ||
            magnitude > (uint32_t)INT32_MAX + (negative ? 1U : 0U)) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_DT_LIST, parser->position);
            return 0;
        }
        if (!append_v_list(parser, node,
                           negative ? (int32_t)(-(int64_t)magnitude) : (int32_t)magnitude))
            return 0;
        expect_value = 0;
        skip_spaces(parser);
        if (peek(parser) == ')') {
            ++parser->position;
            return 1;
        }
        if (peek(parser) != ',') {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_DT_LIST, parser->position);
            return 0;
        }
        ++parser->position;
        expect_value = 1;
    }
}

static int descriptor_boundary(const FormatParser *parser) {
    const int value = peek(parser);
    return value == 0 || value == ')' || value == ',' || value == '/' || value == ':' ||
           isspace(value);
}

static int parse_data_descriptor(FormatParser *parser, F2cFormatNode *node, int code) {
    int second = 0;
    node->kind = F2C_FORMAT_DATA;
    node->code[0] = (char)code;
    ++parser->position;
    if ((code == 'E') && (toupper(peek(parser)) == 'N' || toupper(peek(parser)) == 'S')) {
        second = toupper(peek(parser));
        ++parser->position;
    } else if (code == 'D' && toupper(peek(parser)) == 'T') {
        second = 'T';
        ++parser->position;
    }
    node->code[1] = (char)second;
    if (code == 'D' && second == 'T') {
        skip_spaces(parser);
        if (peek(parser) == '\'' || peek(parser) == '"') {
            if (!parse_quoted(parser, &node->text, &node->text_length))
                return 0;
        }
        skip_spaces(parser);
        if (peek(parser) == '(' && !parse_dt_v_list(parser, node))
            return 0;
        return 1;
    }
    skip_spaces(parser);
    if (code == 'A') {
        uint32_t width;
        int present;
        if (!parse_unsigned(parser, &width, &present))
            return 0;
        if (present) {
            if (width > (uint32_t)INT_MAX) {
                set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD, parser->position);
                return 0;
            }
            node->width = (int)width;
            node->has_width = 1;
        }
        return 1;
    }
    if (!parse_required_unsigned(parser, &node->width, &node->has_width))
        return 0;
    if (code == 'L')
        return 1;
    skip_spaces(parser);
    if (peek(parser) == '.') {
        ++parser->position;
        if (!parse_required_unsigned(parser, &node->digits, &node->has_digits))
            return 0;
    } else if (code == 'F' || code == 'E' || code == 'D' || (code == 'G' && node->width != 0)) {
        set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD, parser->position);
        return 0;
    }
    skip_spaces(parser);
    if ((code == 'E' || code == 'D' || code == 'G') && toupper(peek(parser)) == 'E') {
        ++parser->position;
        if (!parse_required_unsigned(parser, &node->exponent, &node->has_exponent))
            return 0;
    }
    if (!descriptor_boundary(parser)) {
        set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD, parser->position);
        return 0;
    }
    return 1;
}

static int parse_list(FormatParser *parser, F2cFormatNode *parent);

static int parse_group(FormatParser *parser, F2cFormatNode *node, uint32_t repeat, int unlimited,
                       size_t begin) {
    node->kind = F2C_FORMAT_GROUP;
    node->repeat = repeat;
    node->unlimited = unlimited;
    ++parser->position;
    if (++parser->depth > F2C_FORMAT_MAX_DEPTH) {
        set_error(parser, F2C_FORMAT_ERROR_BUDGET, begin);
        return 0;
    }
    if (!parse_list(parser, node))
        return 0;
    --parser->depth;
    return 1;
}

static int parse_item(FormatParser *parser, F2cFormatNode *node) {
    const size_t begin = parser->position;
    uint32_t prefix = 0U;
    int has_prefix = 0;
    int negative = 0;
    int signed_prefix = 0;
    int code;
    memset(node, 0, sizeof(*node));
    node->repeat = 1U;
    if (peek(parser) == '+' || peek(parser) == '-') {
        signed_prefix = 1;
        negative = peek(parser) == '-';
        ++parser->position;
    }
    if (!parse_unsigned(parser, &prefix, &has_prefix))
        return 0;
    skip_spaces(parser);
    code = toupper(peek(parser));
    if (code == '*' && !signed_prefix && !has_prefix) {
        ++parser->position;
        skip_spaces(parser);
        if (peek(parser) != '(') {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        if (!parse_group(parser, node, 1U, 1, begin))
            return 0;
    } else if (code == '(') {
        if (signed_prefix || (has_prefix && prefix == 0U)) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        if (!parse_group(parser, node, has_prefix ? prefix : 1U, 0, begin))
            return 0;
    } else if (has_prefix && !signed_prefix && code == 'H') {
        const size_t available = parser->length - (parser->position + 1U);
        ++parser->position;
        if (prefix == 0U || (size_t)prefix > available) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_HOLLERITH, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_LITERAL;
        node->text = (char *)malloc((size_t)prefix + 1U);
        if (node->text == NULL) {
            set_error(parser, F2C_FORMAT_ERROR_MEMORY, begin);
            return 0;
        }
        memcpy(node->text, parser->text + parser->position, prefix);
        node->text[prefix] = '\0';
        node->text_length = prefix;
        parser->position += prefix;
    } else if (code == '\'' || code == '"') {
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_LITERAL;
        if (!parse_quoted(parser, &node->text, &node->text_length))
            return 0;
    } else if (code == '/') {
        if (signed_prefix || (has_prefix && prefix == 0U)) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_RECORD;
        node->repeat = has_prefix ? prefix : 1U;
        ++parser->position;
    } else if (code == ':') {
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_COLON;
        ++parser->position;
    } else if (code == 'X') {
        if (signed_prefix || (has_prefix && prefix == 0U)) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_SPACE;
        node->control = (int)(has_prefix ? prefix : 1U);
        ++parser->position;
    } else if (code == 'T') {
        int mode = F2C_FORMAT_POSITION_ABSOLUTE;
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        ++parser->position;
        if (toupper(peek(parser)) == 'L' || toupper(peek(parser)) == 'R') {
            mode =
                toupper(peek(parser)) == 'L' ? F2C_FORMAT_POSITION_LEFT : F2C_FORMAT_POSITION_RIGHT;
            ++parser->position;
        }
        node->kind = F2C_FORMAT_POSITION;
        node->control = mode;
        if (!parse_required_unsigned(parser, &node->width, &node->has_width) || node->width <= 0) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD, begin);
            return 0;
        }
    } else if (code == 'P') {
        if (!has_prefix || prefix > (uint32_t)INT_MAX + (negative ? 1U : 0U)) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_SCALE;
        node->control =
            negative ? (prefix == (uint32_t)INT_MAX + 1U ? INT_MIN : -(int)prefix) : (int)prefix;
        ++parser->position;
    } else if (code == 'S') {
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_SIGN;
        node->control = F2C_FORMAT_SIGN_PROCESSOR;
        ++parser->position;
        if (toupper(peek(parser)) == 'P') {
            node->control = F2C_FORMAT_SIGN_PLUS;
            ++parser->position;
        } else if (toupper(peek(parser)) == 'S') {
            node->control = F2C_FORMAT_SIGN_SUPPRESS;
            ++parser->position;
        }
    } else if (code == 'B' && (toupper((unsigned char)(parser->position + 1U < parser->length
                                                           ? parser->text[parser->position + 1U]
                                                           : 0)) == 'N' ||
                               toupper((unsigned char)(parser->position + 1U < parser->length
                                                           ? parser->text[parser->position + 1U]
                                                           : 0)) == 'Z')) {
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_BLANK;
        node->control = toupper((unsigned char)parser->text[parser->position + 1U]) == 'Z'
                            ? F2C_FORMAT_BLANK_ZERO
                            : F2C_FORMAT_BLANK_NULL;
        parser->position += 2U;
    } else if (code == 'D' && (toupper((unsigned char)(parser->position + 1U < parser->length
                                                           ? parser->text[parser->position + 1U]
                                                           : 0)) == 'C' ||
                               toupper((unsigned char)(parser->position + 1U < parser->length
                                                           ? parser->text[parser->position + 1U]
                                                           : 0)) == 'P')) {
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_DECIMAL;
        node->control = toupper((unsigned char)parser->text[parser->position + 1U]) == 'C'
                            ? F2C_FORMAT_DECIMAL_COMMA
                            : F2C_FORMAT_DECIMAL_POINT;
        parser->position += 2U;
    } else if (code == 'R' && parser->position + 1U < parser->length &&
               strchr("UDZNCP", toupper((unsigned char)parser->text[parser->position + 1U])) !=
                   NULL) {
        static const char modes[] = "UDZNCP";
        const char *mode =
            strchr(modes, toupper((unsigned char)parser->text[parser->position + 1U]));
        if (signed_prefix || has_prefix) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->kind = F2C_FORMAT_ROUND;
        node->control = (int)(mode - modes);
        parser->position += 2U;
    } else if (strchr("IBOZFEDGLA", code) != NULL) {
        if (signed_prefix || (has_prefix && prefix == 0U)) {
            set_error(parser, F2C_FORMAT_ERROR_INVALID_REPEAT, begin);
            return 0;
        }
        node->repeat = has_prefix ? prefix : 1U;
        if (!parse_data_descriptor(parser, node, code))
            return 0;
    } else {
        set_error(parser, F2C_FORMAT_ERROR_INVALID_DESCRIPTOR, begin);
        return 0;
    }
    node->span = item_span(parser, begin, parser->position);
    return 1;
}

static int parse_list(FormatParser *parser, F2cFormatNode *parent) {
    for (;;) {
        F2cFormatNode child;
        skip_spaces(parser);
        while (peek(parser) == ',') {
            ++parser->position;
            skip_spaces(parser);
        }
        if (peek(parser) == ')') {
            ++parser->position;
            return 1;
        }
        if (peek(parser) == 0) {
            set_error(parser, F2C_FORMAT_ERROR_EXPECTED_RIGHT_PARENTHESIS, parser->position);
            return 0;
        }
        if (!parse_item(parser, &child)) {
            discard_node(&child);
            return 0;
        }
        if (!append_child(parser, parent, &child)) {
            discard_node(&child);
            return 0;
        }
    }
}

F2cFormat *f2c_format_parse(const char *text, size_t length, const F2cSourceSpan *span,
                            F2cFormatError *error) {
    FormatParser parser;
    F2cFormat *format;
    F2cFormatError local_error = {F2C_FORMAT_ERROR_NONE, 0U};
    if (error == NULL)
        error = &local_error;
    error->code = F2C_FORMAT_ERROR_NONE;
    error->offset = 0U;
    memset(&parser, 0, sizeof(parser));
    parser.text = text != NULL ? text : "";
    parser.length = length;
    parser.error = error;
    if (span != NULL)
        parser.span = *span;
    if (length > F2C_FORMAT_MAX_SOURCE_BYTES) {
        set_error(&parser, F2C_FORMAT_ERROR_BUDGET, 0U);
        return NULL;
    }
    format = (F2cFormat *)calloc(1U, sizeof(*format));
    if (format == NULL) {
        set_error(&parser, F2C_FORMAT_ERROR_MEMORY, 0U);
        return NULL;
    }
    format->source = (char *)malloc(length + 1U);
    if (format->source == NULL) {
        set_error(&parser, F2C_FORMAT_ERROR_MEMORY, 0U);
        f2c_format_free(format);
        return NULL;
    }
    memcpy(format->source, parser.text, length);
    format->source[length] = '\0';
    format->source_length = length;
    format->span = parser.span;
    format->root.kind = F2C_FORMAT_GROUP;
    format->root.repeat = 1U;
    format->root.span = parser.span;
    skip_spaces(&parser);
    if (peek(&parser) != '(') {
        set_error(&parser, F2C_FORMAT_ERROR_EXPECTED_LEFT_PARENTHESIS, parser.position);
        goto failed;
    }
    ++parser.position;
    parser.depth = 1U;
    if (!parse_list(&parser, &format->root))
        goto failed;
    skip_spaces(&parser);
    if (parser.position != parser.length) {
        set_error(&parser, F2C_FORMAT_ERROR_TRAILING_TEXT, parser.position);
        goto failed;
    }
    if (failed(&parser))
        goto failed;
    return format;

failed:
    f2c_format_free(format);
    return NULL;
}

F2cFormat *f2c_format_parse_character_literal(const char *literal, const F2cSourceSpan *span,
                                              F2cFormatError *error) {
    const char *quote = f2c_character_literal_quote(literal);
    const char delimiter = quote != NULL ? *quote : '\0';
    char *text = NULL;
    size_t length = 0U;
    size_t capacity = 0U;
    const char *cursor;
    F2cFormat *format;
    if (error != NULL) {
        error->code = F2C_FORMAT_ERROR_NONE;
        error->offset = 0U;
    }
    if (quote == NULL || (delimiter != '\'' && delimiter != '"')) {
        if (error != NULL)
            error->code = F2C_FORMAT_ERROR_UNTERMINATED_LITERAL;
        return NULL;
    }
    cursor = quote + 1;
    while (*cursor != '\0') {
        if (*cursor == delimiter) {
            if (cursor[1] == delimiter) {
                if (!append_byte(&text, &length, &capacity, delimiter))
                    goto memory_error;
                cursor += 2;
                continue;
            }
            if (cursor[1] != '\0') {
                if (error != NULL) {
                    error->code = F2C_FORMAT_ERROR_TRAILING_TEXT;
                    error->offset = (size_t)(cursor + 1 - literal);
                }
                free(text);
                return NULL;
            }
            format = f2c_format_parse(text, length, span, error);
            free(text);
            return format;
        }
        if (!append_byte(&text, &length, &capacity, *cursor++))
            goto memory_error;
    }
    if (error != NULL) {
        error->code = F2C_FORMAT_ERROR_UNTERMINATED_LITERAL;
        error->offset = (size_t)(cursor - literal);
    }
    free(text);
    return NULL;

memory_error:
    if (error != NULL) {
        error->code = F2C_FORMAT_ERROR_MEMORY;
        error->offset = (size_t)(cursor - literal);
    }
    free(text);
    return NULL;
}

const char *f2c_format_error_message(F2cFormatErrorCode code) {
    static const char *const messages[] = {
        [F2C_FORMAT_ERROR_NONE] = "valid format",
        [F2C_FORMAT_ERROR_MEMORY] = "out of memory",
        [F2C_FORMAT_ERROR_EXPECTED_LEFT_PARENTHESIS] = "expected opening parenthesis",
        [F2C_FORMAT_ERROR_EXPECTED_RIGHT_PARENTHESIS] = "expected closing parenthesis",
        [F2C_FORMAT_ERROR_EXPECTED_ITEM] = "expected format item",
        [F2C_FORMAT_ERROR_INVALID_REPEAT] = "invalid repeat specification",
        [F2C_FORMAT_ERROR_INVALID_NUMBER] = "format integer is out of range",
        [F2C_FORMAT_ERROR_UNTERMINATED_LITERAL] = "unterminated character literal",
        [F2C_FORMAT_ERROR_INVALID_HOLLERITH] = "invalid Hollerith edit descriptor",
        [F2C_FORMAT_ERROR_INVALID_DESCRIPTOR] = "unknown edit descriptor",
        [F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD] = "invalid edit descriptor field",
        [F2C_FORMAT_ERROR_INVALID_DT_LIST] = "invalid DT v-list",
        [F2C_FORMAT_ERROR_TRAILING_TEXT] = "unexpected text after format specification",
        [F2C_FORMAT_ERROR_BUDGET] = "format resource budget exceeded",
    };
    return (size_t)code < sizeof(messages) / sizeof(messages[0]) && messages[code] != NULL
               ? messages[code]
               : "invalid format specification";
}
