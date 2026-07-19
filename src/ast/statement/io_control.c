#include "ast/statement/private.h"

#include <stdlib.h>
#include <string.h>

typedef struct IoControlName {
    const char *name;
    F2cIoControlKind kind;
} IoControlName;

static const IoControlName control_names[] = {
    {"unit", F2C_IO_CONTROL_UNIT},
    {"fmt", F2C_IO_CONTROL_FMT},
    {"nml", F2C_IO_CONTROL_NML},
    {"end", F2C_IO_CONTROL_END},
    {"eor", F2C_IO_CONTROL_EOR},
    {"err", F2C_IO_CONTROL_ERR},
    {"iostat", F2C_IO_CONTROL_IOSTAT},
    {"iomsg", F2C_IO_CONTROL_IOMSG},
    {"size", F2C_IO_CONTROL_SIZE},
    {"advance", F2C_IO_CONTROL_ADVANCE},
    {"rec", F2C_IO_CONTROL_REC},
    {"pos", F2C_IO_CONTROL_POS},
    {"file", F2C_IO_CONTROL_FILE},
    {"status", F2C_IO_CONTROL_STATUS},
    {"access", F2C_IO_CONTROL_ACCESS},
    {"action", F2C_IO_CONTROL_ACTION},
    {"form", F2C_IO_CONTROL_FORM},
    {"recl", F2C_IO_CONTROL_RECL},
    {"blank", F2C_IO_CONTROL_BLANK},
    {"decimal", F2C_IO_CONTROL_DECIMAL},
    {"delim", F2C_IO_CONTROL_DELIM},
    {"encoding", F2C_IO_CONTROL_ENCODING},
    {"pad", F2C_IO_CONTROL_PAD},
    {"round", F2C_IO_CONTROL_ROUND},
    {"sign", F2C_IO_CONTROL_SIGN},
    {"asynchronous", F2C_IO_CONTROL_ASYNCHRONOUS},
    {"id", F2C_IO_CONTROL_ID},
    {"newunit", F2C_IO_CONTROL_NEWUNIT},
    {"iolength", F2C_IO_CONTROL_IOLENGTH},
    {"exist", F2C_IO_CONTROL_EXIST},
    {"opened", F2C_IO_CONTROL_OPENED},
    {"number", F2C_IO_CONTROL_NUMBER},
    {"named", F2C_IO_CONTROL_NAMED},
    {"name", F2C_IO_CONTROL_NAME},
    {"sequential", F2C_IO_CONTROL_SEQUENTIAL},
    {"direct", F2C_IO_CONTROL_DIRECT},
    {"formatted", F2C_IO_CONTROL_FORMATTED},
    {"unformatted", F2C_IO_CONTROL_UNFORMATTED},
    {"nextrec", F2C_IO_CONTROL_NEXTREC},
    {"position", F2C_IO_CONTROL_POSITION},
    {"read", F2C_IO_CONTROL_READ},
    {"write", F2C_IO_CONTROL_WRITE},
    {"readwrite", F2C_IO_CONTROL_READWRITE},
};

static F2cIoControlKind control_kind(const F2cToken *token) {
    size_t index;
    if (token == NULL)
        return F2C_IO_CONTROL_POSITIONAL;
    for (index = 0U; index < sizeof(control_names) / sizeof(control_names[0]); ++index)
        if (f2c_token_equals(token, control_names[index].name))
            return control_names[index].kind;
    return F2C_IO_CONTROL_UNKNOWN;
}

static F2cSourceSpan range_span(F2cTokenRange range) {
    F2cSourceSpan span = {0};
    if (range.count != 0U) {
        span.begin = range.tokens[0].span.begin;
        span.end = range.tokens[range.count - 1U].span.end;
        span.spelling_begin = range.tokens[0].span.spelling_begin;
        span.spelling_end = range.tokens[range.count - 1U].span.spelling_end;
        span.has_spelling =
            range.tokens[0].span.has_spelling || range.tokens[range.count - 1U].span.has_spelling;
    }
    return span;
}

static F2cExpr *parse_namelist_name(F2cTokenRange range) {
    F2cExpr *expression;
    if (range.count != 1U || range.tokens[0].kind != F2C_TOKEN_IDENTIFIER)
        return NULL;
    expression = (F2cExpr *)calloc(1U, sizeof(*expression));
    if (expression == NULL)
        return NULL;
    expression->kind = F2C_EXPR_NAME;
    expression->type = TYPE_UNKNOWN;
    expression->value_category = F2C_VALUE_INVALID;
    expression->shape.kind = F2C_SHAPE_SCALAR;
    expression->parse_error_offset = SIZE_MAX;
    expression->span = range_span(range);
    expression->text = f2c_token_text(&range.tokens[0]);
    expression->source = f2c_token_range_text(range);
    if (expression->text != NULL && expression->source != NULL)
        return expression;
    f2c_expr_free(expression);
    return NULL;
}

static int parse_control(Unit *unit, F2cTokenRange range, F2cIoControl *control) {
    const size_t equals = f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "=");
    F2cTokenRange value = range;
    memset(control, 0, sizeof(*control));
    control->span = range_span(range);
    control->format_span = control->span;
    if (range.count == 0U || !f2c_token_range_balanced(range.tokens, range.count))
        return 0;
    if (equals != SIZE_MAX) {
        if (equals != 1U || range.tokens[0].kind != F2C_TOKEN_IDENTIFIER ||
            equals + 1U == range.count)
            return 0;
        control->keyword = f2c_token_text(&range.tokens[0]);
        if (control->keyword == NULL)
            return 0;
        control->kind = control_kind(&range.tokens[0]);
        value = f2c_token_range_slice(range, equals + 1U, range.count);
    } else {
        control->kind = F2C_IO_CONTROL_POSITIONAL;
    }
    if (value.count == 1U && value.tokens[0].kind == F2C_TOKEN_OPERATOR &&
        f2c_token_equals(&value.tokens[0], "*")) {
        control->asterisk = 1;
        return 1;
    }
    control->value =
        control->kind == F2C_IO_CONTROL_NML
            ? parse_namelist_name(value)
            : f2c_parse_expression_tokens(unit, value.tokens, value.count, value.source, NULL);
    return control->value != NULL;
}

static int position_statement(F2cStatementKind kind) {
    return kind == F2C_STMT_REWIND || kind == F2C_STMT_BACKSPACE || kind == F2C_STMT_ENDFILE;
}

static int parse_control_list(Unit *unit, F2cTokenRange range, F2cStatement *statement) {
    F2cTokenRange *parts = NULL;
    size_t count = 0U;
    size_t index;
    if (!f2c_token_range_split_top_level(range, F2C_TOKEN_COMMA, NULL, &parts, &count) ||
        count == 0U)
        return 0;
    if (count > SIZE_MAX / sizeof(*statement->io_controls)) {
        free(parts);
        return 0;
    }
    statement->io_controls = (F2cIoControl *)calloc(count, sizeof(*statement->io_controls));
    if (statement->io_controls == NULL) {
        free(parts);
        return 0;
    }
    statement->control_count = count;
    for (index = 0U; index < count; ++index) {
        if (!parse_control(unit, parts[index], &statement->io_controls[index])) {
            free(parts);
            return 0;
        }
    }
    free(parts);
    return 1;
}

static int parse_io_items(Unit *unit, F2cTokenRange range, F2cStatement *statement) {
    F2cTokenRange *parts = NULL;
    size_t count = 0U;
    size_t index;
    if (range.count == 0U)
        return 1;
    if (!f2c_token_range_split_top_level(range, F2C_TOKEN_COMMA, NULL, &parts, &count) ||
        count == 0U || count > SIZE_MAX / sizeof(*statement->items) ||
        count > SIZE_MAX / sizeof(*statement->arguments) ||
        count > SIZE_MAX / sizeof(*statement->io_items)) {
        free(parts);
        return 0;
    }
    statement->items = (char **)calloc(count, sizeof(*statement->items));
    statement->arguments = (F2cExpr **)calloc(count, sizeof(*statement->arguments));
    statement->io_items = (F2cIoItem *)calloc(count, sizeof(*statement->io_items));
    if (statement->items == NULL || statement->arguments == NULL || statement->io_items == NULL) {
        free(parts);
        return 0;
    }
    statement->item_count = count;
    for (index = 0U; index < count; ++index) {
        statement->items[index] = f2c_token_range_text(parts[index]);
        statement->arguments[index] = f2c_parse_expression_tokens(
            unit, parts[index].tokens, parts[index].count, parts[index].source, NULL);
        if (statement->items[index] == NULL || statement->arguments[index] == NULL ||
            !f2c_statement_parse_io_item_tokens(unit, parts[index], &statement->io_items[index])) {
            free(parts);
            return 0;
        }
        ++statement->io_item_count;
    }
    free(parts);
    return 1;
}

static int has_control(const F2cStatement *statement, F2cIoControlKind kind) {
    size_t index;
    for (index = 0U; index < statement->control_count; ++index)
        if (statement->io_controls[index].kind == kind)
            return 1;
    return 0;
}

int f2c_statement_parse_io(Unit *unit, const Line *line, size_t body_start,
                           F2cStatement *statement) {
    F2cTokenRange line_range;
    F2cTokenRange controls;
    F2cTokenRange tail = {0};
    size_t close;
    size_t tail_begin;
    if (unit == NULL || line == NULL || statement == NULL || body_start >= line->token_count)
        return 0;
    line_range = f2c_line_token_range(line, body_start, line->token_count);
    if (line_range.count < 2U)
        return 0;
    if (line_range.tokens[1].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line_range.tokens, line_range.count, 1U, &close))
            return 0;
        controls = f2c_token_range_slice(line_range, 2U, close);
        tail_begin = close + 1U;
        if (tail_begin < line_range.count && line_range.tokens[tail_begin].kind == F2C_TOKEN_COMMA)
            ++tail_begin;
        tail = f2c_token_range_slice(line_range, tail_begin, line_range.count);
    } else if (position_statement(statement->kind)) {
        controls = f2c_token_range_slice(line_range, 1U, line_range.count);
    } else {
        return 0;
    }
    if (!parse_control_list(unit, controls, statement))
        return 0;
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        (statement->kind == F2C_STMT_INQUIRE && has_control(statement, F2C_IO_CONTROL_IOLENGTH))) {
        if (!parse_io_items(unit, tail, statement))
            return 0;
    } else if (tail.count != 0U) {
        return 0;
    }
    statement->io_syntax_valid = 1;
    return 1;
}

int f2c_statement_parse_print(Unit *unit, const Line *line, size_t body_start,
                              F2cStatement *statement) {
    F2cTokenRange line_range;
    F2cTokenRange format;
    F2cTokenRange items = {0};
    size_t comma;
    size_t format_end;
    if (unit == NULL || line == NULL || statement == NULL || body_start >= line->token_count)
        return 0;
    line_range = f2c_line_token_range(line, body_start, line->token_count);
    if (line_range.count < 2U)
        return 0;
    comma = f2c_token_range_find_top_level(line_range, 1U, F2C_TOKEN_COMMA, NULL);
    format_end = comma == SIZE_MAX ? line_range.count : comma;
    format = f2c_token_range_slice(line_range, 1U, format_end);
    if (format.count == 0U || !f2c_token_range_balanced(format.tokens, format.count))
        return 0;
    statement->io_controls = (F2cIoControl *)calloc(1U, sizeof(*statement->io_controls));
    if (statement->io_controls == NULL)
        return 0;
    statement->control_count = 1U;
    if (!parse_control(unit, format, &statement->io_controls[0]) ||
        statement->io_controls[0].kind != F2C_IO_CONTROL_POSITIONAL)
        return 0;
    statement->io_controls[0].kind = F2C_IO_CONTROL_FMT;
    if (comma != SIZE_MAX) {
        items = f2c_token_range_slice(line_range, comma + 1U, line_range.count);
        if (items.count == 0U || !parse_io_items(unit, items, statement))
            return 0;
    }
    statement->io_syntax_valid = 1;
    return 1;
}
