#include "ast/declaration/access.h"

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
    parsed->line.source_name = "access-ast.f90";
    parsed->line.number = 17U;
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

static void test_default_access(void) {
    ParsedLine parsed;
    F2cAccessStatementSyntax syntax;
    expect(parsed_line_init(&parsed, "private"), "default PRIVATE tokenizes");
    expect(f2c_parse_access_statement_syntax(&parsed.line, &syntax) ==
                   F2C_ACCESS_STATEMENT_PARSED &&
               syntax.kind == F2C_ACCESS_PRIVATE && syntax.item_count == 0U &&
               syntax.keyword->span.begin.line == 17U,
           "default PRIVATE has a source-mapped structured representation");
    f2c_access_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_complete_access_list(void) {
    static const char source[] =
        "public :: Value, operator(.merge.), assignment(=), read(formatted), write(unformatted)";
    static const char *const expected_keys[] = {
        "value", "operator(.merge.)", "assignment(=)", "read(formatted)", "write(unformatted)",
    };
    ParsedLine parsed;
    F2cAccessStatementSyntax syntax;
    size_t index;
    expect(parsed_line_init(&parsed, source), "complete PUBLIC list tokenizes");
    expect(f2c_parse_access_statement_syntax(&parsed.line, &syntax) ==
                   F2C_ACCESS_STATEMENT_PARSED &&
               syntax.kind == F2C_ACCESS_PUBLIC && syntax.double_colon != NULL &&
               syntax.item_count == 5U,
           "PUBLIC AST retains every standard access designator category");
    for (index = 0U; index < syntax.item_count && index < 5U; ++index) {
        char *key = f2c_generic_designator_key(&syntax.items[index]);
        expect(key != NULL && strcmp(key, expected_keys[index]) == 0,
               "generic designators have stable case-insensitive semantic keys");
        free(key);
    }
    expect(syntax.items[0].span.begin.column == 11U &&
               syntax.items[4].span.end.column == sizeof(source),
           "access identifiers retain exact token spans");
    f2c_access_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "public first, second"),
           "access list without double colon tokenizes");
    expect(f2c_parse_access_statement_syntax(&parsed.line, &syntax) ==
                   F2C_ACCESS_STATEMENT_PARSED &&
               syntax.double_colon == NULL && syntax.item_count == 2U,
           "the optional double colon is represented without changing list semantics");
    f2c_access_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void expect_invalid(const char *source, F2cAccessStatementError error,
                           const char *token_text, const char *message) {
    ParsedLine parsed;
    F2cAccessStatementSyntax syntax;
    expect(parsed_line_init(&parsed, source), "invalid access statement tokenizes");
    expect(f2c_parse_access_statement_syntax(&parsed.line, &syntax) ==
                   F2C_ACCESS_STATEMENT_INVALID &&
               syntax.error == error && syntax.error_token != NULL &&
               f2c_token_equals(syntax.error_token, token_text),
           message);
    f2c_access_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_invalid_access_lists(void) {
    expect_invalid("private ::", F2C_ACCESS_ERROR_EMPTY_LIST,
                   "::", "a double colon requires an access identifier list");
    expect_invalid("public value value", F2C_ACCESS_ERROR_LIST_SEPARATOR, "value",
                   "adjacent access identifiers require a comma");
    expect_invalid("public value, value", F2C_ACCESS_ERROR_DUPLICATE_ITEM, "value",
                   "duplicate access identifiers fail at the repeated token");
    expect_invalid("private operator(name)", F2C_ACCESS_ERROR_ITEM, "operator",
                   "malformed generic designators are rejected structurally");
    expect_invalid("public value,", F2C_ACCESS_ERROR_TRAILING_COMMA, ",",
                   "trailing access commas are rejected explicitly");
}

static void test_nonreserved_names(void) {
    static const char *const statements[] = {
        "public = 1",
        "private(1) = 2",
        "public%field = 3",
    };
    size_t index;
    for (index = 0U; index < sizeof(statements) / sizeof(statements[0]); ++index) {
        ParsedLine parsed;
        F2cAccessStatementSyntax syntax;
        expect(parsed_line_init(&parsed, statements[index]), "nonreserved entity use tokenizes");
        expect(f2c_parse_access_statement_syntax(&parsed.line, &syntax) ==
                   F2C_ACCESS_STATEMENT_NOT_MATCHED,
               "PUBLIC and PRIVATE remain valid ordinary entity designators");
        f2c_access_statement_syntax_discard(&syntax);
        parsed_line_discard(&parsed);
    }
}

static void test_large_access_list(void) {
    char source[4096];
    size_t length;
    size_t index;
    ParsedLine parsed;
    F2cAccessStatementSyntax syntax;
    int written = snprintf(source, sizeof(source), "public :: ");
    expect(written > 0 && (size_t)written < sizeof(source), "large access prefix is bounded");
    if (written <= 0 || (size_t)written >= sizeof(source))
        return;
    length = (size_t)written;
    for (index = 0U; index < 128U; ++index) {
        written = snprintf(source + length, sizeof(source) - length, "%sitem_%03zu",
                           index == 0U ? "" : ", ", index);
        expect(written > 0 && (size_t)written < sizeof(source) - length,
               "large access item append is bounded");
        if (written <= 0 || (size_t)written >= sizeof(source) - length)
            return;
        length += (size_t)written;
    }
    expect(parsed_line_init(&parsed, source), "large access list tokenizes");
    expect(f2c_parse_access_statement_syntax(&parsed.line, &syntax) ==
                   F2C_ACCESS_STATEMENT_PARSED &&
               syntax.item_count == 128U,
           "access AST storage grows dynamically without a fixed item limit");
    f2c_access_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

int main(void) {
    test_default_access();
    test_complete_access_list();
    test_invalid_access_lists();
    test_nonreserved_names();
    test_large_access_list();
    if (failures != 0) {
        fprintf(stderr, "%d access statement AST test(s) failed\n", failures);
        return 1;
    }
    puts("all access statement AST tests passed");
    return 0;
}
