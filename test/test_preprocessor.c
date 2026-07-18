#include "f2c/f2c.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DiagnosticCapture {
    size_t count;
    F2cDiagnostic first;
    char begin_source_name[128];
    char end_source_name[128];
    char spelling_begin_source_name[128];
    char spelling_end_source_name[128];
} DiagnosticCapture;

typedef struct TestBuffer {
    char *data;
    size_t length;
    size_t capacity;
} TestBuffer;

typedef struct IncludeFixture {
    size_t resolve_count;
    size_t release_count;
    F2cIncludeKind last_kind;
} IncludeFixture;

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void expect_contains(const char *text, const char *needle, const char *message) {
    expect(text != NULL && strstr(text, needle) != NULL, message);
}

static void capture_diagnostic(const F2cDiagnostic *diagnostic, void *user_data) {
    DiagnosticCapture *capture = (DiagnosticCapture *)user_data;
    if (capture->count == 0U) {
        capture->first = *diagnostic;
        if (diagnostic->begin.source_name != NULL) {
            (void)snprintf(capture->begin_source_name, sizeof(capture->begin_source_name), "%s",
                           diagnostic->begin.source_name);
            capture->first.begin.source_name = capture->begin_source_name;
        }
        if (diagnostic->end.source_name != NULL) {
            (void)snprintf(capture->end_source_name, sizeof(capture->end_source_name), "%s",
                           diagnostic->end.source_name);
            capture->first.end.source_name = capture->end_source_name;
        }
        if (diagnostic->spelling_begin.source_name != NULL) {
            (void)snprintf(capture->spelling_begin_source_name,
                           sizeof(capture->spelling_begin_source_name), "%s",
                           diagnostic->spelling_begin.source_name);
            capture->first.spelling_begin.source_name = capture->spelling_begin_source_name;
        }
        if (diagnostic->spelling_end.source_name != NULL) {
            (void)snprintf(capture->spelling_end_source_name,
                           sizeof(capture->spelling_end_source_name), "%s",
                           diagnostic->spelling_end.source_name);
            capture->first.spelling_end.source_name = capture->spelling_end_source_name;
        }
    }
    ++capture->count;
}

static F2cConfig config_with_definitions(const F2cPreprocessorDefinition *definitions, size_t count,
                                         DiagnosticCapture *capture) {
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    config.preprocessor_definitions = definitions;
    config.preprocessor_definition_count = count;
    if (capture != NULL) {
        config.diagnostic_callback = capture_diagnostic;
        config.diagnostic_user_data = capture;
    }
    return config;
}

static F2cResult translate(const char *source, const F2cConfig *config) {
    F2cInput input;
    input.source = source;
    input.length = strlen(source);
    input.options = (F2cOptions){"preprocessor.F90", F2C_SOURCE_FREE, 0};
    return f2c_transpile_project_config(&input, 1U, config);
}

static F2cResult translate_project(const char *first, const char *second, const F2cConfig *config) {
    F2cInput inputs[2];
    inputs[0] = (F2cInput){first, strlen(first), {"first.F90", F2C_SOURCE_FREE, 0}};
    inputs[1] = (F2cInput){second, strlen(second), {"second.F90", F2C_SOURCE_FREE, 0}};
    return f2c_transpile_project_config(inputs, 2U, config);
}

static F2cIncludeStatus resolve_test_include(const F2cIncludeRequest *request,
                                             F2cIncludeSource *result, void *user_data) {
    static const char outer[] = "#include \"definitions.inc\"\n";
    static const char definitions[] = "#define INCLUDED_VALUE 11\n";
    static const char invalid[] = "#define INCLUDED_BAD @\nINCLUDED_BAD\n";
    static const char cycle[] = "#include \"cycle.inc\"\n";
    static const char declarations[] = "integer :: local_value\nlocal_value = 31\n";
    IncludeFixture *fixture = (IncludeFixture *)user_data;
    ++fixture->resolve_count;
    fixture->last_kind = request->kind;
    memset(result, 0, sizeof(*result));
    result->source_form = F2C_SOURCE_FREE;
    result->handle = fixture;
    if (strcmp(request->requested_name, "outer.inc") == 0) {
        result->source_name = "virtual/outer.inc";
        result->source = outer;
        result->length = sizeof(outer) - 1U;
    } else if (strcmp(request->requested_name, "definitions.inc") == 0) {
        result->source_name = "virtual/definitions.inc";
        result->source = definitions;
        result->length = sizeof(definitions) - 1U;
    } else if (strcmp(request->requested_name, "invalid.inc") == 0) {
        result->source_name = "virtual/invalid.inc";
        result->source = invalid;
        result->length = sizeof(invalid) - 1U;
    } else if (strcmp(request->requested_name, "cycle.inc") == 0) {
        result->source_name = "virtual/cycle.inc";
        result->source = cycle;
        result->length = sizeof(cycle) - 1U;
    } else if (strcmp(request->requested_name, "declarations.inc") == 0) {
        result->source_name = "virtual/declarations.inc";
        result->source = declarations;
        result->length = sizeof(declarations) - 1U;
    } else {
        result->handle = NULL;
        return F2C_INCLUDE_NOT_FOUND;
    }
    return F2C_INCLUDE_FOUND;
}

static void release_test_include(F2cIncludeSource *source, void *user_data) {
    IncludeFixture *fixture = (IncludeFixture *)user_data;
    expect(source->handle == fixture, "include release receives the resolver-owned handle");
    ++fixture->release_count;
}

static void append(TestBuffer *buffer, const char *text) {
    const size_t length = strlen(text);
    size_t needed;
    char *replacement;
    if (length > SIZE_MAX - buffer->length - 1U) {
        expect(0, "test preprocessor source size remains representable");
        return;
    }
    needed = buffer->length + length + 1U;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
        while (capacity < needed)
            capacity *= 2U;
        replacement = (char *)realloc(buffer->data, capacity);
        if (replacement == NULL) {
            expect(0, "test preprocessor source allocation succeeds");
            return;
        }
        buffer->data = replacement;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, length + 1U);
    buffer->length += length;
}

static void test_reference_lapack_condition(void) {
    static const char source[] = "module la_xisnan\n"
                                 "interface la_isnan\n"
                                 "module procedure sisnan\n"
                                 "end interface\n"
                                 "contains\n"
                                 "logical function sisnan(x)\n"
                                 "use la_constants, only: wp=>sp\n"
                                 "#ifdef USE_IEEE_INTRINSIC\n"
                                 "use, intrinsic :: ieee_arithmetic\n"
                                 "#elif USE_ISNAN\n"
                                 "intrinsic :: isnan\n"
                                 "#endif\n"
                                 "real(wp) :: x\n"
                                 "#ifdef USE_IEEE_INTRINSIC\n"
                                 "sisnan = ieee_is_nan(x)\n"
                                 "#elif USE_ISNAN\n"
                                 "sisnan = isnan(x)\n"
                                 "#else\n"
                                 "sisnan = x /= x\n"
                                 "#endif\n"
                                 "end function sisnan\n"
                                 "end module la_xisnan\n";
    static const F2cPreprocessorDefinition definitions[] = {{"USE_ISNAN", NULL}};
    F2cConfig config = config_with_definitions(definitions, 1U, NULL);
    F2cResult result = translate(source, &config);
    expect(result.error_count == 0U, "explicit LAPACK conditional definition translates");
    expect_contains(result.code, "f2c_module_la_xisnan_sisnan(float *x)",
                    "conditional preprocessing preserves the module procedure");
    expect_contains(result.code, "isnan((*x))",
                    "USE_ISNAN explicitly selects the intrinsic branch");
    expect(result.code == NULL || strstr(result.code, "x /= x") == NULL,
           "inactive LAPACK fallback branch is removed");
    f2c_result_free(&result);
}

static void test_integer_expression_and_definitions(void) {
    static const char source[] =
        "#define OPENMP_LEVEL 201511\n"
        "#if defined(OPENMP_LEVEL) && OPENMP_LEVEL >= 201307 && 2 + 3 * 4 == 14 && "
        "(8 >> 1) == 4 && (1 || 1 / 0) && (0 ? 1 / 0 : 1)\n"
        "subroutine selected_branch()\n"
        "end subroutine selected_branch\n"
        "#else\n"
        "subroutine wrong_branch()\n"
        "end subroutine wrong_branch\n"
        "#endif\n"
        "#undef OPENMP_LEVEL\n"
        "#ifndef OPENMP_LEVEL\n"
        "subroutine undefined_branch()\n"
        "end subroutine undefined_branch\n"
        "#endif\n";
    F2cConfig config = config_with_definitions(NULL, 0U, NULL);
    F2cResult result = translate(source, &config);
    expect(result.error_count == 0U,
           "precedence, short circuit, ternary, define and undef conditions translate");
    expect_contains(result.code, "void selected_branch(void)",
                    "integer conditional expression selects the expected branch");
    expect_contains(result.code, "void undefined_branch(void)",
                    "#undef updates the input-local definition environment");
    expect(result.code == NULL || strstr(result.code, "wrong_branch") == NULL,
           "false conditional branch never reaches the Fortran parser");
    f2c_result_free(&result);
}

static void test_source_macro_expansion(void) {
    static const char source[] = "#define COUNT ALIAS\n"
                                 "#define ALIAS 7\n"
                                 "subroutine expanded_macro(value)\n"
                                 "integer :: value\n"
                                 "value = COUNT\n"
                                 "end subroutine expanded_macro\n"
                                 "#define EMPTY\n"
                                 "EMPTY\n";
    F2cConfig config = config_with_definitions(NULL, 0U, NULL);
    F2cResult result = translate(source, &config);
    expect(result.error_count == 0U, "recursive object-like source macros translate");
    expect_contains(result.code, "(*value) = 7",
                    "source macro replacement reaches the canonical Fortran token stream");
    f2c_result_free(&result);
}

static void test_source_macro_literal_boundaries(void) {
    static const char source[] = "#define VALUE 7\n"
                                 "subroutine literal_macro_boundary(text)\n"
                                 "character(len=5) :: text\n"
                                 "text = 'VALUE' ! VALUE\n"
                                 "end subroutine literal_macro_boundary\n";
    F2cConfig config = config_with_definitions(NULL, 0U, NULL);
    F2cResult result = translate(source, &config);
    expect(result.error_count == 0U, "macro names inside literals and comments remain source text");
    expect_contains(result.code, "\"VALUE\"",
                    "character literal contents are never interpreted as macro tokens");
    f2c_result_free(&result);
}

static void test_case_sensitive_dynamic_nesting(void) {
    TestBuffer source = {0};
    F2cConfig config = config_with_definitions(NULL, 0U, NULL);
    F2cResult result;
    size_t index;
    append(&source, "#define Feature 1\n#ifdef feature\nsubroutine wrong_case()\n"
                    "end subroutine wrong_case\n#else\n");
    for (index = 0U; index < 32U; ++index)
        append(&source, "#if 1\n");
    append(&source, "subroutine deeply_nested()\nend subroutine deeply_nested\n");
    for (index = 0U; index < 32U; ++index)
        append(&source, "#endif\n");
    append(&source, "#endif\n");
    result = translate(source.data, &config);
    expect(result.error_count == 0U, "conditional nesting is dynamic instead of fixed at 16");
    expect_contains(result.code, "void deeply_nested(void)",
                    "32 nested conditional groups retain their active source");
    expect(result.code == NULL || strstr(result.code, "wrong_case") == NULL,
           "conditional names remain case-sensitive");
    f2c_result_free(&result);
    free(source.data);
}

static void test_project_definition_scope(void) {
    static const F2cPreprocessorDefinition definitions[] = {{"SHARED", "1"}};
    static const char first[] = "#define LOCAL 1\n"
                                "#ifdef SHARED\n"
                                "subroutine shared_first()\n"
                                "end subroutine shared_first\n"
                                "#endif\n";
    static const char second[] = "#ifdef LOCAL\n"
                                 "subroutine leaked_local()\n"
                                 "end subroutine leaked_local\n"
                                 "#else\n"
                                 "subroutine isolated_second()\n"
                                 "end subroutine isolated_second\n"
                                 "#endif\n";
    F2cConfig config = config_with_definitions(definitions, 1U, NULL);
    F2cResult result = translate_project(first, second, &config);
    expect(result.error_count == 0U, "project inputs preprocess with isolated local environments");
    expect_contains(result.code, "void shared_first(void)",
                    "request definitions are visible to every project input");
    expect_contains(result.code, "void isolated_second(void)",
                    "an in-source definition does not leak into the next project input");
    expect(result.code == NULL || strstr(result.code, "leaked_local") == NULL,
           "input-local definitions cannot alter later project sources");
    f2c_result_free(&result);
}

static F2cConfig include_config(IncludeFixture *fixture, DiagnosticCapture *capture) {
    F2cConfig config = config_with_definitions(NULL, 0U, capture);
    config.include_resolver = resolve_test_include;
    config.include_release = release_test_include;
    config.include_user_data = fixture;
    return config;
}

static void test_include_resolution(void) {
    static const char source[] = "#include \"outer.inc\"\n"
                                 "subroutine included_macro(value)\n"
                                 "integer :: value\n"
                                 "value = INCLUDED_VALUE\n"
                                 "end subroutine included_macro\n";
    IncludeFixture fixture = {0};
    F2cConfig config = include_config(&fixture, NULL);
    F2cResult result = translate(source, &config);
    expect(result.error_count == 0U, "nested callback-provided include sources translate");
    expect_contains(result.code, "(*value) = 11",
                    "definitions from nested includes remain visible in the including source");
    expect(fixture.resolve_count == 2U && fixture.release_count == 2U,
           "every resolved include is released exactly once");
    expect(fixture.last_kind == F2C_INCLUDE_QUOTED,
           "quoted include requests retain their search-policy kind");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    config = include_config(&fixture, NULL);
    result = translate("#include <definitions.inc>\nsubroutine system_include()\n"
                       "end subroutine system_include\n",
                       &config);
    expect(result.error_count == 0U && fixture.last_kind == F2C_INCLUDE_SYSTEM,
           "system include requests are distinguished from quoted includes");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    config = include_config(&fixture, NULL);
    result = translate("subroutine fortran_include()\ninclude 'declarations.inc'\n"
                       "end subroutine fortran_include\n",
                       &config);
    expect(result.error_count == 0U, "standard Fortran INCLUDE uses the same resolver contract");
    expect_contains(result.code, "local_value = 31",
                    "Fortran INCLUDE content enters normalization and typed IR");
    f2c_result_free(&result);
}

static void test_include_diagnostics_and_limits(void) {
    IncludeFixture fixture = {0};
    DiagnosticCapture capture = {0};
    F2cConfig config = include_config(&fixture, &capture);
    F2cResult result = translate("#include \"invalid.inc\"\n", &config);
    expect(result.error_count != 0U && capture.first.begin.source_name != NULL &&
               strcmp(capture.first.begin.source_name, "virtual/invalid.inc") == 0 &&
               capture.first.begin.line == 2U,
           "diagnostics inside includes retain the resolver-provided source name");
    expect(capture.first.has_spelling_location &&
               strcmp(capture.first.spelling_begin.source_name, "virtual/invalid.inc") == 0 &&
               capture.first.spelling_begin.line == 1U,
           "macro spelling locations inside includes remain independently mapped");
    expect(fixture.release_count == 1U, "failing included sources are still released");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    memset(&capture, 0, sizeof(capture));
    config = include_config(&fixture, &capture);
    result = translate("#include \"missing.inc\"\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_INCLUDE,
           "missing callback-provided includes have a stable diagnostic category");
    expect(fixture.release_count == 0U, "unresolved includes do not invoke the release callback");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    config = include_config(&fixture, NULL);
    result = translate("#include \"cycle.inc\"\n", &config);
    expect(result.error_count != 0U && result.diagnostics != NULL &&
               strstr(result.diagnostics, "recursive include cycle") != NULL,
           "recursive includes terminate with a source-positioned error");
    expect(fixture.release_count == 2U,
           "both outer and cycle-detected include results are released");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    config = include_config(&fixture, NULL);
    config.limits.max_include_depth = 1U;
    result = translate("#include \"outer.inc\"\n", &config);
    expect(result.error_count != 0U && result.diagnostics != NULL &&
               strstr(result.diagnostics, "include depth limit") != NULL,
           "nested include depth obeys its request-local resource limit");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    config = include_config(&fixture, NULL);
    config.limits.max_include_files = 1U;
    result = translate("#include \"outer.inc\"\n", &config);
    expect(result.error_count != 0U && result.diagnostics != NULL &&
               strstr(result.diagnostics, "include file limit") != NULL,
           "resolved include counts obey their request-local resource limit");
    f2c_result_free(&result);

    memset(&fixture, 0, sizeof(fixture));
    config = include_config(&fixture, NULL);
    config.limits.max_input_bytes = 60U;
    result = translate("#include \"outer.inc\"\n", &config);
    expect(result.error_count != 0U && result.diagnostics != NULL &&
               strstr(result.diagnostics, "project input limit") != NULL,
           "included source bytes participate in the aggregate project input budget");
    f2c_result_free(&result);
}

static void expect_preprocessor_error(const char *source, F2cDiagnosticCode code, size_t line,
                                      size_t column, const char *diagnostic_fragment,
                                      const char *message) {
    DiagnosticCapture capture = {0};
    F2cConfig config = config_with_definitions(NULL, 0U, &capture);
    F2cResult result = translate(source, &config);
    expect(result.error_count != 0U && result.code == NULL && result.header == NULL, message);
    expect(capture.count != 0U, "preprocessor error reaches the structured callback");
    expect(capture.first.code == code, "preprocessor error has a stable diagnostic category");
    expect(capture.first.begin.line == line && capture.first.begin.column == column,
           "preprocessor error reports its physical source position");
    expect_contains(result.diagnostics, diagnostic_fragment,
                    "preprocessor text diagnostic explains the rejected construct");
    f2c_result_free(&result);
}

static void test_explicit_unsupported_contract(void) {
    expect_preprocessor_error("  #include \"values.inc\"\n", F2C_DIAGNOSTIC_UNSUPPORTED, 1U, 3U,
                              "#include requires a configured resolver",
                              "#include is a hard error until include resolution is configured");
    expect_preprocessor_error("#define VALUE(x) x\n", F2C_DIAGNOSTIC_UNSUPPORTED, 1U, 9U,
                              "function-like macro is not supported",
                              "function-like definitions cannot be silently miscompiled");
}

static void test_line_remapping(void) {
    static const char source[] = "#line 50 \"definition.f90\"\n"
                                 "#define BAD @\n"
                                 "#line 900 \"consumer.f90\"\n"
                                 "BAD\n";
    DiagnosticCapture capture = {0};
    F2cConfig config = config_with_definitions(NULL, 0U, &capture);
    F2cResult result = translate(source, &config);
    expect(result.error_count != 0U && capture.count != 0U,
           "#line-remapped invalid source produces a diagnostic");
    if (!(capture.first.begin.source_name != NULL &&
          strcmp(capture.first.begin.source_name, "consumer.f90") == 0 &&
          capture.first.begin.line == 900U && capture.first.begin.column == 1U))
        fprintf(stderr, "#line primary was %s:%zu:%zu\n",
                capture.first.begin.source_name != NULL ? capture.first.begin.source_name
                                                        : "<null>",
                capture.first.begin.line, capture.first.begin.column);
    expect(capture.first.begin.source_name != NULL &&
               strcmp(capture.first.begin.source_name, "consumer.f90") == 0 &&
               capture.first.begin.line == 900U && capture.first.begin.column == 1U,
           "#line remaps the primary expansion source and line");
    if (!(capture.first.has_spelling_location && capture.first.spelling_begin.source_name != NULL &&
          strcmp(capture.first.spelling_begin.source_name, "definition.f90") == 0 &&
          capture.first.spelling_begin.line == 50U && capture.first.spelling_begin.column == 13U))
        fprintf(stderr, "#line spelling was %d %s:%zu:%zu\n", capture.first.has_spelling_location,
                capture.first.spelling_begin.source_name != NULL
                    ? capture.first.spelling_begin.source_name
                    : "<null>",
                capture.first.spelling_begin.line, capture.first.spelling_begin.column);
    expect(
        capture.first.has_spelling_location && capture.first.spelling_begin.source_name != NULL &&
            strcmp(capture.first.spelling_begin.source_name, "definition.f90") == 0 &&
            capture.first.spelling_begin.line == 50U && capture.first.spelling_begin.column == 13U,
        "macro spelling locations retain the #line state at definition time");
    expect_contains(result.diagnostics, "consumer.f90:900:1: error:",
                    "text diagnostics render the remapped expansion source");
    expect_contains(result.diagnostics,
                    "definition.f90:50:13: note: expanded from macro definition",
                    "text diagnostics render the remapped spelling source");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(NULL, 0U, &capture);
    result = translate("# 700 \"marker.f90\" 1 3\n@\n", &config);
    expect(result.error_count != 0U && capture.first.begin.source_name != NULL &&
               strcmp(capture.first.begin.source_name, "marker.f90") == 0 &&
               capture.first.begin.line == 700U,
           "standard numeric line markers and flags update source provenance");
    f2c_result_free(&result);

    expect_preprocessor_error("#line 0\n", F2C_DIAGNOSTIC_SYNTAX, 1U, 7U,
                              "invalid source line number", "invalid #line numbers are rejected");
}

static void test_expansion_and_spelling_locations(void) {
    static const char source[] = "#define BAD @\n"
                                 "subroutine invalid_expansion()\n"
                                 "BAD\n"
                                 "end subroutine invalid_expansion\n";
    DiagnosticCapture capture = {0};
    F2cConfig config = config_with_definitions(NULL, 0U, &capture);
    F2cResult result = translate(source, &config);
    expect(result.error_count != 0U && capture.count != 0U,
           "invalid expanded tokens produce a structured diagnostic");
    expect(capture.first.code == F2C_DIAGNOSTIC_INVALID_TOKEN && capture.first.begin.line == 3U &&
               capture.first.begin.column == 1U && capture.first.end.line == 3U &&
               capture.first.end.column == 4U,
           "expanded-token diagnostics identify the complete macro use");
    expect(capture.first.has_spelling_location && capture.first.spelling_begin.line == 1U &&
               capture.first.spelling_begin.column == 13U &&
               capture.first.spelling_end.line == 1U && capture.first.spelling_end.column == 14U,
           "expanded-token diagnostics retain the macro definition spelling range");
    expect_contains(result.diagnostics,
                    "preprocessor.F90:1:13: note: expanded from macro definition",
                    "text diagnostics render the macro spelling location for CLI consumers");
    f2c_result_free(&result);
}

static void test_malformed_conditions(void) {
    expect_preprocessor_error("#endif\n", F2C_DIAGNOSTIC_SYNTAX, 1U, 1U,
                              "#endif has no matching #if", "unmatched #endif is rejected");
    expect_preprocessor_error("#if 1\n#else\n#else\n#endif\n", F2C_DIAGNOSTIC_SYNTAX, 3U, 1U,
                              "more than one #else", "duplicate #else is rejected");
    expect_preprocessor_error("#if 1\nsubroutine missing_end()\nend subroutine missing_end\n",
                              F2C_DIAGNOSTIC_SYNTAX, 1U, 1U, "unterminated conditional",
                              "unterminated conditional group is rejected");
    expect_preprocessor_error("#if 1 / 0\n#endif\n", F2C_DIAGNOSTIC_SYNTAX, 1U, 7U,
                              "division by zero", "evaluated division by zero is rejected");
    expect_preprocessor_error("#define A B\n#define B A\n#if A\n#endif\n", F2C_DIAGNOSTIC_SYNTAX,
                              3U, 5U, "cyclic conditional macro",
                              "cyclic conditional definitions terminate with a hard error");
    expect_preprocessor_error("#define A B\n#define B A\ninteger :: value = A\n",
                              F2C_DIAGNOSTIC_SYNTAX, 3U, 20U, "cyclic source macro expansion",
                              "cyclic source macro definitions terminate with a hard error");
}

static void test_resource_limits(void) {
    static const F2cPreprocessorDefinition two_definitions[] = {{"A", "1"}, {"B", "1"}};
    static const F2cPreprocessorDefinition expansion_definitions[] = {{"A", "B"}, {"B", "1"}};
    DiagnosticCapture capture = {0};
    F2cConfig config = config_with_definitions(two_definitions, 2U, &capture);
    F2cResult result;
    config.limits.max_preprocessor_definitions = 1U;
    result = translate("subroutine limited()\nend subroutine limited\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_RESOURCE_LIMIT,
           "definition count obeys the request-local resource limit");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(expansion_definitions, 2U, &capture);
    config.limits.max_macro_expansion_depth = 1U;
    result = translate("#if A\nsubroutine expanded()\nend subroutine expanded\n#endif\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_RESOURCE_LIMIT,
           "macro expansion depth obeys the request-local resource limit");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(NULL, 0U, &capture);
    config.limits.max_parse_depth = 1U;
    result = translate("#if 1\n#if 1\n#endif\n#endif\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_RESOURCE_LIMIT,
           "conditional nesting obeys the shared parse-depth budget");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(NULL, 0U, &capture);
    config.limits.max_preprocessed_bytes = 8U;
    result = translate("subroutine too_large()\nend subroutine too_large\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_RESOURCE_LIMIT,
           "retained preprocessed bytes obey their request-local budget");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(NULL, 0U, &capture);
    config.limits.max_preprocessed_bytes = 1U;
    result = translate_project("\n", "\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_RESOURCE_LIMIT,
           "preprocessed byte budgets remain bounded after an earlier input exactly exhausts them");
    f2c_result_free(&result);
}

static void test_invalid_configuration(void) {
    static const F2cPreprocessorDefinition invalid_name[] = {{"1INVALID", "1"}};
    static const F2cPreprocessorDefinition multiline_value[] = {{"INVALID", "1\n2"}};
    static const F2cPreprocessorDefinition command_line_bad[] = {{"COMMAND_BAD", "@"}};
    DiagnosticCapture capture = {0};
    F2cConfig config = config_with_definitions(NULL, 1U, &capture);
    F2cResult result =
        translate("subroutine invalid_config()\nend subroutine invalid_config\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_INVALID_ARGUMENT,
           "a null definition array with a nonzero count is rejected as an invalid argument");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(invalid_name, 1U, &capture);
    result = translate("subroutine invalid_name()\nend subroutine invalid_name\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_INVALID_ARGUMENT,
           "invalid API definition names are rejected before source normalization");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(multiline_value, 1U, &capture);
    result = translate("subroutine multiline_value()\nend subroutine multiline_value\n", &config);
    expect(result.error_count != 0U && capture.first.code == F2C_DIAGNOSTIC_INVALID_ARGUMENT,
           "API macro values cannot inject untracked physical lines");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    config = config_with_definitions(command_line_bad, 1U, &capture);
    result = translate("COMMAND_BAD\n", &config);
    expect(result.error_count != 0U && capture.first.has_spelling_location &&
               capture.first.spelling_begin.source_name != NULL &&
               strcmp(capture.first.spelling_begin.source_name, "<command-line>") == 0,
           "API macro expansions identify their command-line spelling source");
    f2c_result_free(&result);
}

static void test_bom_crlf_and_final_line(void) {
    static const char source[] = "\xEF\xBB\xBF#if 1\r\nsubroutine portable_line_endings()\r\n"
                                 "end subroutine portable_line_endings\r\n#endif";
    F2cConfig config = config_with_definitions(NULL, 0U, NULL);
    F2cResult result = translate(source, &config);
    expect(result.error_count == 0U, "BOM, CRLF and a final line without newline preprocess");
    expect_contains(result.code, "void portable_line_endings(void)",
                    "physical line-ending normalization preserves active Fortran");
    f2c_result_free(&result);
}

int main(void) {
    test_reference_lapack_condition();
    test_integer_expression_and_definitions();
    test_source_macro_expansion();
    test_source_macro_literal_boundaries();
    test_case_sensitive_dynamic_nesting();
    test_project_definition_scope();
    test_include_resolution();
    test_include_diagnostics_and_limits();
    test_explicit_unsupported_contract();
    test_line_remapping();
    test_expansion_and_spelling_locations();
    test_malformed_conditions();
    test_resource_limits();
    test_invalid_configuration();
    test_bom_crlf_and_final_line();
    if (failures != 0)
        fprintf(stderr, "%d preprocessor test(s) failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
