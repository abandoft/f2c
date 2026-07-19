#include "ast/interface/header.h"
#include "ast/interface/specific.h"

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
    parsed->line.source_name = "interface-ast.f90";
    parsed->line.number = 23U;
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

static void test_interface_headers(void) {
    static const char *const headers[] = {
        "interface solve",         "interface operator(.combine.)", "interface operator(+)",
        "interface assignment(=)", "interface read(formatted)",     "interface write(unformatted)",
        "abstract interface",
    };
    size_t index;
    for (index = 0U; index < sizeof(headers) / sizeof(headers[0]); ++index) {
        ParsedLine parsed;
        F2cInterfaceHeaderSyntax syntax;
        expect(parsed_line_init(&parsed, headers[index]), "INTERFACE header tokenizes");
        expect(f2c_parse_interface_header_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_HEADER_PARSED,
               "every standard generic INTERFACE designator has a structured header AST");
        expect(syntax.interface_keyword != NULL && syntax.span.begin.line == 23U,
               "INTERFACE header AST retains keyword and source span");
        parsed_line_discard(&parsed);
    }
}

static void test_end_interface(void) {
    ParsedLine start;
    ParsedLine end;
    F2cInterfaceHeaderSyntax header;
    F2cEndInterfaceSyntax terminator;
    expect(parsed_line_init(&start, "interface operator(.combine.)") &&
               parsed_line_init(&end, "end interface operator(.combine.)"),
           "matching INTERFACE delimiters tokenize");
    expect(f2c_parse_interface_header_syntax(&start.line, &header) == F2C_INTERFACE_HEADER_PARSED &&
               f2c_parse_end_interface_syntax(&end.line, &terminator) ==
                   F2C_INTERFACE_HEADER_PARSED &&
               header.has_generic && terminator.has_generic &&
               f2c_generic_designators_equal(&header.generic, &terminator.generic),
           "END INTERFACE retains a matching structured generic designator");
    parsed_line_discard(&start);
    parsed_line_discard(&end);

    expect(parsed_line_init(&end, "endinterface assignment(=)"),
           "joined ENDINTERFACE form tokenizes");
    expect(f2c_parse_end_interface_syntax(&end.line, &terminator) == F2C_INTERFACE_HEADER_PARSED &&
               terminator.has_generic &&
               terminator.generic.kind == F2C_GENERIC_DESIGNATOR_ASSIGNMENT,
           "joined ENDINTERFACE form uses the same generic designator AST");
    parsed_line_discard(&end);
}

static void test_invalid_headers(void) {
    ParsedLine parsed;
    F2cInterfaceHeaderSyntax syntax;
    expect(parsed_line_init(&parsed, "abstract interface generic"),
           "invalid ABSTRACT INTERFACE tokenizes");
    expect(f2c_parse_interface_header_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_HEADER_INVALID &&
               syntax.error == F2C_INTERFACE_HEADER_ERROR_ABSTRACT_GENERIC,
           "ABSTRACT INTERFACE rejects a generic spec in its structured AST");
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "interface assignment(+)"),
           "malformed assignment generic tokenizes");
    expect(f2c_parse_interface_header_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_HEADER_INVALID &&
               syntax.error == F2C_INTERFACE_HEADER_ERROR_GENERIC,
           "malformed generic specs fail in the INTERFACE AST");
    parsed_line_discard(&parsed);
}

static void test_specific_lists(void) {
    static const char *const sources[] = {
        "module procedure :: Solve_Integer, solve_real",
        "procedure solve_integer, solve_real",
        "100 procedure :: solve",
    };
    size_t index;
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        ParsedLine parsed;
        F2cInterfaceSpecificSyntax syntax;
        expect(parsed_line_init(&parsed, sources[index]), "specific list tokenizes");
        expect(f2c_parse_interface_specific_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_SPECIFIC_PARSED,
               "MODULE PROCEDURE and PROCEDURE lists share one structured AST");
        expect(syntax.name_count != 0U && syntax.names[0]->span.begin.line == 23U,
               "specific procedure names retain source spans");
        f2c_interface_specific_syntax_discard(&syntax);
        parsed_line_discard(&parsed);
    }
}

static void expect_invalid_specific(const char *source, F2cInterfaceSpecificError error,
                                    const char *token_text, const char *message) {
    ParsedLine parsed;
    F2cInterfaceSpecificSyntax syntax;
    expect(parsed_line_init(&parsed, source), "invalid specific list tokenizes");
    expect(f2c_parse_interface_specific_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_SPECIFIC_INVALID &&
               syntax.error == error && syntax.error_token != NULL &&
               f2c_token_equals(syntax.error_token, token_text),
           message);
    f2c_interface_specific_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_invalid_specific_lists(void) {
    expect_invalid_specific("procedure", F2C_INTERFACE_SPECIFIC_ERROR_EMPTY_LIST, "procedure",
                            "an empty PROCEDURE list fails at its keyword");
    expect_invalid_specific("module procedure ::", F2C_INTERFACE_SPECIFIC_ERROR_EMPTY_LIST,
                            "::", "a separator requires a specific name");
    expect_invalid_specific("procedure first second", F2C_INTERFACE_SPECIFIC_ERROR_SEPARATOR,
                            "second", "adjacent names require a comma");
    expect_invalid_specific("procedure first, first", F2C_INTERFACE_SPECIFIC_ERROR_DUPLICATE_NAME,
                            "first", "duplicate specific names fail at the repeated token");
    expect_invalid_specific("procedure first,", F2C_INTERFACE_SPECIFIC_ERROR_TRAILING_COMMA, ",",
                            "a trailing comma is rejected explicitly");
}

static void test_specific_nonmatches(void) {
    static const char *const sources[] = {
        "procedure(interface) :: callback",
        "module subroutine solve(value)",
        "call procedure()",
    };
    size_t index;
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        ParsedLine parsed;
        F2cInterfaceSpecificSyntax syntax;
        expect(parsed_line_init(&parsed, sources[index]), "specific nonmatch tokenizes");
        expect(f2c_parse_interface_specific_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_SPECIFIC_NOT_MATCHED,
               "only an exact PROCEDURE specific list is classified as a binding");
        f2c_interface_specific_syntax_discard(&syntax);
        parsed_line_discard(&parsed);
    }
}

static void test_large_specific_list(void) {
    char source[4096];
    size_t length;
    size_t index;
    ParsedLine parsed;
    F2cInterfaceSpecificSyntax syntax;
    int written = snprintf(source, sizeof(source), "procedure :: ");
    expect(written > 0 && (size_t)written < sizeof(source), "large specific prefix is bounded");
    if (written <= 0 || (size_t)written >= sizeof(source))
        return;
    length = (size_t)written;
    for (index = 0U; index < 128U; ++index) {
        written = snprintf(source + length, sizeof(source) - length, "%sspecific_%03zu",
                           index == 0U ? "" : ", ", index);
        expect(written > 0 && (size_t)written < sizeof(source) - length,
               "large specific name append is bounded");
        if (written <= 0 || (size_t)written >= sizeof(source) - length)
            return;
        length += (size_t)written;
    }
    expect(parsed_line_init(&parsed, source), "large PROCEDURE list tokenizes");
    expect(f2c_parse_interface_specific_syntax(&parsed.line, &syntax) ==
                   F2C_INTERFACE_SPECIFIC_PARSED &&
               syntax.name_count == 128U,
           "specific storage grows dynamically without a fixed limit");
    f2c_interface_specific_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

int main(void) {
    test_interface_headers();
    test_end_interface();
    test_invalid_headers();
    test_specific_lists();
    test_invalid_specific_lists();
    test_specific_nonmatches();
    test_large_specific_list();
    if (failures != 0) {
        fprintf(stderr, "%d INTERFACE AST test(s) failed\n", failures);
        return 1;
    }
    puts("all INTERFACE AST tests passed");
    return 0;
}
