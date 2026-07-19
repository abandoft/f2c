#include "ast/declaration/use.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ParsedLine {
    Line line;
    F2cToken *tokens;
} ParsedLine;

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int parsed_line_init(ParsedLine *parsed, const char *source) {
    F2cTokenStream stream;
    size_t count = 0U;
    size_t capacity = 0U;
    memset(parsed, 0, sizeof(*parsed));
    parsed->line.text = (char *)source;
    parsed->line.source_name = "use-ast.f90";
    parsed->line.number = 13U;
    f2c_token_stream_init(&stream, source, parsed->line.number, 1U);
    for (;;) {
        F2cToken *replacement;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            return 0;
        if (count == capacity) {
            const size_t next = capacity == 0U ? 16U : capacity * 2U;
            replacement = (F2cToken *)realloc(parsed->tokens, next * sizeof(*replacement));
            if (replacement == NULL)
                return 0;
            parsed->tokens = replacement;
            capacity = next;
        }
        stream.token.span.begin.source_name = parsed->line.source_name;
        stream.token.span.end.source_name = parsed->line.source_name;
        parsed->tokens[count++] = stream.token;
    }
    parsed->line.tokens = parsed->tokens;
    parsed->line.token_count = count;
    return 1;
}

static void parsed_line_discard(ParsedLine *parsed) {
    free(parsed->tokens);
    memset(parsed, 0, sizeof(*parsed));
}

static void test_complete_only_list(void) {
    static const char source[] =
        "use, non_intrinsic :: provider, only: local => remote, keep, operator(.combine.), "
        "assignment(=), read(formatted)";
    ParsedLine parsed;
    F2cUseStatementSyntax syntax;
    expect(parsed_line_init(&parsed, source), "complete USE statement tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_PARSED,
           "complete USE statement builds a syntax AST");
    expect(syntax.nature == F2C_USE_NATURE_NON_INTRINSIC && syntax.nature_token != NULL &&
               syntax.module_name != NULL && f2c_token_equals(syntax.module_name, "provider") &&
               syntax.only_token != NULL,
           "USE AST retains module nature, module name, and ONLY token");
    expect(syntax.item_count == 5U && syntax.items[0].renamed &&
               f2c_token_equals(syntax.items[0].local.name, "local") &&
               f2c_token_equals(syntax.items[0].remote.name, "remote") &&
               !syntax.items[1].renamed &&
               syntax.items[2].local.kind == F2C_USE_DESIGNATOR_OPERATOR &&
               syntax.items[3].local.kind == F2C_USE_DESIGNATOR_ASSIGNMENT &&
               syntax.items[4].local.kind == F2C_USE_DESIGNATOR_DEFINED_IO,
           "USE AST represents names, renames, operators, assignment, and defined I/O");
    expect(syntax.items[0].local.span.begin.line == 13U &&
               syntax.items[0].local.span.begin.column == 39U &&
               syntax.items[0].remote.span.begin.column == 48U,
           "USE associations retain exact local and remote source spans");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_rename_and_empty_only(void) {
    ParsedLine parsed;
    F2cUseStatementSyntax syntax;
    expect(parsed_line_init(&parsed, "use provider, local => remote"), "USE rename list tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_PARSED &&
               syntax.only_token == NULL && syntax.item_count == 1U && syntax.items[0].renamed,
           "a rename list is structurally distinct from an ONLY list");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "use provider, only:"), "empty ONLY list tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_PARSED &&
               syntax.only_token != NULL && syntax.item_count == 0U,
           "an empty ONLY list is represented explicitly and imports no names");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void expect_invalid(const char *source, F2cUseStatementError error, const char *token_text,
                           const char *message) {
    ParsedLine parsed;
    F2cUseStatementSyntax syntax;
    expect(parsed_line_init(&parsed, source), "invalid USE statement tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_INVALID &&
               syntax.error == error && syntax.error_token != NULL &&
               f2c_token_equals(syntax.error_token, token_text),
           message);
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_invalid_statements(void) {
    expect_invalid("use, external :: provider", F2C_USE_ERROR_MODULE_NATURE, "external",
                   "unknown module nature fails at its canonical token");
    expect_invalid("use, intrinsic provider", F2C_USE_ERROR_DOUBLE_COLON, "provider",
                   "module nature without :: fails at the module-name token");
    expect_invalid("use provider, only value", F2C_USE_ERROR_ONLY_COLON, "value",
                   "ONLY without a colon fails at the following token");
    expect_invalid("use provider, local", F2C_USE_ERROR_RENAME_REQUIRED, "local",
                   "a non-ONLY list requires an explicit rename");
    expect_invalid("use provider, only: local => operator(.remote.)", F2C_USE_ERROR_RENAME_KIND,
                   "operator", "a name cannot be renamed to an operator generic");
    expect_invalid("use provider, only: value, value", F2C_USE_ERROR_DUPLICATE_LOCAL_NAME, "value",
                   "duplicate USE local names fail at the repeated designator");
    expect_invalid("use provider, only: value,", F2C_USE_ERROR_TRAILING_COMMA, ",",
                   "trailing USE comma is rejected explicitly");
}

static void test_large_only_list(void) {
    char source[4096];
    size_t length = 0U;
    size_t index;
    ParsedLine parsed;
    F2cUseStatementSyntax syntax;
    int written = snprintf(source, sizeof(source), "use provider, only: ");
    expect(written > 0 && (size_t)written < sizeof(source), "large ONLY prefix is bounded");
    if (written <= 0 || (size_t)written >= sizeof(source))
        return;
    length = (size_t)written;
    for (index = 0U; index < 128U; ++index) {
        written = snprintf(source + length, sizeof(source) - length, "%sitem_%03zu",
                           index == 0U ? "" : ", ", index);
        expect(written > 0 && (size_t)written < sizeof(source) - length,
               "large ONLY item append is bounded");
        if (written <= 0 || (size_t)written >= sizeof(source) - length)
            return;
        length += (size_t)written;
    }
    expect(parsed_line_init(&parsed, source), "large ONLY list tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_PARSED &&
               syntax.item_count == 128U,
           "USE association storage grows dynamically without a fixed item limit");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_not_matched(void) {
    ParsedLine parsed;
    F2cUseStatementSyntax syntax;
    expect(parsed_line_init(&parsed, "used = 1"), "non-USE statement tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_NOT_MATCHED,
           "an identifier containing USE is not mistaken for a USE statement");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "use = 1"), "USE-named assignment tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_NOT_MATCHED,
           "a scalar named USE remains a valid assignment designator");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "use(1) = 2"), "USE-named array assignment tokenizes");
    expect(f2c_parse_use_statement_syntax(&parsed.line, &syntax) == F2C_USE_STATEMENT_NOT_MATCHED,
           "an array named USE remains a valid assignment designator");
    f2c_use_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

int main(void) {
    test_complete_only_list();
    test_rename_and_empty_only();
    test_invalid_statements();
    test_large_only_list();
    test_not_matched();
    if (failures != 0) {
        fprintf(stderr, "%d USE AST test(s) failed\n", failures);
        return 1;
    }
    puts("all USE statement AST tests passed");
    return 0;
}
