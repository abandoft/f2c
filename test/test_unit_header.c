#include "internal/f2c.h"

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
    parsed->line.source_name = "unit-header.f90";
    parsed->line.number = 7U;
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

static void test_function_header_ast(void) {
    static const char source[] =
        "recursive pure real(kind=8) function evaluate(value, scale) result(answer)";
    ParsedLine parsed;
    F2cUnitHeaderSyntax syntax;
    char *type_text;
    expect(parsed_line_init(&parsed, source), "function header tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_PARSED,
           "function header builds a syntax AST");
    type_text = f2c_token_range_text(syntax.type_spec);
    expect(syntax.kind == F2C_UNIT_SYNTAX_FUNCTION && syntax.recursive_prefix != NULL &&
               syntax.pure_prefix != NULL && syntax.elemental_prefix == NULL,
           "procedure prefixes are represented independently in the header AST");
    expect(type_text != NULL && strcmp(type_text, "real(kind=8)") == 0,
           "function result type remains a canonical token range");
    expect(syntax.name != NULL && f2c_token_equals(syntax.name, "evaluate") &&
               syntax.argument_count == 2U &&
               f2c_token_equals(syntax.arguments[0].token, "value") &&
               f2c_token_equals(syntax.arguments[1].token, "scale") && syntax.result_name != NULL &&
               f2c_token_equals(syntax.result_name, "answer"),
           "header AST owns structured procedure, dummy, and result designators");
    expect(syntax.name->span.begin.line == 7U && syntax.name->span.begin.column == 38U &&
               syntax.name->span.end.column == 46U,
           "header AST retains the exact procedure-name span");
    free(type_text);
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_header_lowering(void) {
    static const char source[] = "real(kind=8) recursive function norm(value) result(answer)";
    ParsedLine parsed;
    Unit unit;
    expect(parsed_line_init(&parsed, source), "lowered header tokenizes");
    expect(f2c_parse_unit_header(NULL, &parsed.line, &unit) == F2C_UNIT_HEADER_PARSED,
           "syntax header lowers into a semantic program unit");
    expect(unit.kind == UNIT_FUNCTION && unit.recursive && unit.return_type == TYPE_DOUBLE &&
               unit.return_kind == 8 && unit.return_type_explicit,
           "header lowering retains typed result and procedure metadata");
    expect(unit.argument_count == 1U && strcmp(unit.arguments[0], "value") == 0 &&
               unit.argument_spans[0].begin.column == 38U &&
               strcmp(unit.result_name, "answer") == 0 && unit.result_name_span.begin.column == 52U,
           "semantic unit retains exact dummy and result-name spans");
    f2c_free_unit(&unit);
    parsed_line_discard(&parsed);
}

static void test_legacy_alternate_return_ast(void) {
    static const char source[] = "subroutine legacy(value, *, status)";
    ParsedLine parsed;
    F2cUnitHeaderSyntax syntax;
    expect(parsed_line_init(&parsed, source), "alternate-return header tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_PARSED &&
               syntax.argument_count == 3U && syntax.arguments[1].alternate_return,
           "syntax AST distinguishes an alternate-return dummy from a named dummy");
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_block_data_header_ast(void) {
    ParsedLine parsed;
    F2cUnitHeaderSyntax syntax;
    Unit unit;
    expect(parsed_line_init(&parsed, "block data initialize_state"),
           "named BLOCK DATA header tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_PARSED &&
               syntax.kind == F2C_UNIT_SYNTAX_BLOCK_DATA && syntax.name != NULL &&
               f2c_token_equals(syntax.name, "initialize_state"),
           "BLOCK DATA has a distinct syntax kind and optional canonical name");
    f2c_unit_header_syntax_discard(&syntax);
    expect(f2c_parse_unit_header(NULL, &parsed.line, &unit) == F2C_UNIT_HEADER_PARSED &&
               unit.kind == UNIT_BLOCK_DATA && strcmp(unit.name, "initialize_state") == 0,
           "named BLOCK DATA lowers without becoming a procedure");
    f2c_free_unit(&unit);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "blockdata"), "unnamed joined BLOCKDATA header tokenizes");
    expect(f2c_parse_unit_header(NULL, &parsed.line, &unit) == F2C_UNIT_HEADER_PARSED &&
               unit.kind == UNIT_BLOCK_DATA && unit.name != NULL &&
               strcmp(unit.fortran_name, "") == 0,
           "unnamed BLOCK DATA receives an internal-only deterministic identity");
    f2c_free_unit(&unit);
    parsed_line_discard(&parsed);
}

static void test_invalid_headers(void) {
    ParsedLine parsed;
    F2cUnitHeaderSyntax syntax;
    expect(parsed_line_init(&parsed, "end function evaluate"), "END FUNCTION tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_NOT_MATCHED,
           "END FUNCTION is never mistaken for a new program-unit header");
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "procedure(iface) :: function"),
           "procedure entity named FUNCTION tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_NOT_MATCHED,
           "a PROCEDURE entity named FUNCTION is not mistaken for a unit header");
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "pure pure subroutine duplicate()"),
           "duplicate-prefix header tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_INVALID &&
               syntax.error == F2C_UNIT_HEADER_ERROR_DUPLICATE_PREFIX &&
               syntax.error_token != NULL && syntax.error_token->span.begin.column == 6U,
           "duplicate prefixes fail at the repeated canonical token");
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "subroutine duplicate(value, value)"),
           "duplicate-dummy header tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_INVALID &&
               syntax.error == F2C_UNIT_HEADER_ERROR_DUPLICATE_ARGUMENT &&
               syntax.error_token != NULL && syntax.error_token->span.begin.column == 29U,
           "duplicate dummy arguments fail at the second name");
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "integer function value() result(answer) junk"),
           "trailing-token header tokenizes");
    expect(f2c_parse_unit_header_syntax(&parsed.line, &syntax) == F2C_UNIT_HEADER_INVALID &&
               syntax.error == F2C_UNIT_HEADER_ERROR_TRAILING_TOKENS &&
               syntax.error_token != NULL && f2c_token_equals(syntax.error_token, "junk"),
           "trailing tokens fail at the first unconsumed token");
    f2c_unit_header_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_unit_end_ast(void) {
    ParsedLine parsed;
    F2cUnitEndSyntax syntax;
    expect(parsed_line_init(&parsed, "120 end subroutine worker"), "labeled unit END tokenizes");
    expect(f2c_parse_unit_end_syntax(&parsed.line, &syntax) == F2C_UNIT_END_PARSED &&
               syntax.has_kind && syntax.kind == F2C_UNIT_SYNTAX_SUBROUTINE &&
               syntax.kind_token != NULL && syntax.name != NULL &&
               f2c_token_equals(syntax.name, "worker"),
           "unit END AST retains its kind, optional label, and closing name");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "endfunction evaluate"), "joined unit END tokenizes");
    expect(f2c_parse_unit_end_syntax(&parsed.line, &syntax) == F2C_UNIT_END_PARSED &&
               syntax.kind == F2C_UNIT_SYNTAX_FUNCTION && f2c_token_equals(syntax.name, "evaluate"),
           "joined legacy unit END spelling uses the same syntax AST");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "end block data initialize_state"),
           "separated END BLOCK DATA tokenizes");
    expect(f2c_parse_unit_end_syntax(&parsed.line, &syntax) == F2C_UNIT_END_PARSED &&
               syntax.has_kind && syntax.kind == F2C_UNIT_SYNTAX_BLOCK_DATA &&
               syntax.name != NULL && f2c_token_equals(syntax.name, "initialize_state"),
           "END BLOCK DATA retains its optional closing name");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "endblockdata initialize_state"),
           "joined ENDBLOCKDATA tokenizes");
    expect(f2c_parse_unit_end_syntax(&parsed.line, &syntax) == F2C_UNIT_END_PARSED &&
               syntax.kind == F2C_UNIT_SYNTAX_BLOCK_DATA,
           "joined ENDBLOCKDATA uses the same syntax kind");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "end if"), "construct END tokenizes");
    expect(f2c_parse_unit_end_syntax(&parsed.line, &syntax) == F2C_UNIT_END_NOT_MATCHED,
           "construct terminators are not mistaken for program-unit END statements");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "end subroutine worker junk"), "invalid unit END tokenizes");
    expect(f2c_parse_unit_end_syntax(&parsed.line, &syntax) == F2C_UNIT_END_INVALID &&
               syntax.error_token != NULL && f2c_token_equals(syntax.error_token, "junk"),
           "unit END rejects trailing syntax at the first unconsumed token");
    parsed_line_discard(&parsed);
}

static void test_module_header_ast(void) {
    ParsedLine parsed;
    F2cModuleHeaderSyntax syntax;
    expect(parsed_line_init(&parsed, "120 module numerics"), "labeled module header tokenizes");
    expect(f2c_parse_module_header_syntax(&parsed.line, &syntax) == F2C_MODULE_HEADER_PARSED &&
               syntax.name != NULL && f2c_token_equals(syntax.name, "numerics") &&
               syntax.span.begin.column == 5U && syntax.span.end.column == 20U,
           "module header AST retains its exact name and statement span");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "module procedure solve"),
           "MODULE PROCEDURE statement tokenizes");
    expect(f2c_parse_module_header_syntax(&parsed.line, &syntax) == F2C_MODULE_HEADER_NOT_MATCHED,
           "MODULE PROCEDURE is not mistaken for a module header");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "module numerics junk"), "invalid module header tokenizes");
    expect(f2c_parse_module_header_syntax(&parsed.line, &syntax) == F2C_MODULE_HEADER_INVALID &&
               syntax.error_token != NULL && f2c_token_equals(syntax.error_token, "junk"),
           "module header AST rejects trailing syntax at the first unconsumed token");
    parsed_line_discard(&parsed);
}

int main(void) {
    test_function_header_ast();
    test_header_lowering();
    test_legacy_alternate_return_ast();
    test_block_data_header_ast();
    test_invalid_headers();
    test_unit_end_ast();
    test_module_header_ast();
    if (failures != 0) {
        fprintf(stderr, "%d unit-header test(s) failed\n", failures);
        return 1;
    }
    puts("all unit-header tests passed");
    return 0;
}
