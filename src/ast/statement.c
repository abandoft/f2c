#include "ast/statement/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int token_word(const Line *line, size_t index, const char *word) {
    return index < line->token_count && line->tokens[index].kind == F2C_TOKEN_IDENTIFIER &&
           f2c_token_equals(&line->tokens[index], word);
}

static int token_words(const Line *line, size_t begin, const char *first, const char *second) {
    return token_word(line, begin, first) && token_word(line, begin + 1U, second);
}

static size_t statement_body_start(const Line *line) {
    return line->token_count >= 3U && line->tokens[0].kind == F2C_TOKEN_IDENTIFIER &&
                   line->tokens[1].kind == F2C_TOKEN_COLON
               ? 2U
               : 0U;
}

static void set_statement_span(const Line *line, F2cStatement *statement) {
    if (line->token_count != 0U) {
        const F2cToken *first = &line->tokens[0];
        const F2cToken *last = &line->tokens[line->token_count - 1U];
        statement->span.begin = first->span.begin;
        statement->span.end = last->span.end;
        if (statement->span.begin.source_name == NULL)
            statement->span.begin.source_name = line->source_name;
        if (statement->span.end.source_name == NULL)
            statement->span.end.source_name = line->source_name;
        return;
    }
    statement->span.begin.source_name = line->source_name;
    statement->span.end.source_name = line->source_name;
    statement->span.begin.line = line->number;
    statement->span.end.line = line->number;
    statement->span.begin.column = 1U;
    statement->span.end.column = 1U;
}

static F2cStatementKind classify_tokens(const Line *line, size_t begin) {
    size_t index;
    if (begin >= line->token_count)
        return F2C_STMT_EMPTY;
    if (line->tokens[begin].kind == F2C_TOKEN_NUMBER)
        return begin == 0U ? F2C_STMT_LABEL : F2C_STMT_INVALID;
    if (token_words(line, begin, "type", "is") || token_words(line, begin, "class", "is") ||
        token_words(line, begin, "class", "default"))
        return F2C_STMT_TYPE_GUARD;
    if (begin == 0U && (f2c_declaration_tokens(line) || token_word(line, 0U, "contains") ||
                        token_word(line, 0U, "use")))
        return F2C_STMT_DECLARATION;
    if (token_words(line, begin, "end", "select") || token_word(line, begin, "endselect"))
        return F2C_STMT_END_SELECT;
    if (token_words(line, begin, "end", "where") || token_word(line, begin, "endwhere"))
        return F2C_STMT_END_WHERE;
    if (token_word(line, begin, "elsewhere"))
        return F2C_STMT_ELSEWHERE;
    if (token_word(line, begin, "where"))
        return F2C_STMT_WHERE;
    if (token_word(line, begin, "case"))
        return F2C_STMT_CASE;
    if (token_words(line, begin, "select", "case"))
        return F2C_STMT_SELECT_CASE;
    if (token_words(line, begin, "select", "type"))
        return F2C_STMT_SELECT_TYPE;
    if (token_words(line, begin, "end", "block"))
        return F2C_STMT_END_BLOCK_SCOPE;
    if (line->token_count == begin + 1U && token_word(line, begin, "block"))
        return F2C_STMT_BLOCK_SCOPE;
    if (token_words(line, begin, "end", "if") || token_word(line, begin, "endif"))
        return F2C_STMT_END_IF;
    if (token_words(line, begin, "end", "do") || token_word(line, begin, "enddo"))
        return F2C_STMT_END_DO;
    if (token_words(line, begin, "else", "if") || token_word(line, begin, "elseif"))
        return F2C_STMT_ELSE_IF;
    if (token_word(line, begin, "else"))
        return F2C_STMT_ELSE;
    if (token_word(line, begin, "if")) {
        for (index = begin + 1U; index < line->token_count; ++index) {
            if (line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN)
                return F2C_STMT_IF;
        }
    }
    if (token_words(line, begin, "do", "while"))
        return F2C_STMT_DO_WHILE;
    if (token_word(line, begin, "do"))
        return F2C_STMT_DO;
    if (token_word(line, begin, "write"))
        return F2C_STMT_WRITE;
    if (token_word(line, begin, "read"))
        return F2C_STMT_READ;
    if (token_word(line, begin, "print"))
        return F2C_STMT_PRINT;
    if (token_word(line, begin, "open"))
        return F2C_STMT_OPEN;
    if (token_word(line, begin, "rewind"))
        return F2C_STMT_REWIND;
    if (token_word(line, begin, "close"))
        return F2C_STMT_CLOSE;
    if (token_word(line, begin, "allocate"))
        return F2C_STMT_ALLOCATE;
    if (token_word(line, begin, "deallocate"))
        return F2C_STMT_DEALLOCATE;
    if (token_word(line, begin, "nullify"))
        return F2C_STMT_NULLIFY;
    if (token_word(line, begin, "data"))
        return F2C_STMT_DATA;
    if (token_word(line, begin, "call"))
        return F2C_STMT_CALL;
    if (token_word(line, begin, "return"))
        return F2C_STMT_RETURN;
    if (token_word(line, begin, "stop") || token_words(line, begin, "error", "stop"))
        return F2C_STMT_STOP;
    if (token_word(line, begin, "cycle"))
        return F2C_STMT_CYCLE;
    if (token_word(line, begin, "exit"))
        return F2C_STMT_EXIT;
    if (token_word(line, begin, "continue"))
        return F2C_STMT_CONTINUE;
    if (token_word(line, begin, "assign"))
        return F2C_STMT_ASSIGN_LABEL;
    if (token_word(line, begin, "goto") || token_words(line, begin, "go", "to"))
        return F2C_STMT_GOTO;
    return F2C_STMT_INVALID;
}

static void parse_counted_do(Unit *unit, F2cStatement *statement, char *syntax_text) {
    char *control = f2c_strdup(f2c_trim(syntax_text + 2));
    char *equals = control != NULL ? strchr(control, '=') : NULL;
    char **bounds = NULL;
    size_t count = 0U;
    if (equals == NULL) {
        free(control);
        return;
    }
    *equals = '\0';
    bounds = f2c_split_comma_list(equals + 1, &count);
    statement->left = f2c_parse_expression_ast(unit, f2c_trim(control), NULL);
    if (count >= 2U) {
        statement->right = f2c_parse_expression_ast(unit, f2c_trim(bounds[0]), NULL);
        statement->limit = f2c_parse_expression_ast(unit, f2c_trim(bounds[1]), NULL);
        statement->step =
            f2c_parse_expression_ast(unit, count >= 3U ? f2c_trim(bounds[2]) : "1", NULL);
    }
    while (count != 0U)
        free(bounds[--count]);
    free(bounds);
    free(control);
}

static void parse_assign_label(Unit *unit, F2cStatement *statement) {
    const char *cursor = f2c_trim(statement->text + strlen("assign"));
    const char *label_begin = cursor;
    size_t name_length = 0U;
    while (isdigit((unsigned char)*cursor))
        ++cursor;
    if (cursor == label_begin)
        return;
    statement->labels = (char **)calloc(1U, sizeof(*statement->labels));
    if (statement->labels == NULL)
        return;
    statement->labels[0] = f2c_strdup_n(label_begin, (size_t)(cursor - label_begin));
    if (statement->labels[0] == NULL)
        return;
    statement->label_count = 1U;
    cursor = f2c_trim((char *)cursor);
    if (!f2c_starts_word(cursor, "to"))
        return;
    cursor = f2c_trim((char *)cursor + strlen("to"));
    statement->name = f2c_identifier(cursor, &name_length);
    if (statement->name == NULL || *f2c_trim((char *)cursor + name_length) != '\0')
        return;
    statement->expression = f2c_parse_expression_ast(unit, statement->name, NULL);
}

static void parse_goto(Unit *unit, F2cStatement *statement) {
    char *target =
        f2c_strdup(f2c_trim(statement->text + (f2c_starts_word(statement->text, "goto") ? 4 : 5)));
    if (target == NULL)
        return;
    if (*target == '(') {
        char *close = f2c_statement_matching_parenthesis(target);
        if (close != NULL) {
            char *selector;
            *close = '\0';
            statement->labels = f2c_split_arguments(target + 1, &statement->label_count);
            selector = f2c_trim(close + 1);
            if (*selector == ',')
                selector = f2c_trim(selector + 1);
            statement->expression = f2c_parse_expression_ast(unit, selector, NULL);
        }
    } else if (isdigit((unsigned char)*target)) {
        statement->name = f2c_strdup(target);
    } else {
        const char *cursor = target;
        size_t name_length = 0U;
        statement->kind = F2C_STMT_ASSIGNED_GOTO;
        statement->name = f2c_identifier(cursor, &name_length);
        if (statement->name != NULL) {
            char *open;
            statement->expression = f2c_parse_expression_ast(unit, statement->name, NULL);
            cursor = f2c_trim((char *)cursor + name_length);
            if (*cursor == ',')
                cursor = f2c_trim((char *)cursor + 1);
            open = *cursor == '(' ? (char *)cursor : NULL;
            if (open != NULL) {
                char *close = f2c_statement_matching_parenthesis(open);
                if (close != NULL) {
                    *close = '\0';
                    statement->labels = f2c_split_arguments(open + 1, &statement->label_count);
                }
            }
        }
    }
    free(target);
}

char **f2c_statement_split_arguments(const char *text, size_t *count) {
    const char *cursor = text;
    const char *start = text;
    char **items = NULL;
    size_t item_count = 0U;
    size_t capacity = 0U;
    int depth = 0;
    int quote = 0;
    *count = 0U;
    for (;; ++cursor) {
        const int at_end = *cursor == '\0';
        if (!at_end && (*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            quote = quote == 0 ? (unsigned char)*cursor : 0;
        } else if (!at_end && quote == 0 && *cursor == '(') {
            ++depth;
        } else if (!at_end && quote == 0 && *cursor == ')') {
            --depth;
        }
        if (at_end || (*cursor == ',' && quote == 0 && depth == 0)) {
            char *item = f2c_strdup_n(start, (size_t)(cursor - start));
            char *clean;
            char **replacement;
            if (item == NULL)
                goto failed;
            clean = f2c_trim(item);
            if (clean != item)
                memmove(item, clean, strlen(clean) + 1U);
            if (*item != '\0') {
                if (item_count == capacity) {
                    capacity = capacity == 0U ? 4U : capacity * 2U;
                    replacement = (char **)realloc(items, capacity * sizeof(*items));
                    if (replacement == NULL) {
                        free(item);
                        goto failed;
                    }
                    items = replacement;
                }
                items[item_count++] = item;
            } else {
                free(item);
            }
            if (at_end)
                break;
            start = cursor + 1;
        }
    }
    *count = item_count;
    return items;

failed:
    while (item_count != 0U)
        free(items[--item_count]);
    free(items);
    return NULL;
}

static void parse_expression_list(Unit *unit, const char *source, int parenthesized,
                                  int preserve_empty, char ***items_out, F2cExpr ***arguments_out,
                                  size_t *count_out) {
    char **items;
    F2cExpr **arguments;
    size_t count = 0U;
    size_t i;
    if (source == NULL)
        return;
    items = parenthesized ? (preserve_empty ? f2c_split_actual_arguments(source, &count)
                                            : f2c_split_arguments(source, &count))
                          : f2c_statement_split_arguments(source, &count);
    if (count == 0U)
        return;
    arguments = (F2cExpr **)calloc(count, sizeof(*arguments));
    if (arguments == NULL) {
        while (count != 0U)
            free(items[--count]);
        free(items);
        return;
    }
    for (i = 0U; i < count; ++i) {
        if (items[i][0] == '\0') {
            arguments[i] = f2c_expr_new_absent(TYPE_UNKNOWN, 0U);
            if (arguments[i] != NULL)
                arguments[i]->source = f2c_strdup("");
            continue;
        }
        char *equals = f2c_find_assignment(items[i]);
        size_t consumed = 0U;
        char *keyword = equals != NULL ? f2c_identifier(items[i], &consumed) : NULL;
        const char *between = keyword != NULL ? items[i] + consumed : NULL;
        while (between != NULL && between < equals && isspace((unsigned char)*between))
            ++between;
        if (keyword != NULL && between == equals) {
            F2cExpr *value = f2c_parse_expression_ast(unit, equals + 1, NULL);
            F2cExpr *argument = (F2cExpr *)calloc(1U, sizeof(*argument));
            if (value != NULL && argument != NULL) {
                argument->kind = F2C_EXPR_KEYWORD_ARGUMENT;
                argument->type = value->type;
                argument->rank = value->rank;
                argument->definable = value->definable;
                argument->parse_error_offset = SIZE_MAX;
                argument->text = keyword;
                keyword = NULL;
                argument->source = f2c_strdup(items[i]);
                argument->children = (F2cExpr **)calloc(1U, sizeof(*argument->children));
                if (argument->source != NULL && argument->children != NULL) {
                    argument->children[0] = value;
                    argument->child_count = 1U;
                    arguments[i] = argument;
                    continue;
                }
            }
            free(keyword);
            f2c_expr_free(value);
            f2c_expr_free(argument);
        } else {
            free(keyword);
        }
        arguments[i] = f2c_parse_expression_ast(unit, items[i], NULL);
    }
    *items_out = items;
    *arguments_out = arguments;
    *count_out = count;
}

static void parse_item_list(Unit *unit, F2cStatement *statement, const char *source,
                            int parenthesized) {
    parse_expression_list(unit, source, parenthesized, 0, &statement->items, &statement->arguments,
                          &statement->item_count);
}

static char *find_top_level_double_colon(char *text) {
    int depth = 0;
    int quote = 0;
    char *cursor;
    for (cursor = text; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            quote = quote == 0 ? (unsigned char)*cursor : 0;
        } else if (quote == 0 && *cursor == '(') {
            ++depth;
        } else if (quote == 0 && *cursor == ')') {
            --depth;
        } else if (quote == 0 && depth == 0 && cursor[0] == ':' && cursor[1] == ':') {
            return cursor;
        }
    }
    return NULL;
}

static F2cExpr *parse_allocate_character_length(Unit *unit, const char *type_specification) {
    char *copy = f2c_strdup(type_specification);
    char *clean = copy != NULL ? f2c_trim(copy) : NULL;
    char *open;
    char *close;
    char *selector;
    char *value;
    F2cExpr *expression = NULL;
    if (clean == NULL || !f2c_starts_word(clean, "character"))
        goto cleanup;
    open = strchr(clean + strlen("character"), '(');
    close = open != NULL ? f2c_statement_matching_parenthesis(open) : NULL;
    if (open == NULL || close == NULL || *f2c_trim(close + 1) != '\0')
        goto cleanup;
    selector = f2c_strdup_n(open + 1, (size_t)(close - open - 1));
    if (selector == NULL)
        goto cleanup;
    value = f2c_trim(selector);
    if (strncmp(value, "len", strlen("len")) == 0 &&
        (value[strlen("len")] == '\0' || isspace((unsigned char)value[strlen("len")]) ||
         value[strlen("len")] == '=')) {
        value = f2c_trim(value + strlen("len"));
        if (*value == '=')
            value = f2c_trim(value + 1);
    }
    if (*value != '\0' && strcmp(value, ":") != 0 && strcmp(value, "*") != 0)
        expression = f2c_parse_expression_ast(unit, value, NULL);
    free(selector);
cleanup:
    free(copy);
    return expression;
}

static void parse_allocation_statement(Unit *unit, F2cStatement *statement) {
    char *open = strchr(statement->text, '(');
    char *close = open != NULL ? f2c_statement_matching_parenthesis(open) : NULL;
    char *inside;
    char *separator;
    char *entities;
    if (open == NULL || close == NULL)
        return;
    inside = f2c_strdup_n(open + 1, (size_t)(close - open - 1));
    if (inside == NULL)
        return;
    separator = find_top_level_double_colon(inside);
    if (separator != NULL && statement->kind == F2C_STMT_ALLOCATE) {
        *separator = '\0';
        separator[1] = '\0';
        statement->tail = f2c_strdup(f2c_trim(inside));
        statement->allocation_character_length =
            parse_allocate_character_length(unit, f2c_trim(inside));
        entities = f2c_trim(separator + 2);
        parse_item_list(unit, statement, entities, 0);
    } else {
        parse_item_list(unit, statement, open, 1);
    }
    free(inside);
}

static void parse_call(Unit *unit, F2cStatement *statement) {
    char *cursor = f2c_trim(statement->text + strlen("call"));
    char *open = strchr(cursor, '(');
    size_t consumed = 0U;
    if (open != NULL && memchr(cursor, '%', (size_t)(open - cursor)) != NULL) {
        char *designator = f2c_strdup_n(cursor, (size_t)(open - cursor));
        if (designator != NULL) {
            statement->expression = f2c_parse_expression_ast(unit, f2c_trim(designator), NULL);
            free(designator);
        }
        parse_expression_list(unit, open, 1, 1, &statement->items, &statement->arguments,
                              &statement->item_count);
        return;
    }
    statement->name = f2c_identifier(cursor, &consumed);
    if (statement->name == NULL)
        return;
    cursor = f2c_trim(cursor + consumed);
    if (*cursor == '(')
        parse_expression_list(unit, cursor, 1, 1, &statement->items, &statement->arguments,
                              &statement->item_count);
    if (strcmp(statement->name, "move_alloc") == 0)
        statement->kind = F2C_STMT_MOVE_ALLOC;
}

static F2cIoControlKind io_control_kind(const char *keyword) {
    static const struct {
        const char *name;
        F2cIoControlKind kind;
    } controls[] = {
        {"unit", F2C_IO_CONTROL_UNIT},     {"fmt", F2C_IO_CONTROL_FMT},
        {"nml", F2C_IO_CONTROL_NML},       {"end", F2C_IO_CONTROL_END},
        {"eor", F2C_IO_CONTROL_EOR},       {"err", F2C_IO_CONTROL_ERR},
        {"iostat", F2C_IO_CONTROL_IOSTAT}, {"iomsg", F2C_IO_CONTROL_IOMSG},
        {"size", F2C_IO_CONTROL_SIZE},     {"advance", F2C_IO_CONTROL_ADVANCE},
        {"rec", F2C_IO_CONTROL_REC},       {"pos", F2C_IO_CONTROL_POS},
        {"file", F2C_IO_CONTROL_FILE},     {"status", F2C_IO_CONTROL_STATUS},
        {"access", F2C_IO_CONTROL_ACCESS}, {"action", F2C_IO_CONTROL_ACTION},
        {"form", F2C_IO_CONTROL_FORM},     {"recl", F2C_IO_CONTROL_RECL},
        {"blank", F2C_IO_CONTROL_BLANK},   {"decimal", F2C_IO_CONTROL_DECIMAL},
        {"delim", F2C_IO_CONTROL_DELIM},   {"encoding", F2C_IO_CONTROL_ENCODING},
        {"pad", F2C_IO_CONTROL_PAD},       {"round", F2C_IO_CONTROL_ROUND},
        {"sign", F2C_IO_CONTROL_SIGN},     {"asynchronous", F2C_IO_CONTROL_ASYNCHRONOUS},
        {"id", F2C_IO_CONTROL_ID},         {"newunit", F2C_IO_CONTROL_NEWUNIT}};
    size_t i;
    if (keyword == NULL)
        return F2C_IO_CONTROL_POSITIONAL;
    for (i = 0U; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        if (strcmp(keyword, controls[i].name) == 0)
            return controls[i].kind;
    }
    return F2C_IO_CONTROL_UNKNOWN;
}

static void parse_io_controls(Unit *unit, F2cStatement *statement, const char *source) {
    char **items;
    size_t count = 0U;
    size_t i;
    items = f2c_split_arguments(source, &count);
    if (count == 0U)
        return;
    statement->io_controls = (F2cIoControl *)calloc(count, sizeof(*statement->io_controls));
    if (statement->io_controls == NULL)
        goto cleanup;
    for (i = 0U; i < count; ++i) {
        F2cIoControl *control = &statement->io_controls[i];
        char *clean = f2c_trim(items[i]);
        char *equals = f2c_find_assignment(clean);
        const char *value_text = clean;
        if (equals != NULL) {
            size_t consumed = 0U;
            const char *between;
            control->keyword = f2c_identifier(clean, &consumed);
            between = control->keyword != NULL ? clean + consumed : NULL;
            while (between != NULL && between < equals && isspace((unsigned char)*between))
                ++between;
            if (between != equals) {
                free(control->keyword);
                control->keyword = NULL;
                break;
            }
            value_text = f2c_trim(equals + 1);
        }
        control->kind = io_control_kind(control->keyword);
        if (strcmp(value_text, "*") == 0)
            control->asterisk = 1;
        else if (control->kind == F2C_IO_CONTROL_NML) {
            size_t consumed = 0U;
            char *name = f2c_identifier(value_text, &consumed);
            if (name != NULL && consumed == strlen(value_text)) {
                control->value = (F2cExpr *)calloc(1U, sizeof(*control->value));
                if (control->value != NULL) {
                    control->value->kind = F2C_EXPR_NAME;
                    control->value->type = TYPE_UNKNOWN;
                    control->value->value_category = F2C_VALUE_INVALID;
                    control->value->shape.kind = F2C_SHAPE_SCALAR;
                    control->value->parse_error_offset = SIZE_MAX;
                    control->value->text = name;
                    control->value->source = f2c_strdup(value_text);
                    name = NULL;
                }
            }
            free(name);
        } else
            control->value = f2c_parse_expression_ast(unit, value_text, NULL);
        if ((!control->asterisk && control->value == NULL) ||
            (control->keyword != NULL && control->keyword[0] == '\0'))
            break;
        ++statement->control_count;
    }

cleanup:
    while (count != 0U)
        free(items[--count]);
    free(items);
}

static void parse_io_statement(Unit *unit, F2cStatement *statement) {
    char *open = strchr(statement->text, '(');
    char *close = open != NULL ? f2c_statement_matching_parenthesis(open) : NULL;
    char *items;
    char *item_close;
    if (open == NULL || close == NULL)
        return;
    parse_io_controls(unit, statement, open);
    items = f2c_trim(close + 1);
    if (*items == ',')
        items = f2c_trim(items + 1);
    if (*items == '\0')
        return;
    item_close = *items == '(' ? f2c_statement_matching_parenthesis(items) : NULL;
    if (item_close != NULL && *f2c_trim(item_close + 1) == '\0') {
        statement->items = (char **)calloc(1U, sizeof(*statement->items));
        statement->arguments = (F2cExpr **)calloc(1U, sizeof(*statement->arguments));
        if (statement->items == NULL || statement->arguments == NULL)
            return;
        statement->items[0] = f2c_strdup(items);
        statement->arguments[0] = f2c_parse_expression_ast(unit, items, NULL);
        statement->item_count = 1U;
    } else {
        parse_item_list(unit, statement, items, 0);
    }
}

static void parse_print_statement(Unit *unit, F2cStatement *statement) {
    char *comma = strchr(statement->text, ',');
    if (comma != NULL)
        parse_item_list(unit, statement, comma + 1, 0);
}

static void free_io_item(F2cIoItem *item) {
    size_t i;
    if (item == NULL)
        return;
    free(item->text);
    f2c_expr_free(item->expression);
    for (i = 0U; i < item->child_count; ++i)
        free_io_item(&item->children[i]);
    free(item->children);
    f2c_expr_free(item->iterator);
    f2c_expr_free(item->initial);
    f2c_expr_free(item->limit);
    f2c_expr_free(item->step);
    memset(item, 0, sizeof(*item));
}

int f2c_statement_parse_io_item(Unit *unit, const char *text, F2cIoItem *item) {
    char *copy = f2c_strdup(text);
    char *clean = copy != NULL ? f2c_trim(copy) : NULL;
    char **parts = NULL;
    size_t count = 0U;
    size_t control;
    size_t i;
    if (clean == NULL)
        return 0;
    item->text = f2c_strdup(clean);
    if (*clean == '(') {
        char *close = f2c_statement_matching_parenthesis(clean);
        char *after = close != NULL ? f2c_trim(close + 1) : NULL;
        if (close != NULL && after != NULL && *after == '\0') {
            parts = f2c_split_arguments(clean, &count);
            for (control = 0U; control < count; ++control) {
                if (f2c_find_assignment(parts[control]) != NULL)
                    break;
            }
            if (control < count && control + 1U < count) {
                char *equals = f2c_find_assignment(parts[control]);
                *equals = '\0';
                item->implied_do = 1;
                item->child_count = control;
                item->children =
                    control != 0U ? (F2cIoItem *)calloc(control, sizeof(*item->children)) : NULL;
                if (control != 0U && item->children == NULL)
                    goto failed;
                for (i = 0U; i < control; ++i) {
                    if (!f2c_statement_parse_io_item(unit, parts[i], &item->children[i]))
                        goto failed;
                }
                item->iterator = f2c_parse_expression_ast(unit, f2c_trim(parts[control]), NULL);
                item->initial = f2c_parse_expression_ast(unit, f2c_trim(equals + 1), NULL);
                item->limit = f2c_parse_expression_ast(unit, f2c_trim(parts[control + 1U]), NULL);
                item->step = f2c_parse_expression_ast(
                    unit, control + 2U < count ? f2c_trim(parts[control + 2U]) : "1", NULL);
                if (item->iterator == NULL || item->initial == NULL || item->limit == NULL ||
                    item->step == NULL)
                    goto failed;
                while (count != 0U)
                    free(parts[--count]);
                free(parts);
                free(copy);
                return 1;
            }
        }
    }
    item->expression = f2c_parse_expression_ast(unit, clean, NULL);
    while (count != 0U)
        free(parts[--count]);
    free(parts);
    free(copy);
    return item->expression != NULL;

failed:
    while (count != 0U)
        free(parts[--count]);
    free(parts);
    free(copy);
    free_io_item(item);
    return 0;
}

static void build_io_item_ir(Unit *unit, F2cStatement *statement) {
    size_t i;
    if (statement->item_count == 0U)
        return;
    statement->io_items = (F2cIoItem *)calloc(statement->item_count, sizeof(*statement->io_items));
    if (statement->io_items == NULL)
        return;
    for (i = 0U; i < statement->item_count; ++i) {
        if (!f2c_statement_parse_io_item(unit, statement->items[i], &statement->io_items[i]))
            return;
        ++statement->io_item_count;
    }
}

static char *find_pointer_assignment(char *text) {
    F2cTokenStream lexer;
    int parenthesis_depth = 0;
    int bracket_depth = 0;
    f2c_token_stream_init(&lexer, text, 1U, 1U);
    for (;;) {
        f2c_token_stream_next(&lexer);
        if (lexer.token.kind == F2C_TOKEN_END)
            return NULL;
        if (lexer.token.kind == F2C_TOKEN_LEFT_PAREN)
            ++parenthesis_depth;
        else if (lexer.token.kind == F2C_TOKEN_RIGHT_PAREN && parenthesis_depth > 0)
            --parenthesis_depth;
        else if (lexer.token.kind == F2C_TOKEN_LEFT_BRACKET ||
                 lexer.token.kind == F2C_TOKEN_ARRAY_BEGIN)
            ++bracket_depth;
        else if ((lexer.token.kind == F2C_TOKEN_RIGHT_BRACKET ||
                  lexer.token.kind == F2C_TOKEN_ARRAY_END) &&
                 bracket_depth > 0)
            --bracket_depth;
        else if (lexer.token.kind == F2C_TOKEN_OPERATOR && parenthesis_depth == 0 &&
                 bracket_depth == 0 && f2c_token_equals(&lexer.token, "=>"))
            return (char *)lexer.token.begin;
    }
}

static int parse_statement(Unit *unit, const char *text, size_t line, F2cStatement *statement,
                           F2cStatementKind classified_kind, const Line *token_line,
                           size_t body_start) {
    char *equals;
    char *owned_syntax_text = NULL;
    char *syntax_text;
    Line syntax_line;
    memset(statement, 0, sizeof(*statement));
    statement->state = F2C_IR_SYNTAX;
    statement->line = line;
    statement->text = f2c_strdup(text != NULL ? text : "");
    if (statement->text == NULL)
        return 0;
    {
        char *trimmed = f2c_trim(statement->text);
        if (trimmed != statement->text)
            memmove(statement->text, trimmed, strlen(trimmed) + 1U);
    }
    statement->kind = classified_kind;
    syntax_text = statement->text;
    if (body_start < token_line->token_count && body_start != 0U) {
        owned_syntax_text = f2c_strdup(token_line->tokens[body_start].begin);
        if (owned_syntax_text == NULL)
            return 0;
        syntax_text = f2c_trim(owned_syntax_text);
    }
    if (statement->kind == F2C_STMT_IF || statement->kind == F2C_STMT_ELSE_IF ||
        statement->kind == F2C_STMT_DO_WHILE || statement->kind == F2C_STMT_SELECT_CASE ||
        statement->kind == F2C_STMT_SELECT_TYPE || statement->kind == F2C_STMT_WHERE ||
        (statement->kind == F2C_STMT_ELSEWHERE && body_start + 1U < token_line->token_count &&
         token_line->tokens[body_start + 1U].kind == F2C_TOKEN_LEFT_PAREN)) {
        statement->expression = f2c_statement_parse_parenthesized_tokens(
            unit, token_line, body_start,
            statement->kind == F2C_STMT_IF || statement->kind == F2C_STMT_ELSE_IF ||
                    statement->kind == F2C_STMT_WHERE
                ? &statement->tail
                : NULL);
    }
    if (statement->kind == F2C_STMT_TYPE_GUARD) {
        if (f2c_starts_word(statement->text, "class default")) {
            statement->name = f2c_strdup("default");
        } else {
            statement->name =
                f2c_strdup(f2c_starts_word(statement->text, "type is") ? "type" : "class");
            statement->expression =
                f2c_statement_parse_parenthesized_tokens(unit, token_line, body_start, NULL);
            if (statement->expression != NULL && statement->expression->kind == F2C_EXPR_NAME &&
                statement->expression->text != NULL)
                statement->guard_type = f2c_find_derived_type(unit, statement->expression->text);
        }
    }
    if (statement->kind == F2C_STMT_IF && statement->tail != NULL) {
        statement->block = f2c_starts_word(statement->tail, "then");
        if (!statement->block && f2c_statement_parse_arithmetic_labels(statement)) {
            statement->block = 0;
        } else if (!statement->block && statement->tail[0] != '\0') {
            statement->nested = (F2cStatement *)calloc(1U, sizeof(*statement->nested));
            if (statement->nested != NULL &&
                !f2c_parse_statement(unit, statement->tail, line, statement->nested)) {
                free(statement->nested);
                statement->nested = NULL;
            }
        }
    } else if (statement->kind == F2C_STMT_ELSE_IF) {
        statement->block = 1;
    }
    if (statement->kind == F2C_STMT_WHERE && statement->tail != NULL) {
        statement->block = statement->tail[0] == '\0';
        if (!statement->block) {
            statement->nested = (F2cStatement *)calloc(1U, sizeof(*statement->nested));
            if (statement->nested != NULL &&
                !f2c_parse_statement(unit, statement->tail, line, statement->nested)) {
                free(statement->nested);
                statement->nested = NULL;
            }
        }
    }
    if (statement->kind == F2C_STMT_DO)
        parse_counted_do(unit, statement, syntax_text);
    if (statement->kind == F2C_STMT_ASSIGN_LABEL)
        parse_assign_label(unit, statement);
    if (statement->kind == F2C_STMT_GOTO)
        parse_goto(unit, statement);
    if (statement->kind == F2C_STMT_CALL)
        parse_call(unit, statement);
    if (statement->kind == F2C_STMT_STOP) {
        const char *code_text;
        statement->error_stop = f2c_starts_word(statement->text, "error stop");
        code_text = f2c_trim(statement->text +
                             (statement->error_stop ? strlen("error stop") : strlen("stop")));
        if (*code_text != '\0' && *code_text != '\'' && *code_text != '"')
            statement->expression = f2c_parse_expression_ast(unit, code_text, NULL);
    }
    if (statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE)
        parse_allocation_statement(unit, statement);
    if (statement->kind == F2C_STMT_NULLIFY) {
        char *open = strchr(statement->text, '(');
        if (open != NULL)
            parse_item_list(unit, statement, open, 1);
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_CLOSE)
        parse_io_statement(unit, statement);
    if (statement->kind == F2C_STMT_PRINT)
        parse_print_statement(unit, statement);
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_PRINT)
        build_io_item_ir(unit, statement);
    if (statement->kind == F2C_STMT_DATA)
        f2c_statement_parse_data(unit, statement);
    syntax_line = *token_line;
    if (syntax_line.tokens != NULL)
        syntax_line.tokens += body_start;
    syntax_line.token_count -= body_start;
    if (statement->kind == F2C_STMT_CASE &&
        !f2c_statement_parse_case(unit, &syntax_line, statement)) {
        free(owned_syntax_text);
        return 0;
    }
    if (statement->kind == F2C_STMT_REWIND) {
        char *unit_text = f2c_trim(statement->text + strlen("rewind"));
        if (*unit_text == '(') {
            parse_io_statement(unit, statement);
        } else if (*unit_text != '\0') {
            statement->io_controls = (F2cIoControl *)calloc(1U, sizeof(*statement->io_controls));
            if (statement->io_controls != NULL) {
                statement->io_controls[0].kind = F2C_IO_CONTROL_POSITIONAL;
                statement->io_controls[0].value = f2c_parse_expression_ast(unit, unit_text, NULL);
                if (statement->io_controls[0].value != NULL)
                    statement->control_count = 1U;
            }
        }
    }
    if (statement->kind == F2C_STMT_LABEL)
        f2c_statement_parse_label(unit, statement);
    if (!f2c_statement_parse_construct_syntax(token_line, body_start, statement)) {
        free(owned_syntax_text);
        return 0;
    }
    if (body_start == 0U && statement->kind == F2C_STMT_INVALID &&
        (equals = find_pointer_assignment(statement->text)) != NULL) {
        char *left = f2c_strdup_n(statement->text, (size_t)(equals - statement->text));
        char *right = f2c_strdup(equals + 2);
        statement->kind = F2C_STMT_POINTER_ASSIGNMENT;
        if (left != NULL && right != NULL) {
            statement->left = f2c_parse_expression_ast(unit, f2c_trim(left), NULL);
            statement->right = f2c_parse_expression_ast(unit, f2c_trim(right), NULL);
            statement->items = (char **)calloc(2U, sizeof(*statement->items));
            if (statement->items != NULL) {
                statement->items[0] = f2c_strdup(f2c_trim(left));
                statement->items[1] = f2c_strdup(f2c_trim(right));
                statement->item_count = 2U;
            }
        }
        free(left);
        free(right);
    } else if (body_start == 0U && statement->kind == F2C_STMT_INVALID &&
               (equals = f2c_find_assignment(statement->text)) != NULL) {
        char *left = f2c_strdup_n(statement->text, (size_t)(equals - statement->text));
        char *right = f2c_strdup(equals + 1);
        statement->kind = F2C_STMT_ASSIGNMENT;
        if (left != NULL && right != NULL) {
            statement->items = (char **)calloc(2U, sizeof(*statement->items));
            if (statement->items != NULL) {
                statement->items[0] = f2c_strdup(f2c_trim(left));
                statement->items[1] = f2c_strdup(f2c_trim(right));
                statement->item_count = 2U;
            }
            statement->left = f2c_parse_expression_ast(unit, f2c_trim(left), NULL);
            statement->right = f2c_parse_expression_ast(unit, f2c_trim(right), NULL);
        }
        free(left);
        free(right);
    }
    free(owned_syntax_text);
    return 1;
}

int f2c_parse_statement(Unit *unit, const char *text, size_t line, F2cStatement *statement) {
    Line token_line;
    int result;
    size_t body_start;
    if (!f2c_statement_tokenize_transient(text != NULL ? text : "", line, &token_line))
        return 0;
    body_start = statement_body_start(&token_line);
    result = parse_statement(unit, text, line, statement, classify_tokens(&token_line, body_start),
                             &token_line, body_start);
    if (result)
        set_statement_span(&token_line, statement);
    f2c_statement_release_transient(&token_line);
    return result;
}

int f2c_parse_statement_tokens(Unit *unit, const Line *line, F2cStatement *statement) {
    size_t index;
    size_t body_start;
    if (line == NULL || statement == NULL)
        return 0;
    for (index = 0U; index < line->token_count; ++index) {
        if (line->tokens[index].kind == F2C_TOKEN_INVALID)
            return 0;
    }
    body_start = statement_body_start(line);
    if (!parse_statement(unit, line->text, line->number, statement,
                         classify_tokens(line, body_start), line, body_start))
        return 0;
    set_statement_span(line, statement);
    return 1;
}

void f2c_statement_free(F2cStatement *statement) {
    if (statement == NULL)
        return;
    free(statement->text);
    free(statement->tail);
    free(statement->name);
    free(statement->construct_name);
    free(statement->control_name);
    while (statement->item_count != 0U) {
        --statement->item_count;
        free(statement->items[statement->item_count]);
        if (statement->arguments != NULL)
            f2c_expr_free(statement->arguments[statement->item_count]);
    }
    free(statement->items);
    free(statement->arguments);
    while (statement->control_count != 0U) {
        F2cIoControl *control = &statement->io_controls[--statement->control_count];
        free(control->keyword);
        f2c_expr_free(control->value);
    }
    free(statement->io_controls);
    while (statement->io_item_count != 0U)
        free_io_item(&statement->io_items[--statement->io_item_count]);
    free(statement->io_items);
    while (statement->data_group_count != 0U) {
        F2cDataGroup *group = &statement->data_groups[--statement->data_group_count];
        size_t i;
        for (i = 0U; i < group->target_count; ++i)
            free_io_item(&group->targets[i]);
        free(group->targets);
        for (i = 0U; i < group->value_count; ++i) {
            free(group->values[i].text);
            f2c_expr_free(group->values[i].expression);
            f2c_expr_free(group->values[i].repeat);
        }
        free(group->values);
    }
    free(statement->data_groups);
    while (statement->case_range_count != 0U) {
        F2cCaseRange *range = &statement->case_ranges[--statement->case_range_count];
        f2c_expr_free(range->lower);
        f2c_expr_free(range->upper);
    }
    free(statement->case_ranges);
    f2c_expr_free(statement->expression);
    f2c_expr_free(statement->left);
    f2c_expr_free(statement->right);
    f2c_expr_free(statement->limit);
    f2c_expr_free(statement->step);
    f2c_expr_free(statement->allocation_character_length);
    while (statement->label_count != 0U)
        free(statement->labels[--statement->label_count]);
    free(statement->labels);
    if (statement->nested != NULL) {
        f2c_statement_free(statement->nested);
        free(statement->nested);
    }
    memset(statement, 0, sizeof(*statement));
}
