#include "ast/statement/private.h"

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
    if (token_word(line, begin, "backspace"))
        return F2C_STMT_BACKSPACE;
    if (token_word(line, begin, "endfile"))
        return F2C_STMT_ENDFILE;
    if (token_word(line, begin, "inquire"))
        return F2C_STMT_INQUIRE;
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

static int parse_statement(Unit *unit, const char *text, size_t line, F2cStatement *statement,
                           F2cStatementKind classified_kind, const Line *token_line,
                           size_t body_start) {
    Line syntax_line;
    memset(statement, 0, sizeof(*statement));
    statement->state = F2C_IR_SYNTAX;
    statement->control_syntax_valid = 1;
    statement->action_syntax_valid = 1;
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
    if (statement->kind == F2C_STMT_ELSE_IF || statement->kind == F2C_STMT_DO_WHILE ||
        statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
        statement->kind == F2C_STMT_WHERE ||
        (statement->kind == F2C_STMT_ELSEWHERE && body_start + 1U < token_line->token_count &&
         token_line->tokens[body_start + 1U].kind == F2C_TOKEN_LEFT_PAREN)) {
        statement->expression = f2c_statement_parse_parenthesized_tokens(
            unit, token_line, body_start,
            statement->kind == F2C_STMT_ELSE_IF || statement->kind == F2C_STMT_WHERE
                ? &statement->tail
                : NULL);
    }
    if (statement->kind == F2C_STMT_TYPE_GUARD) {
        if (token_words(token_line, body_start, "class", "default")) {
            statement->name = f2c_strdup("default");
        } else {
            statement->name =
                f2c_strdup(token_words(token_line, body_start, "type", "is") ? "type" : "class");
            statement->expression =
                f2c_statement_parse_parenthesized_tokens(unit, token_line, body_start, NULL);
            if (statement->expression != NULL && statement->expression->kind == F2C_EXPR_NAME &&
                statement->expression->text != NULL)
                statement->guard_type = f2c_find_derived_type(unit, statement->expression->text);
        }
    }
    if (statement->kind == F2C_STMT_ELSE_IF) {
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
    if ((statement->kind == F2C_STMT_IF || statement->kind == F2C_STMT_DO ||
         statement->kind == F2C_STMT_ASSIGN_LABEL || statement->kind == F2C_STMT_GOTO) &&
        !f2c_statement_parse_control(unit, token_line, body_start, statement))
        return 0;
    if ((statement->kind == F2C_STMT_CALL || statement->kind == F2C_STMT_STOP ||
         statement->kind == F2C_STMT_RETURN || statement->kind == F2C_STMT_ALLOCATE ||
         statement->kind == F2C_STMT_DEALLOCATE || statement->kind == F2C_STMT_NULLIFY) &&
        !f2c_statement_parse_action(unit, token_line, body_start, statement))
        return 0;
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_BACKSPACE || statement->kind == F2C_STMT_ENDFILE ||
        statement->kind == F2C_STMT_INQUIRE || statement->kind == F2C_STMT_CLOSE)
        (void)f2c_statement_parse_io(unit, token_line, body_start, statement);
    if (statement->kind == F2C_STMT_PRINT)
        (void)f2c_statement_parse_print(unit, token_line, body_start, statement);
    if (statement->kind == F2C_STMT_DATA &&
        !f2c_statement_parse_data(unit, token_line, body_start, statement)) {
        return 0;
    }
    syntax_line = *token_line;
    if (syntax_line.tokens != NULL)
        syntax_line.tokens += body_start;
    syntax_line.token_count -= body_start;
    if (statement->kind == F2C_STMT_CASE &&
        !f2c_statement_parse_case(unit, &syntax_line, statement)) {
        return 0;
    }
    if (statement->kind == F2C_STMT_LABEL)
        f2c_statement_parse_label(unit, token_line, statement);
    if (!f2c_statement_parse_construct_syntax(token_line, body_start, statement)) {
        return 0;
    }
    if (statement->kind == F2C_STMT_INVALID &&
        !f2c_statement_parse_assignment(unit, token_line, body_start, statement))
        return 0;
    return 1;
}

int f2c_statement_parse_nested_tokens(Unit *unit, const Line *line, size_t begin,
                                      F2cStatement **statement) {
    F2cTokenRange range;
    F2cStatement *nested;
    Line view;
    char *text;
    size_t body_start;
    if (unit == NULL || line == NULL || statement == NULL || begin >= line->token_count)
        return 0;
    range = f2c_line_token_range(line, begin, line->token_count);
    text = f2c_token_range_text(range);
    if (text == NULL)
        return 0;
    nested = (F2cStatement *)calloc(1U, sizeof(*nested));
    if (nested == NULL) {
        free(text);
        return 0;
    }
    view = *line;
    view.tokens += begin;
    view.token_count -= begin;
    body_start = statement_body_start(&view);
    if (!parse_statement(unit, text, line->number, nested, classify_tokens(&view, body_start),
                         &view, body_start)) {
        f2c_statement_free(nested);
        free(nested);
        free(text);
        return 0;
    }
    set_statement_span(&view, nested);
    free(text);
    *statement = nested;
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
    free(statement->terminal_label);
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
        f2c_format_free(control->format);
        free(control->cleanup.symbols);
    }
    free(statement->io_controls);
    while (statement->io_item_count != 0U)
        f2c_statement_free_io_item(&statement->io_items[--statement->io_item_count]);
    free(statement->io_items);
    while (statement->data_group_count != 0U) {
        F2cDataGroup *group = &statement->data_groups[--statement->data_group_count];
        f2c_statement_free_data_group(group);
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
    f2c_format_free(statement->format);
    if (statement->label_cleanups != NULL) {
        size_t cleanup;
        for (cleanup = 0U; cleanup < statement->label_count; ++cleanup)
            free(statement->label_cleanups[cleanup].symbols);
    }
    free(statement->label_cleanups);
    while (statement->label_count != 0U)
        free(statement->labels[--statement->label_count]);
    free(statement->labels);
    free(statement->label_spans);
    free(statement->terminal_loops);
    free(statement->transfer_cleanup.symbols);
    while (statement->resolved_branch_count != 0U) {
        F2cResolvedBranch *branch =
            &statement->resolved_branches[--statement->resolved_branch_count];
        free(branch->label);
        free(branch->cleanup.symbols);
    }
    free(statement->resolved_branches);
    if (statement->nested != NULL) {
        f2c_statement_free(statement->nested);
        free(statement->nested);
    }
    memset(statement, 0, sizeof(*statement));
}
