#include "ast/statement/private.h"

#include <stdlib.h>

static char *copy_identifier(const F2cToken *token) {
    if (token == NULL || token->kind != F2C_TOKEN_IDENTIFIER)
        return NULL;
    return f2c_strdup_n(token->begin, token->length);
}

static int is_construct_opener(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE ||
           statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
           statement->kind == F2C_STMT_BLOCK_SCOPE ||
           (statement->kind == F2C_STMT_WHERE && statement->block) ||
           (statement->kind == F2C_STMT_IF && statement->block);
}

static int parenthesized_opener_ends_at(const Line *line, size_t open, size_t expected_tail) {
    size_t close;
    return open < line->token_count && line->tokens[open].kind == F2C_TOKEN_LEFT_PAREN &&
           f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close) &&
           close + expected_tail == line->token_count;
}

static int opener_syntax_valid(const Line *line, size_t begin, const F2cStatement *statement) {
    if (statement->kind == F2C_STMT_WHERE && statement->block)
        return parenthesized_opener_ends_at(line, begin + 1U, 1U);
    if (statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
        statement->kind == F2C_STMT_DO_WHILE)
        return parenthesized_opener_ends_at(line, begin + 2U, 1U);
    if (statement->kind == F2C_STMT_IF && statement->block) {
        size_t close;
        return begin + 1U < line->token_count &&
               line->tokens[begin + 1U].kind == F2C_TOKEN_LEFT_PAREN &&
               f2c_token_matching_delimiter(line->tokens, line->token_count, begin + 1U, &close) &&
               close + 2U == line->token_count &&
               f2c_token_equals(&line->tokens[close + 1U], "then");
    }
    return 1;
}

static size_t terminator_suffix(const Line *line, size_t begin) {
    if (begin >= line->token_count)
        return begin;
    if (f2c_token_equals(&line->tokens[begin], "endif") ||
        f2c_token_equals(&line->tokens[begin], "enddo") ||
        f2c_token_equals(&line->tokens[begin], "endselect") ||
        f2c_token_equals(&line->tokens[begin], "endwhere"))
        return begin + 1U;
    return begin + 2U;
}

static size_t branch_suffix(const Line *line, size_t begin, F2cStatementKind kind) {
    size_t open = SIZE_MAX;
    size_t close;
    size_t index;
    if (kind == F2C_STMT_ELSE)
        return begin + 1U;
    if (kind == F2C_STMT_CASE) {
        if (begin + 1U < line->token_count &&
            f2c_token_equals(&line->tokens[begin + 1U], "default"))
            return begin + 2U;
        open = begin + 1U;
    } else if (kind == F2C_STMT_TYPE_GUARD) {
        if (begin + 1U < line->token_count &&
            f2c_token_equals(&line->tokens[begin + 1U], "default"))
            return begin + 2U;
        open = begin + 2U;
    } else if (kind == F2C_STMT_ELSE_IF) {
        index = begin + (f2c_token_equals(&line->tokens[begin], "elseif") ? 1U : 2U);
        open = index;
    } else if (kind == F2C_STMT_ELSEWHERE) {
        if (begin + 1U == line->token_count ||
            line->tokens[begin + 1U].kind != F2C_TOKEN_LEFT_PAREN)
            return begin + 1U;
        open = begin + 1U;
    }
    if (open >= line->token_count || line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close))
        return SIZE_MAX;
    if (kind == F2C_STMT_ELSE_IF) {
        if (close + 1U >= line->token_count || !f2c_token_equals(&line->tokens[close + 1U], "then"))
            return SIZE_MAX;
        return close + 2U;
    }
    return close + 1U;
}

static int parse_optional_identifier(const Line *line, size_t begin, char **name) {
    if (begin == line->token_count)
        return 1;
    if (begin + 1U != line->token_count || line->tokens[begin].kind != F2C_TOKEN_IDENTIFIER)
        return 0;
    *name = copy_identifier(&line->tokens[begin]);
    return *name != NULL;
}

int f2c_statement_parse_construct_syntax(const Line *line, size_t body_start,
                                         F2cStatement *statement) {
    size_t suffix = SIZE_MAX;
    if (line == NULL || statement == NULL)
        return 0;
    statement->construct_syntax_valid = 1;
    if (body_start != 0U) {
        statement->construct_name = copy_identifier(&line->tokens[0]);
        if (statement->construct_name == NULL)
            return 0;
        if (!is_construct_opener(statement))
            statement->construct_syntax_valid = 0;
    }
    if (is_construct_opener(statement) && !opener_syntax_valid(line, body_start, statement))
        statement->construct_syntax_valid = 0;
    if (statement->kind == F2C_STMT_END_IF || statement->kind == F2C_STMT_END_DO ||
        statement->kind == F2C_STMT_END_SELECT || statement->kind == F2C_STMT_END_BLOCK_SCOPE ||
        statement->kind == F2C_STMT_END_WHERE) {
        suffix = terminator_suffix(line, body_start);
        if (statement->kind == F2C_STMT_END_DO && suffix < line->token_count &&
            line->tokens[suffix].kind == F2C_TOKEN_NUMBER && suffix + 1U == line->token_count) {
            statement->name = f2c_strdup_n(line->tokens[suffix].begin, line->tokens[suffix].length);
            return statement->name != NULL;
        }
    } else if (statement->kind == F2C_STMT_CASE || statement->kind == F2C_STMT_TYPE_GUARD ||
               statement->kind == F2C_STMT_ELSE_IF || statement->kind == F2C_STMT_ELSE ||
               statement->kind == F2C_STMT_ELSEWHERE) {
        suffix = branch_suffix(line, body_start, statement->kind);
    } else if (statement->kind == F2C_STMT_CYCLE || statement->kind == F2C_STMT_EXIT) {
        if (!parse_optional_identifier(line, body_start + 1U, &statement->control_name))
            statement->construct_syntax_valid = 0;
        return 1;
    }
    if (suffix != SIZE_MAX && !parse_optional_identifier(line, suffix, &statement->construct_name))
        statement->construct_syntax_valid = 0;
    else if (suffix == SIZE_MAX &&
             (statement->kind == F2C_STMT_ELSE_IF || statement->kind == F2C_STMT_CASE ||
              statement->kind == F2C_STMT_TYPE_GUARD || statement->kind == F2C_STMT_ELSEWHERE))
        statement->construct_syntax_valid = 0;
    return 1;
}
