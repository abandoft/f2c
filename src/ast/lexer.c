#include "ast/internal.h"

#include <ctype.h>
#include <string.h>

int f2c_ast_token_equals(const AstToken *token, const char *text) {
    const char *cursor = token->begin;
    const char *end = token->begin + token->length;
    while (cursor < end || *text != '\0') {
        while (cursor < end && isspace((unsigned char)*cursor))
            ++cursor;
        if (cursor == end || *text == '\0')
            return cursor == end && *text == '\0';
        if (tolower((unsigned char)*cursor++) != tolower((unsigned char)*text++))
            return 0;
    }
    return 1;
}

void f2c_ast_parser_error(AstParser *parser, const char *at) {
    if (parser->error_at == NULL)
        parser->error_at = at;
}

static AstTokenKind ast_token_kind(F2cTokenKind kind) {
    switch (kind) {
    case F2C_TOKEN_END:
        return AST_TOKEN_END;
    case F2C_TOKEN_IDENTIFIER:
        return AST_TOKEN_NAME;
    case F2C_TOKEN_NUMBER:
        return AST_TOKEN_NUMBER;
    case F2C_TOKEN_STRING:
        return AST_TOKEN_STRING;
    case F2C_TOKEN_HOLLERITH:
        return AST_TOKEN_HOLLERITH;
    case F2C_TOKEN_BOZ:
        return AST_TOKEN_BOZ;
    case F2C_TOKEN_LEFT_PAREN:
        return AST_TOKEN_LEFT_PAREN;
    case F2C_TOKEN_RIGHT_PAREN:
        return AST_TOKEN_RIGHT_PAREN;
    case F2C_TOKEN_LEFT_BRACKET:
    case F2C_TOKEN_ARRAY_BEGIN:
        return AST_TOKEN_ARRAY_BEGIN;
    case F2C_TOKEN_RIGHT_BRACKET:
    case F2C_TOKEN_ARRAY_END:
        return AST_TOKEN_ARRAY_END;
    case F2C_TOKEN_COMMA:
        return AST_TOKEN_COMMA;
    case F2C_TOKEN_COLON:
        return AST_TOKEN_COLON;
    case F2C_TOKEN_PERCENT:
        return AST_TOKEN_PERCENT;
    case F2C_TOKEN_OPERATOR:
        return AST_TOKEN_OPERATOR;
    case F2C_TOKEN_DOUBLE_COLON:
    case F2C_TOKEN_SEMICOLON:
    case F2C_TOKEN_INVALID:
    default:
        return AST_TOKEN_INVALID;
    }
}

void f2c_ast_next_token(AstParser *parser) {
    F2cLexer lexer;
    f2c_lexer_init(&lexer, parser->cursor, 1U, (size_t)(parser->cursor - parser->source) + 1U);
    f2c_lexer_next(&lexer);
    parser->cursor = lexer.cursor;
    parser->token.kind = ast_token_kind(lexer.token.kind);
    parser->token.begin = lexer.token.begin;
    parser->token.length = lexer.token.length;
    parser->token.line = lexer.token.line;
    parser->token.column = lexer.token.column;
    if (lexer.token.kind == F2C_TOKEN_SEMICOLON) {
        while (isspace((unsigned char)*parser->cursor))
            ++parser->cursor;
        parser->token.kind = *parser->cursor == '\0' ? AST_TOKEN_END : AST_TOKEN_INVALID;
    }
    if (parser->token.kind == AST_TOKEN_INVALID)
        f2c_ast_parser_error(parser, lexer.error_at != NULL ? lexer.error_at : lexer.token.begin);
}
