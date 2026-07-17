#include "ast/internal.h"

#include <string.h>

void f2c_ast_parser_error(AstParser *parser, const char *at) {
    if (parser->error_at == NULL)
        parser->error_at = at;
}

static void normalize_expression_token(F2cToken *token) {
    if (token->kind == F2C_TOKEN_LEFT_BRACKET)
        token->kind = F2C_TOKEN_ARRAY_BEGIN;
    else if (token->kind == F2C_TOKEN_RIGHT_BRACKET)
        token->kind = F2C_TOKEN_ARRAY_END;
    else if (token->kind == F2C_TOKEN_DOUBLE_COLON || token->kind == F2C_TOKEN_SEMICOLON)
        token->kind = F2C_TOKEN_INVALID;
}

void f2c_ast_next_token(AstParser *parser) {
    if (parser->tokens != NULL) {
        if (parser->token_index < parser->token_count) {
            parser->token = parser->tokens[parser->token_index++];
            parser->cursor = parser->token.begin + parser->token.length;
        } else {
            memset(&parser->token, 0, sizeof(parser->token));
            parser->token.kind = F2C_TOKEN_END;
            parser->token.begin = parser->cursor;
        }
        normalize_expression_token(&parser->token);
        if (parser->token.kind == F2C_TOKEN_INVALID)
            f2c_ast_parser_error(parser, parser->token.begin);
        return;
    }
    {
        F2cTokenStream stream;
        f2c_token_stream_init(&stream, parser->cursor, 1U,
                              (size_t)(parser->cursor - parser->source) + 1U);
        f2c_token_stream_next(&stream);
        parser->cursor = stream.cursor;
        parser->token = stream.token;
        normalize_expression_token(&parser->token);
        if (parser->token.kind == F2C_TOKEN_INVALID)
            f2c_ast_parser_error(parser,
                                 stream.error_at != NULL ? stream.error_at : stream.token.begin);
    }
}
