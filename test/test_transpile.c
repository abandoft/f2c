#include "f2c/f2c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

typedef struct DiagnosticCapture {
    size_t count;
    F2cDiagnosticCode code;
    F2cDiagnosticSeverity severity;
    size_t line;
    size_t column;
    size_t end_line;
    size_t end_column;
    char source_name[64];
    char message[160];
} DiagnosticCapture;

static void capture_diagnostic(const F2cDiagnostic *diagnostic, void *user_data) {
    DiagnosticCapture *capture = (DiagnosticCapture *)user_data;
    const size_t source_length =
        diagnostic->begin.source_name != NULL ? strlen(diagnostic->begin.source_name) : 0U;
    const size_t bounded_source_length = source_length < sizeof(capture->source_name) - 1U
                                             ? source_length
                                             : sizeof(capture->source_name) - 1U;
    const size_t bounded_message_length = diagnostic->message_length < sizeof(capture->message) - 1U
                                              ? diagnostic->message_length
                                              : sizeof(capture->message) - 1U;
    ++capture->count;
    if (capture->count > 1U && diagnostic->severity < capture->severity)
        return;
    capture->code = diagnostic->code;
    capture->severity = diagnostic->severity;
    capture->line = diagnostic->begin.line;
    capture->column = diagnostic->begin.column;
    capture->end_line = diagnostic->end.line;
    capture->end_column = diagnostic->end.column;
    memcpy(capture->source_name, diagnostic->begin.source_name, bounded_source_length);
    capture->source_name[bounded_source_length] = '\0';
    memcpy(capture->message, diagnostic->message, bounded_message_length);
    capture->message[bounded_message_length] = '\0';
}

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void expect_contains(const char *text, const char *needle, const char *message) {
    expect(text != NULL && strstr(text, needle) != NULL, message);
}

static void test_version_contract(void) {
    expect(F2C_VERSION_MAJOR == 1 && F2C_VERSION_MINOR == 3 && F2C_VERSION_PATCH == 1,
           "public version macros match the CMake project version");
    expect(strcmp(f2c_version(), "1.3.1") == 0,
           "runtime version matches the public compile-time version");
}

static void test_empty_source(void) {
    F2cResult result = f2c_transpile("", 0U, NULL);
    expect(result.error_count == 1U, "empty source reports one error");
    expect_contains(result.diagnostics, "no PROGRAM", "empty-source diagnostic is actionable");
    f2c_result_free(&result);
}

static F2cConfig limited_config(void) {
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    return config;
}

static void test_resource_limits(void) {
    static const char source[] =
        "program limits\ninteger :: value\nvalue = 1\nend program limits\n";
    F2cOptions options = {"limits.f90", F2C_SOURCE_FREE, 0};
    F2cInput input = {source, sizeof(source) - 1U, options};
    F2cConfig config = limited_config();
    F2cResult result;

    {
        DiagnosticCapture capture = {0};
        config.diagnostic_callback = capture_diagnostic;
        config.diagnostic_user_data = &capture;
        result = f2c_transpile_project_config(NULL, 1U, &config);
        expect(capture.count == 1U, "structured diagnostics report each public API error once");
        expect(capture.code == F2C_DIAGNOSTIC_INVALID_ARGUMENT,
               "structured diagnostics expose a stable invalid-argument code");
        expect(capture.severity == F2C_DIAGNOSTIC_ERROR && capture.line == 1U &&
                   capture.column == 1U,
               "structured diagnostics expose severity and source coordinates");
        expect(strcmp(capture.source_name, "<input>") == 0,
               "structured diagnostics expose a deterministic source name");
        expect_contains(capture.message, "no project inputs",
                        "structured diagnostic messages remain actionable during callbacks");
        f2c_result_free(&result);
    }

    config = limited_config();
    config.limits.max_input_bytes = 8U;
    result = f2c_transpile_project_config(&input, 1U, &config);
    expect(result.error_count != 0U && result.code == NULL,
           "project input budgets fail before source normalization");
    expect_contains(result.diagnostics, "project input limit of 8 bytes exceeded",
                    "input-budget diagnostics identify the configured limit");
    f2c_result_free(&result);

    config = limited_config();
    config.limits.max_logical_lines = 2U;
    result = f2c_transpile_project_config(&input, 1U, &config);
    expect_contains(result.diagnostics, "logical-line limit of 2 exceeded",
                    "logical-line budgets stop source expansion");
    f2c_result_free(&result);

    config = limited_config();
    config.limits.max_tokens = 3U;
    result = f2c_transpile_project_config(&input, 1U, &config);
    expect_contains(result.diagnostics, "token limit of 3 exceeded",
                    "token budgets stop syntax construction");
    f2c_result_free(&result);

    {
        static const char expression_source[] =
            "program expression_limits\ninteger :: value\nvalue = 1 + 2 + 3\nend program "
            "expression_limits\n";
        F2cInput expression_input = {
            expression_source,
            sizeof(expression_source) - 1U,
            {"expression_limits.f90", F2C_SOURCE_FREE, 0},
        };
        config = limited_config();
        config.limits.max_ast_nodes = 2U;
        result = f2c_transpile_project_config(&expression_input, 1U, &config);
        expect_contains(result.diagnostics, "expression AST-node limit of 2 exceeded",
                        "AST-node budgets stop syntax-tree amplification");
        f2c_result_free(&result);

        config = limited_config();
        config.limits.max_parse_depth = 2U;
        result = f2c_transpile_project_config(&expression_input, 1U, &config);
        expect_contains(result.diagnostics, "expression parse-depth limit of 2 exceeded",
                        "expression-depth budgets stop pathological nesting");
        f2c_result_free(&result);
    }

    {
        static const char constant_source[] =
            "program constant_limits\ninteger :: values(1 + 2)\nvalues = 0\nend program "
            "constant_limits\n";
        F2cInput constant_input = {
            constant_source,
            sizeof(constant_source) - 1U,
            {"constant_limits.f90", F2C_SOURCE_FREE, 0},
        };
        config = limited_config();
        config.limits.max_constant_steps = 1U;
        result = f2c_transpile_project_config(&constant_input, 1U, &config);
        expect_contains(result.diagnostics, "constant-evaluation step limit of 1 exceeded",
                        "constant-evaluation budgets stop recursive semantic work");
        f2c_result_free(&result);
    }

    config = limited_config();
    config.limits.max_output_bytes = 64U;
    result = f2c_transpile_project_config(&input, 1U, &config);
    expect_contains(result.diagnostics, "generated output limit of 64 bytes exceeded",
                    "generated-output budgets stop emitter amplification");
    expect(result.code == NULL, "output-budget failures never return partial generated C");
    f2c_result_free(&result);

    config = limited_config();
    config.limits.max_diagnostic_bytes = 32U;
    result = f2c_transpile_project_config(NULL, 1U, &config);
    expect(result.error_count != 0U && result.diagnostics != NULL &&
               strlen(result.diagnostics) <= 32U,
           "diagnostic byte budgets preserve a valid bounded result buffer");
    expect(result.diagnostics == NULL || strstr(result.diagnostics, "out of memory") == NULL,
           "diagnostic truncation is not misreported as allocation failure");
    f2c_result_free(&result);

    {
        static const char diagnostic_source[] =
            "program diagnostics\nbackspace 1\nbackspace 2\nend program diagnostics\n";
        F2cInput diagnostic_input = {
            diagnostic_source,
            sizeof(diagnostic_source) - 1U,
            {"diagnostics.f90", F2C_SOURCE_FREE, 0},
        };
        config = limited_config();
        config.limits.max_diagnostics = 1U;
        result = f2c_transpile_project_config(&diagnostic_input, 1U, &config);
        expect_contains(result.diagnostics, "diagnostic count limit reached",
                        "diagnostic-count budgets emit one stable suppression notice");
        f2c_result_free(&result);
    }

    {
        static const char invalid_source[] = "program invalid\n@\nend program invalid\n";
        DiagnosticCapture capture = {0};
        F2cInput invalid_input = {
            invalid_source,
            sizeof(invalid_source) - 1U,
            {"invalid.f90", F2C_SOURCE_FREE, 0},
        };
        config = limited_config();
        config.diagnostic_callback = capture_diagnostic;
        config.diagnostic_user_data = &capture;
        result = f2c_transpile_project_config(&invalid_input, 1U, &config);
        expect(capture.code == F2C_DIAGNOSTIC_INVALID_TOKEN,
               "lexer failures expose a stable invalid-token code");
        expect(capture.line == 2U && capture.column == 1U &&
                   strcmp(capture.source_name, "invalid.f90") == 0,
               "lexer diagnostics preserve exact file, line, and column coordinates");
        f2c_result_free(&result);
    }

    {
        DiagnosticCapture invalid_capture = {0};
        config = limited_config();
        config.structure_size = sizeof(config.structure_size);
        config.diagnostic_callback = capture_diagnostic;
        config.diagnostic_user_data = &invalid_capture;
        result = f2c_transpile_project_config(&input, 1U, &config);
        expect_contains(result.diagnostics, "configuration structure size does not match",
                        "the current API rejects smaller configuration layouts");
        expect(invalid_capture.count == 0U,
               "a mismatched configuration does not read fields beyond structure_size");
        f2c_result_free(&result);
    }

    {
        DiagnosticCapture invalid_capture = {0};
        config = limited_config();
        config.structure_size = sizeof(config) + 1U;
        config.diagnostic_callback = capture_diagnostic;
        config.diagnostic_user_data = &invalid_capture;
        result = f2c_transpile_project_config(&input, 1U, &config);
        expect_contains(result.diagnostics, "configuration structure size does not match",
                        "the current API rejects larger configuration layouts");
        expect(invalid_capture.count == 0U,
               "a larger unknown configuration layout is not partially interpreted");
        f2c_result_free(&result);
    }

    result = f2c_transpile_project_config(NULL, 1U, NULL);
    expect_contains(result.diagnostics, "no project inputs were provided",
                    "a null project input array is rejected without dereferencing it");
    f2c_result_free(&result);

    input.source = NULL;
    input.length = 1U;
    result = f2c_transpile_project_config(&input, 1U, NULL);
    expect_contains(result.diagnostics, "null source buffer with a nonzero length",
                    "a null source buffer cannot claim readable bytes");
    f2c_result_free(&result);

    input.source = source;
    input.length = sizeof(source) - 1U;
    input.options.source_form = (F2cSourceForm)99;
    result = f2c_transpile_project_config(&input, 1U, NULL);
    expect_contains(result.diagnostics, "invalid source-form value",
                    "invalid public source-form values are rejected deterministically");
    f2c_result_free(&result);

    {
        static const char embedded_nul[] = "program nul\n\0end program nul\n";
        input.source = embedded_nul;
        input.length = sizeof(embedded_nul) - 1U;
        input.options.source_form = F2C_SOURCE_FREE;
        result = f2c_transpile_project_config(&input, 1U, NULL);
        expect_contains(result.diagnostics, "embedded NUL byte",
                        "embedded NUL bytes are diagnosed before normalization");
        f2c_result_free(&result);
    }
}

static DiagnosticCapture transpile_invalid_location(const char *source, F2cOptions options) {
    DiagnosticCapture capture = {0};
    F2cInput input = {source, strlen(source), options};
    F2cConfig config = limited_config();
    F2cResult result;
    config.diagnostic_callback = capture_diagnostic;
    config.diagnostic_user_data = &capture;
    result = f2c_transpile_project_config(&input, 1U, &config);
    f2c_result_free(&result);
    return capture;
}

static void test_physical_source_mapping(void) {
    static const char continued[] = "program mapped\n"
                                    "  value = 1 + &\n"
                                    "    @\n"
                                    "end program mapped\n";
    static const char separated[] = "program mapped; @\n"
                                    "end program mapped\n";
    static const char fixed[] = "      PROGRAM MAPPED\n"
                                "      VALUE = 1 +\n"
                                "     &  @\n"
                                "      END\n";
    static const char declaration[] = "subroutine mapped(value)\n"
                                      "  optional :: value, &\n"
                                      "    42\n"
                                      "end subroutine mapped\n";
    DiagnosticCapture capture =
        transpile_invalid_location(continued, (F2cOptions){"continued.f90", F2C_SOURCE_FREE, 0});
    expect(capture.code == F2C_DIAGNOSTIC_INVALID_TOKEN && capture.line == 3U &&
               capture.column == 5U && capture.end_line == 3U && capture.end_column == 6U &&
               strcmp(capture.source_name, "continued.f90") == 0,
           "free-form continuation diagnostics map to the exact physical token range");

    capture =
        transpile_invalid_location(separated, (F2cOptions){"separated.f90", F2C_SOURCE_FREE, 0});
    expect(capture.code == F2C_DIAGNOSTIC_INVALID_TOKEN && capture.line == 1U &&
               capture.column == 17U && capture.end_line == 1U && capture.end_column == 18U,
           "semicolon-split logical statements preserve their original physical columns");

    capture = transpile_invalid_location(fixed, (F2cOptions){"mapped.f", F2C_SOURCE_FIXED, 0});
    expect(capture.code == F2C_DIAGNOSTIC_INVALID_TOKEN && capture.line == 3U &&
               capture.column == 9U && capture.end_line == 3U && capture.end_column == 10U,
           "fixed-form continuation diagnostics preserve continuation-line columns");

    capture = transpile_invalid_location(declaration,
                                         (F2cOptions){"declaration.f90", F2C_SOURCE_FREE, 0});
    if (!(capture.code == F2C_DIAGNOSTIC_SYNTAX && capture.line == 3U && capture.column == 5U &&
          capture.end_line == 3U && capture.end_column == 7U))
        fprintf(stderr, "semantic mapping was %d:%zu:%zu-%zu:%zu (%s)\n", (int)capture.code,
                capture.line, capture.column, capture.end_line, capture.end_column,
                capture.message);
    expect(capture.code == F2C_DIAGNOSTIC_SYNTAX && capture.line == 3U && capture.column == 5U &&
               capture.end_line == 3U && capture.end_column == 7U,
           "token-based semantic diagnostics retain continuation-line source ranges");
}

static void test_program_and_control_flow(void) {
    static const char source[] = "program sum_numbers\n"
                                 "  implicit none\n"
                                 "  integer :: i, total\n"
                                 "  total = 0\n"
                                 "  do i = 1, 10\n"
                                 "    if (mod(i, 2) == 0) then\n"
                                 "      total = total + i\n"
                                 "    end if\n"
                                 "  end do\n"
                                 "  if (total /= 30) error stop\n"
                                 "end program sum_numbers\n";
    F2cOptions options = {"sum_numbers.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "free-form program translates without errors");
    expect_contains(result.code, "Portable C17", "generated source declares the C17 contract");
    expect_contains(result.code, "#if defined(F2C_FP_CONTRACT)",
                    "generated source exposes the floating-point contraction profile");
    expect_contains(result.code, "#pragma STDC FP_CONTRACT OFF",
                    "generated source defaults to reproducible floating-point evaluation");
    expect_contains(result.code, "#define F2C_LOOP_UNROLL",
                    "generated source defines a portable loop optimization hint");
    expect_contains(result.code, "__STDC_VERSION__ < 201710L",
                    "generated source rejects pre-C17 compilation modes");
    expect_contains(result.code, "int main(void)", "PROGRAM maps to C main");
    expect_contains(result.code, "for (; f2c_do_count_", "DO maps to a counted C loop");
    expect_contains(result.code, "const int32_t f2c_do_start_",
                    "DO initial value is evaluated exactly once");
    expect_contains(result.code, "const int32_t f2c_do_limit_",
                    "DO limit is evaluated exactly once");
    expect_contains(result.code, "const int32_t f2c_do_step_", "DO step is evaluated exactly once");
    expect_contains(result.code, "int32_t f2c_do_count_",
                    "canonical default-integer DO uses a native-width trip counter");
    expect_contains(result.code, "F2C_LOOP_UNROLL\n    for (;",
                    "isolated counted loops receive the portable optimization hint");
    expect_contains(result.code, "F2C_MOD(i, 2)", "MOD intrinsic is preserved without a runtime");
    expect_contains(result.code, "return EXIT_FAILURE", "ERROR STOP maps to a failure exit");
    f2c_result_free(&result);
}

static void test_wide_do_trip_count(void) {
    static const char source[] = "subroutine wide_do(n, observed)\n"
                                 "  integer :: n, observed, i\n"
                                 "  observed = 0\n"
                                 "  do i = -2147483647, n\n"
                                 "    observed = observed + 1\n"
                                 "  end do\n"
                                 "end subroutine wide_do\n";
    F2cOptions options = {"wide_do.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "wide-trip-count DO translates without errors");
    expect_contains(result.code, "int64_t f2c_do_count_",
                    "DO ranges spanning the default-integer domain retain a wide trip counter");
    f2c_result_free(&result);

    {
        static const char stride_source[] = "subroutine stride_do(first, last, observed)\n"
                                            "  integer :: first, last, observed, i\n"
                                            "  observed = 0\n"
                                            "  do i = first, last, 4\n"
                                            "    observed = observed + 1\n"
                                            "  end do\n"
                                            "end subroutine stride_do\n";
        F2cOptions stride_options = {"stride_do.f90", F2C_SOURCE_FREE, 0};
        F2cResult stride = f2c_transpile(stride_source, strlen(stride_source), &stride_options);
        expect(stride.error_count == 0U, "wide-stride default-integer DO translates");
        expect_contains(stride.code, "int32_t f2c_do_count_",
                        "a constant stride of four has a provably native-width trip count");
        f2c_result_free(&stride);
    }
    {
        static const char positive_source[] = "subroutine positive_do(last, observed)\n"
                                              "  integer :: last, observed, i\n"
                                              "  observed = 0\n"
                                              "  do i = 2, last\n"
                                              "    observed = observed + 1\n"
                                              "  end do\n"
                                              "end subroutine positive_do\n";
        F2cOptions positive_options = {"positive_do.f90", F2C_SOURCE_FREE, 0};
        F2cResult positive =
            f2c_transpile(positive_source, strlen(positive_source), &positive_options);
        expect(positive.error_count == 0U, "positive-start default-integer DO translates");
        expect_contains(positive.code, "int32_t f2c_do_count_",
                        "a unit-stride range starting at two has a proven native-width count");
        f2c_result_free(&positive);
    }
}

static void test_blas_style_subroutine(void) {
    static const char source[] = "subroutine daxpy(n, da, dx, incx, dy, incy)\n"
                                 "  implicit none\n"
                                 "  integer, intent(in) :: n, incx, incy\n"
                                 "  double precision, intent(in) :: da\n"
                                 "  double precision, intent(in) :: dx(*)\n"
                                 "  double precision, intent(inout) :: dy(*)\n"
                                 "  integer :: i, ix, iy\n"
                                 "  ix = 1\n"
                                 "  iy = 1\n"
                                 "  do i = 1, n\n"
                                 "    dy(iy) = dy(iy) + da * dx(ix)\n"
                                 "    ix = ix + incx\n"
                                 "    iy = iy + incy\n"
                                 "  end do\n"
                                 "end subroutine daxpy\n";
    F2cOptions options = {"daxpy.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "BLAS-style subroutine translates without errors");
    expect_contains(result.code, "void daxpy(", "SUBROUTINE maps to a void function");
    expect_contains(result.code, "const int32_t *n",
                    "INTENT(IN) scalar uses a const, alias-safe reference");
    expect_contains(result.code, "void daxpy(const int32_t *n",
                    "the public C ABI does not expose optimizer-only alias contracts");
    expect_contains(result.code, "const double *F2C_RESTRICT dx",
                    "the internal implementation preserves Fortran alias optimization");
    expect_contains(result.code, "dy[(((int32_t)(iy)) - (1))]",
                    "Fortran one-based array is rebased with an integer subscript");
    expect_contains(result.code, "int64_t f2c_do_count_",
                    "dynamic unit-stride loops retain the optimizer-friendly wide trip counter");
    f2c_result_free(&result);
}

static void test_nested_loop_optimization_hints(void) {
    static const char source[] = "subroutine nested_loops(n, x)\n"
                                 "  integer :: n, i, j, k\n"
                                 "  real :: x(n, n, n)\n"
                                 "  do i = 1, n\n"
                                 "    do j = 1, n\n"
                                 "      do k = 1, n\n"
                                 "        x(k, j, i) = 0.0\n"
                                 "      end do\n"
                                 "    end do\n"
                                 "  end do\n"
                                 "end subroutine nested_loops\n";
    F2cOptions options = {"nested_loops.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "nested counted loops translate without errors");
    expect(result.code == NULL || strstr(result.code, "F2C_LOOP_UNROLL\n    for (;") == NULL,
           "three-level loop nests do not force-unroll the outermost loop");
    expect(result.code == NULL || strstr(result.code, "F2C_LOOP_UNROLL\n        for (;") == NULL,
           "nested loop parents are left to the compiler cost model");
    expect_contains(result.code, "F2C_LOOP_UNROLL\n            for (;",
                    "the innermost counted loop receives an unroll hint");
    f2c_result_free(&result);
}

static void test_fixed_form_continuation(void) {
    static const char source[] = "      SUBROUTINE SCALE(N, A, X)\n"
                                 "      INTEGER N, I\n"
                                 "      DOUBLE PRECISION A, X(*)\n"
                                 "      DO I = 1, N\n"
                                 "      X(I) = X(I)\n"
                                 "     1       * A\n"
                                 "      END DO\n"
                                 "      END\n";
    F2cOptions options = {"scale.f", F2C_SOURCE_FIXED, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "fixed-form source translates without errors");
    expect_contains(result.code, "void scale(", "fixed-form names are normalized");
    expect_contains(result.code, "* (*a)", "column-six continuation joins expressions");
    f2c_result_free(&result);
}

static void test_fixed_form_spaced_exponent(void) {
    static const char source[] = "      SUBROUTINE SPACED_EXPONENT(X)\n"
                                 "      REAL X\n"
                                 "      DATA X / -1. E0 /\n"
                                 "      END\n";
    F2cOptions options = {"spaced_exponent.f", F2C_SOURCE_FIXED, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "fixed-form blanks inside an exponent do not split the numeric literal");
    expect_contains(result.code, "-1.e0f",
                    "fixed-form spaced exponent is normalized before typed AST parsing");
    f2c_result_free(&result);
}

static void test_extended_fixed_form_width(void) {
    static const char source[] =
        "      SUBROUTINE DRIVER()\n"
        "      EXTERNAL           ZLASET, ZLATME, ZLATMR, ZLATMS, ZTREVC, ZTREVC3,\n"
        "     $                   ZUNGHR, ZUNMHR\n"
        "      END\n";
    F2cOptions options = {"driver.f", F2C_SOURCE_FIXED, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "132-column fixed form preserves column 73");
    expect_contains(result.code, "extern void ztrevc3(void);",
                    "extended fixed-form punctuation keeps procedures separate");
    expect_contains(result.code, "extern void zunghr(void);",
                    "continued EXTERNAL declaration retains its first procedure");
    expect(result.code == NULL || strstr(result.code, "ztrevc3 zunghr") == NULL,
           "fixed-form continuation never merges adjacent procedure names");
    f2c_result_free(&result);
}

static void test_function_result_and_power(void) {
    static const char source[] = "double precision function square_plus(x) result(answer)\n"
                                 "  double precision, intent(in) :: x\n"
                                 "  double precision :: answer\n"
                                 "  answer = x ** 2 + 1.0d0\n"
                                 "end function square_plus\n";
    F2cOptions options = {"function.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "typed function translates without errors");
    expect_contains(result.code, "double square_plus(const double *x)",
                    "typed function signature is retained");
    expect_contains(result.code, "f2c_result =", "RESULT variable maps to a clear C name");
    expect_contains(result.code, "f2c_square_d((*x))",
                    "constant square uses a single-evaluation multiplication helper");
    expect_contains(result.code, "1.0e0", "D exponent maps to a C exponent");
    f2c_result_free(&result);
}

static void test_legacy_blas_constructs(void) {
    static const char source[] = "      SUBROUTINE LEGACY(N,X)\n"
                                 "      INTEGER N,I\n"
                                 "      DOUBLE PRECISION X(*),SCALE\n"
                                 "      DATA SCALE/2.0D0/\n"
                                 "      DO 10 I=1,N\n"
                                 "      X(I)=SCALE*X(I)\n"
                                 "   10 CONTINUE\n"
                                 "      END\n";
    F2cOptions options = {"legacy.f", F2C_SOURCE_FIXED, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "legacy labeled DO and DATA translate");
    expect(result.warning_count == 0U, "supported legacy constructs produce no warning");
    expect_contains(result.code, "scale = 2.0e0;", "scalar DATA initializes its target");
    expect_contains(result.code, "for (; f2c_do_count_",
                    "labeled DO maps to structured control flow");
    f2c_result_free(&result);
}

static void test_character_and_external_interface(void) {
    static const char source[] = "logical function same_letter(left, right)\n"
                                 "  character*( * ) left, right\n"
                                 "  logical lsame\n"
                                 "  external lsame\n"
                                 "  same_letter = lsame(left, right)\n"
                                 "end function same_letter\n"
                                 "subroutine forward_fixed(name)\n"
                                 "  character*4 name\n"
                                 "  character ( len = 3 ) :: path\n"
                                 "  path = 'ABI'\n"
                                 "  call consume_text(name, 'XY')\n"
                                 "end subroutine forward_fixed\n"
                                 "subroutine forward_readonly(n, values)\n"
                                 "  integer, intent(in) :: n\n"
                                 "  real, intent(in) :: values(*)\n"
                                 "  external consume_values\n"
                                 "  call consume_values(n, values)\n"
                                 "end subroutine forward_readonly\n"
                                 "double precision function inspect_readonly(n)\n"
                                 "  integer, intent(in) :: n\n"
                                 "  double precision inspect_value\n"
                                 "  external inspect_value\n"
                                 "  inspect_readonly = inspect_value(n)\n"
                                 "end function inspect_readonly\n";
    F2cOptions options = {"character.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "character arguments and external function translate");
    expect_contains(result.code,
                    "int32_t same_letter(char *left, char *right, "
                    "size_t f2c_len_left, size_t f2c_len_right)",
                    "assumed-length CHARACTER arguments expose explicit trailing lengths");
    expect_contains(result.code, "extern int32_t lsame(char *, char *, size_t, size_t);",
                    "external character interface includes strict hidden-length parameters");
    expect_contains(result.code, "lsame(left, right, f2c_len_left, f2c_len_right)",
                    "spaced assumed-length CHARACTER propagates actual lengths");
    expect_contains(result.code, "void forward_fixed(char *name, size_t f2c_len_name)",
                    "fixed-length character dummies retain ABI actual-length parameters");
    expect_contains(result.code, "char path[(3) + 1]",
                    "spaced CHARACTER(LEN=...) selector retains its explicit length");
    expect_contains(result.code, "consume_text(name, \"XY\", 4, 2U)",
                    "fixed dummies forward declared length while literals use static length");
    expect_contains(result.code, "extern void consume_text(char *, char *, size_t, size_t);",
                    "implicit external prototypes do not infer dummy constness from actuals");
    expect_contains(result.code,
                    "consume_values(f2c_implicit_mutable_actual(n), "
                    "f2c_implicit_mutable_actual(values));",
                    "legacy calls bridge const dummy actuals without changing the external ABI");
    expect_contains(result.code, "inspect_value(f2c_implicit_mutable_actual(n))",
                    "legacy function calls bridge const dummy actuals without a cast warning");
    f2c_result_free(&result);
}

static void test_complex_temporary_arguments(void) {
    static const char source[] = "subroutine invoke_complex(value)\n"
                                 "  real :: value\n"
                                 "  external consume_complex\n"
                                 "  call consume_complex(-cmplx(value, huge(value)))\n"
                                 "end subroutine invoke_complex\n";
    F2cOptions options = {"complex_argument.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "complex temporary argument translates");
    expect_contains(result.code, "&((f2c_complex_float[]){",
                    "CMPLX actual argument retains its complex storage type");
    expect_contains(result.code, "f2c_make_c(",
                    "CMPLX constructs infinite components without algebraic contamination");
    expect(result.code == NULL || strstr(result.code, "&(float){f2c_cneg(") == NULL,
           "complex actual argument is never narrowed to a real temporary");
    f2c_result_free(&result);
}

static void test_typed_integer_and_nested_call_expressions(void) {
    static const char source[] = "subroutine typed_expression(n, nwork, x, info)\n"
                                 "  integer :: n, nwork, info\n"
                                 "  double precision :: x(*)\n"
                                 "  double precision dnrm2, dlapy2\n"
                                 "  external dnrm2, dlapy2\n"
                                 "  if (5*n+2*n**2.gt.nwork) info = abs(-n)\n"
                                 "  x(1) = dlapy2(dnrm2(n, x, 1), dnrm2(n, x, 1))\n"
                                 "end subroutine typed_expression\n";
    F2cOptions options = {"typed_expression.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "typed nested expressions translate without errors");
    expect_contains(result.code, "> (*nwork)",
                    "adjacent dotted comparison is not consumed as a real literal");
    expect_contains(result.code, "((int32_t)pow((double)",
                    "integer exponentiation retains INTEGER result type");
    expect_contains(result.code, "abs((-(*n)))", "integer ABS resolves to the integer C function");
    expect_contains(result.code, "&(double){dnrm2(",
                    "nested DOUBLE function result uses a DOUBLE temporary");
    f2c_result_free(&result);
}

static void test_single_complex_specific_abs(void) {
    static const char source[] = "subroutine specific_abs(value, magnitude)\n"
                                 "  complex :: value\n"
                                 "  real :: magnitude\n"
                                 "  intrinsic cabs\n"
                                 "  magnitude = cabs(value)\n"
                                 "end subroutine specific_abs\n";
    F2cOptions options = {"specific_abs.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "single-complex CABS translates without errors");
    expect_contains(result.code, "cabsf((*value))",
                    "specific CABS preserves its single-precision COMPLEX argument kind");
    expect(result.code == NULL || strstr(result.code, "cabs((*value))") == NULL,
           "specific CABS never introduces a DOUBLE result into REAL arithmetic");
    f2c_result_free(&result);
}

static void test_lapack_f90_semantics(void) {
    static const char source[] = "subroutine scaled_sum(n, work, result)\n"
                                 "  use la_constants, only: wp=>sp, zero=>szero, one=>sone\n"
                                 "  integer :: n, result\n"
                                 "  real(wp) :: work(n)\n"
                                 "  result = maxloc(work(1:n), 1)\n"
                                 "  if (work(result) == zero) work(result) = one\n"
                                 "end subroutine scaled_sum\n"
                                 "subroutine intrinsic_name_array(sum, observed)\n"
                                 "  real :: sum(2, 2), observed\n"
                                 "  sum(1, 1) = 7.0\n"
                                 "  observed = sum(1, 1)\n"
                                 "end subroutine intrinsic_name_array\n"
                                 "subroutine character_section(name)\n"
                                 "  character*12 :: name\n"
                                 "  name(2:6) = 'GEQRF'\n"
                                 "end subroutine character_section\n"
                                 "subroutine copy_section(k, ldb, b)\n"
                                 "  integer :: k, ldb\n"
                                 "  real :: b(ldb, *)\n"
                                 "  real :: h(2, 3)\n"
                                 "  h = b(k+1:k+2, k:k+2)\n"
                                 "end subroutine copy_section\n";
    F2cOptions options = {"lapack_semantics.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "LAPACK F90 module and section semantics translate");
    expect_contains(result.code, "float *F2C_RESTRICT work",
                    "LA_CONSTANTS aliases import with the correct precision");
    expect_contains(result.code, "((float)(0.0f))",
                    "imported zero constant is folded with its declared precision");
    expect_contains(result.code, "F2C_MAXIMUM_LOCATION((&work[",
                    "MAXLOC array sections lower to a typed strided C17 reduction");
    expect_contains(result.code, "float *F2C_RESTRICT sum",
                    "a declared array shadows an intrinsic with the same name");
    expect_contains(result.code, "memmove((&name[f2c_substring_offset",
                    "character substring assignment uses checked typed-AST bounds");
    expect_contains(result.code, "int32_t f2c_row, f2c_column",
                    "rank-two array section assignment lowers to column-major loops");
    f2c_result_free(&result);
}

static void test_standalone_lapack_constants_module(void) {
    static const char source[] = "module la_constants\n"
                                 "  integer, parameter :: sp = kind(1.e0)\n"
                                 "end module la_constants\n";
    F2cOptions options = {"la_constants.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "standalone LA_CONSTANTS module translates");
    expect_contains(result.code, "const float f2c_la_constants_szero",
                    "standalone module exports namespaced C constants");
    expect_contains(result.code, "const f2c_complex_double f2c_la_constants_zone",
                    "standalone module preserves double-complex constants");
    expect_contains(result.code, "const char f2c_la_constants_sprefix = 'S'",
                    "standalone module emits portable character constant initializers");
    f2c_result_free(&result);
}

static void test_allocatable_arrays(void) {
    static const char source[] = "subroutine dynamic_workspace(n, status)\n"
                                 "  integer :: n, status\n"
                                 "  real, dimension(:,:), allocatable :: work\n"
                                 "  allocate(work(n, 2), stat=status)\n"
                                 "  if (status == 0) work(1, 2) = 3.0\n"
                                 "  deallocate(work, stat=status)\n"
                                 "end subroutine dynamic_workspace\n";
    F2cOptions options = {"allocation.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "allocatable workspace translates");
    expect_contains(result.code, "float *work = NULL", "ALLOCATABLE maps to an owned C pointer");
    expect_contains(result.code, "work_extent_1 = (int32_t)f2c_alloc_extent_1",
                    "ALLOCATE commits checked runtime extents");
    expect_contains(result.code,
                    "f2c_size_multiply(f2c_alloc_count, f2c_alloc_extent_1, &f2c_alloc_count)",
                    "ALLOCATE checks element-count multiplication for overflow");
    expect_contains(result.code, "free(work); work = NULL",
                    "DEALLOCATE releases and clears the pointer");
    f2c_result_free(&result);
}

static void test_allocatable_dummy_descriptor_abi(void) {
    static const char source[] = "subroutine resize(values, n)\n"
                                 "  integer, intent(in) :: n\n"
                                 "  real, allocatable, intent(inout) :: values(:)\n"
                                 "  if (allocated(values)) deallocate(values)\n"
                                 "  allocate(values(0:n-1))\n"
                                 "end subroutine resize\n"
                                 "program descriptor_caller\n"
                                 "  real, allocatable :: data(:)\n"
                                 "  integer :: n\n"
                                 "  n = 4\n"
                                 "  call resize(data, n)\n"
                                 "  deallocate(data)\n"
                                 "end program descriptor_caller\n";
    F2cOptions options = {"allocatable_dummy.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U,
           "ALLOCATABLE dummy arguments translate through a descriptor ABI");
    expect_contains(result.code,
                    "void resize(f2c_descriptor *f2c_descriptor_values, "
                    "const int32_t *n)",
                    "ALLOCATABLE dummy signature carries data, rank, bounds, and extents");
    expect_contains(result.code, "f2c_descriptor_values->data = values;",
                    "callee writes allocation state back to its caller descriptor");
    expect_contains(result.code, "f2c_descriptor f2c_call_descriptor_0 = {.data = data",
                    "caller materializes the descriptor from its live allocation state");
    expect_contains(result.code, "data = (float *)f2c_call_descriptor_0.data;",
                    "caller imports reallocation performed by an INTENT(INOUT) dummy");
    expect_contains(result.code, "(values != NULL)",
                    "ALLOCATED inquiry lowers against descriptor-backed allocation state");
    f2c_result_free(&result);
}

static void test_allocatable_function_result_descriptor(void) {
    static const char source[] = "function make_values(n) result(values)\n"
                                 "  integer, intent(in) :: n\n"
                                 "  real, allocatable :: values(:)\n"
                                 "  allocate(values(0:n-1))\n"
                                 "  values = 2.0\n"
                                 "end function make_values\n"
                                 "program result_caller\n"
                                 "  real make_values\n"
                                 "  real, allocatable :: values_out(:)\n"
                                 "  integer :: n\n"
                                 "  n = 4\n"
                                 "  values_out = make_values(n)\n"
                                 "  deallocate(values_out)\n"
                                 "end program result_caller\n";
    F2cOptions options = {"allocatable_result.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U,
           "ALLOCATABLE function results translate as owned descriptor values");
    expect_contains(result.code, "f2c_descriptor make_values(const int32_t *n)",
                    "ALLOCATABLE result ABI returns ownership and shape in one C value");
    expect_contains(result.code, "f2c_result_descriptor.data = f2c_result;",
                    "function commits its allocated result storage into the result descriptor");
    expect_contains(result.code, "f2c_descriptor f2c_function_result = make_values(&n);",
                    "caller evaluates an allocatable function result exactly once");
    expect_contains(result.code, "values_out = (float *)f2c_function_result.data;",
                    "allocatable assignment adopts the temporary result ownership");
    f2c_result_free(&result);
}

static void test_deferred_character_allocation(void) {
    static const char source[] = "subroutine deferred_character(n, status)\n"
                                 "  implicit none\n"
                                 "  integer :: n, status\n"
                                 "  character(:), allocatable :: text\n"
                                 "  character(:), allocatable :: values(:)\n"
                                 "  text = 'AB'\n"
                                 "  text = text // 'C'\n"
                                 "  allocate(character(len=n) :: values(0:2), stat=status)\n"
                                 "  values(0) = text\n"
                                 "  deallocate(values, stat=status)\n"
                                 "end subroutine deferred_character\n";
    static const char invalid_source[] = "subroutine invalid_allocation(n, extent, status_flag)\n"
                                         "  implicit none\n"
                                         "  integer :: n\n"
                                         "  real :: extent\n"
                                         "  logical :: status_flag\n"
                                         "  character(:), allocatable :: text\n"
                                         "  real, allocatable :: work(:)\n"
                                         "  allocate(text)\n"
                                         "  allocate(character(len=extent) :: text)\n"
                                         "  allocate(n)\n"
                                         "  allocate(work(1:n:2))\n"
                                         "  allocate(work(n), stat=status_flag)\n"
                                         "  allocate(work(n), source='bad')\n"
                                         "  deallocate(work(1))\n"
                                         "  allocate()\n"
                                         "end subroutine invalid_allocation\n";
    static const char invalid_declaration[] = "subroutine invalid_deferred_declaration()\n"
                                              "  implicit none\n"
                                              "  character(:) :: text\n"
                                              "end subroutine invalid_deferred_declaration\n";
    static const char saved_source[] = "subroutine saved_deferred()\n"
                                       "  implicit none\n"
                                       "  character(:), allocatable, save :: values(:)\n"
                                       "end subroutine saved_deferred\n";
    static const char ownership_source[] = "subroutine allocation_ownership()\n"
                                           "  implicit none\n"
                                           "  integer :: values(-2:2)\n"
                                           "  integer, allocatable :: copy(:)\n"
                                           "  character(len=2) :: labels(0:1)\n"
                                           "  character(:), allocatable :: deferred(:)\n"
                                           "  character(len=3), allocatable :: padded(:)\n"
                                           "  copy = values\n"
                                           "  deferred = labels\n"
                                           "  padded = labels\n"
                                           "  deallocate(copy, deferred, padded)\n"
                                           "  allocate(copy, source=values)\n"
                                           "  deallocate(copy)\n"
                                           "  allocate(copy, mold=values)\n"
                                           "  deallocate(copy)\n"
                                           "  allocate(deferred, source=labels)\n"
                                           "  deallocate(deferred)\n"
                                           "  allocate(deferred, mold=labels)\n"
                                           "end subroutine allocation_ownership\n";
    static const char invalid_models[] = "subroutine invalid_models(values)\n"
                                         "  implicit none\n"
                                         "  integer :: values(2)\n"
                                         "  integer, allocatable :: copy(:)\n"
                                         "  allocate(copy, source=values, source=values)\n"
                                         "  allocate(copy, mold=values, mold=values)\n"
                                         "  allocate(copy, source=values, mold=values)\n"
                                         "end subroutine invalid_models\n";
    static const char move_alloc_source[] =
        "subroutine transfer_ownership(status, message)\n"
        "  implicit none\n"
        "  integer, intent(out) :: status\n"
        "  character(len=16), intent(out) :: message\n"
        "  integer, allocatable :: source(:), destination(:)\n"
        "  character(:), allocatable :: text, moved_text\n"
        "  source = [1, 2, 3]\n"
        "  destination = [9]\n"
        "  call move_alloc(from=source, to=destination, stat=status, errmsg=message)\n"
        "  text = 'owned'\n"
        "  call move_alloc(text, moved_text)\n"
        "end subroutine transfer_ownership\n";
    static const char invalid_move_alloc[] =
        "subroutine invalid_transfers()\n"
        "  implicit none\n"
        "  integer :: plain, status\n"
        "  logical :: bad_status\n"
        "  integer, allocatable :: scalar, vector(:)\n"
        "  real, allocatable :: real_value\n"
        "  character(:), allocatable :: deferred_text\n"
        "  character(len=3), allocatable :: fixed_text\n"
        "  call move_alloc(plain, scalar)\n"
        "  call move_alloc(scalar, scalar)\n"
        "  call move_alloc(scalar, vector)\n"
        "  call move_alloc(scalar, real_value)\n"
        "  call move_alloc(scalar, real_value, stat=bad_status, errmsg=status)\n"
        "  call move_alloc(deferred_text, fixed_text)\n"
        "end subroutine invalid_transfers\n";
    static const char invalid_move_alloc_binding[] =
        "subroutine invalid_transfer_binding()\n"
        "  implicit none\n"
        "  integer, allocatable :: source, destination\n"
        "  integer :: status\n"
        "  call move_alloc(to=destination)\n"
        "  call move_alloc(source, to=destination, status)\n"
        "  call move_alloc(source, to=destination, from=source)\n"
        "  call move_alloc(source, target=destination)\n"
        "end subroutine invalid_transfer_binding\n";
    F2cOptions options = {"deferred_character.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U, "deferred-length CHARACTER allocation translates");
    expect_contains(result.code, "size_t f2c_char_len_text = 0U",
                    "deferred CHARACTER owns a runtime length descriptor");
    expect_contains(result.code, "char *f2c_character_result_0 = NULL",
                    "dynamic CHARACTER temporaries are not sized before runtime values exist");
    expect_contains(result.code, "f2c_character_concatenation_resize(",
                    "CHARACTER concatenation resizes its temporary at each evaluation");
    expect_contains(result.code, "values_lower_1 = (int32_t)f2c_alloc_lower_1",
                    "ALLOCATE preserves a non-default runtime lower bound");
    expect_contains(result.code, "f2c_char_len_values = f2c_alloc_char_len",
                    "typed ALLOCATE commits the deferred element length");
    expect_contains(result.code, "free(text);",
                    "local deferred CHARACTER storage is released on every unit exit");
    f2c_result_free(&result);

    result = f2c_transpile(invalid_source, sizeof(invalid_source) - 1U, &options);
    expect(result.error_count != 0U, "invalid allocation semantics are hard errors");
    expect_contains(result.diagnostics, "requires an explicit CHARACTER length",
                    "deferred CHARACTER allocation without a type specification is diagnosed");
    expect_contains(result.diagnostics, "length must be a scalar INTEGER",
                    "non-integer deferred CHARACTER length is diagnosed");
    expect_contains(result.diagnostics, "target 'n' is not ALLOCATABLE",
                    "allocation of a non-allocatable object is diagnosed");
    expect_contains(result.diagnostics, "cannot have a stride",
                    "strided allocation bounds are diagnosed");
    expect_contains(result.diagnostics, "STAT= in ALLOCATE must be a definable scalar INTEGER",
                    "invalid allocation status variables are diagnosed");
    expect_contains(result.diagnostics, "SOURCE= type is incompatible",
                    "type-incompatible allocation initialization is rejected explicitly");
    expect_contains(result.diagnostics, "must be a whole allocatable object",
                    "element deallocation is diagnosed");
    expect_contains(result.diagnostics, "ALLOCATE requires at least one target",
                    "empty allocation statements are diagnosed");
    f2c_result_free(&result);

    result = f2c_transpile(invalid_declaration, sizeof(invalid_declaration) - 1U, &options);
    expect(result.error_count != 0U,
           "deferred-length CHARACTER without ALLOCATABLE is a hard error");
    expect_contains(result.diagnostics, "must be ALLOCATABLE",
                    "invalid deferred CHARACTER ownership is diagnosed at declaration time");
    f2c_result_free(&result);

    result = f2c_transpile(saved_source, sizeof(saved_source) - 1U, &options);
    expect(result.error_count == 0U, "saved deferred CHARACTER descriptor translates");
    expect_contains(result.code, "static char *values = NULL",
                    "saved allocatable storage persists across calls");
    expect_contains(result.code, "static size_t f2c_char_len_values = 0U",
                    "saved deferred length persists across calls");
    expect_contains(result.code, "static int32_t values_lower_1 = 1",
                    "saved allocatable lower bounds persist across calls");
    expect_contains(result.code, "static int32_t values_extent_1 = 0",
                    "saved allocatable extents persist across calls");
    f2c_result_free(&result);

    result = f2c_transpile(ownership_source, sizeof(ownership_source) - 1U, &options);
    expect(result.error_count == 0U,
           "allocatable array assignment and SOURCE=/MOLD= allocation translate");
    expect_contains(result.code, "bool f2c_assign_reallocate = copy == NULL",
                    "numeric allocatable assignment performs shape-driven allocation");
    expect_contains(result.code, "f2c_assign_source_character_length",
                    "character allocatable assignment preserves source element length");
    expect_contains(result.code, "f2c_assign_copy_length",
                    "fixed-length character allocatable assignment pads or truncates elements");
    expect_contains(result.code, "f2c_alloc_storage[f2c_alloc_index]",
                    "ALLOCATE SOURCE= initializes every array element");
    expect_contains(result.code, "f2c_char_len_deferred = f2c_alloc_char_len",
                    "ALLOCATE SOURCE=/MOLD= commits a deferred character length descriptor");
    f2c_result_free(&result);

    result = f2c_transpile(invalid_models, sizeof(invalid_models) - 1U, &options);
    expect(result.error_count != 0U, "conflicting allocation model keywords are hard errors");
    expect_contains(result.diagnostics, "duplicate source= in ALLOCATE",
                    "duplicate SOURCE= is diagnosed");
    expect_contains(result.diagnostics, "duplicate mold= in ALLOCATE",
                    "duplicate MOLD= is diagnosed");
    expect_contains(result.diagnostics, "cannot specify both SOURCE= and MOLD=",
                    "combined SOURCE= and MOLD= is diagnosed");
    f2c_result_free(&result);

    result = f2c_transpile(move_alloc_source, sizeof(move_alloc_source) - 1U, &options);
    expect(result.error_count == 0U, "MOVE_ALLOC ownership transfers translate");
    expect_contains(result.code, "free(destination);",
                    "MOVE_ALLOC deallocates an allocated TO object before transfer");
    expect_contains(result.code, "destination = source;",
                    "MOVE_ALLOC transfers storage without copying elements");
    expect_contains(result.code, "source = NULL;",
                    "MOVE_ALLOC leaves FROM unallocated after transfer");
    expect_contains(result.code, "destination_lower_1 = source_lower_1;",
                    "MOVE_ALLOC transfers array bounds and extents");
    expect_contains(result.code, "f2c_char_len_moved_text = f2c_char_len_text;",
                    "MOVE_ALLOC transfers deferred CHARACTER length ownership");
    expect_contains(result.code, "(*status) = 0;",
                    "successful MOVE_ALLOC defines an optional STAT= result");
    f2c_result_free(&result);

    result = f2c_transpile(invalid_move_alloc, sizeof(invalid_move_alloc) - 1U, &options);
    expect(result.error_count != 0U, "invalid MOVE_ALLOC semantics are hard errors");
    expect_contains(result.diagnostics, "FROM= object 'plain' is not ALLOCATABLE",
                    "MOVE_ALLOC rejects nonallocatable ownership sources");
    expect_contains(result.diagnostics, "must designate distinct objects",
                    "MOVE_ALLOC rejects self-transfer");
    expect_contains(result.diagnostics, "FROM= rank 0 does not match TO= rank 1",
                    "MOVE_ALLOC rejects rank mismatch");
    expect_contains(result.diagnostics, "same declared type and kind",
                    "MOVE_ALLOC rejects declared-type mismatch");
    expect_contains(result.diagnostics, "STAT= must be a definable scalar INTEGER",
                    "MOVE_ALLOC validates STAT= type and definability");
    expect_contains(result.diagnostics, "ERRMSG= must be a definable scalar CHARACTER",
                    "MOVE_ALLOC validates ERRMSG= type and definability");
    expect_contains(result.diagnostics, "compatible length type parameters",
                    "MOVE_ALLOC validates CHARACTER length parameters");
    f2c_result_free(&result);

    result = f2c_transpile(invalid_move_alloc_binding, sizeof(invalid_move_alloc_binding) - 1U,
                           &options);
    expect(result.error_count != 0U, "invalid MOVE_ALLOC argument association is rejected");
    expect_contains(result.diagnostics, "required MOVE_ALLOC argument 'from'",
                    "MOVE_ALLOC requires FROM=");
    expect_contains(result.diagnostics, "positional argument follows a keyword argument",
                    "MOVE_ALLOC rejects positional arguments after keywords");
    expect_contains(result.diagnostics, "argument 'from' is associated more than once",
                    "MOVE_ALLOC rejects duplicate associations");
    expect_contains(result.diagnostics, "has no argument named 'target'",
                    "MOVE_ALLOC rejects unknown keywords");
    f2c_result_free(&result);
}

static void test_common_block_storage(void) {
    static const char source[] = "subroutine set_error(value)\n"
                                 "  integer :: value, infot, nout\n"
                                 "  logical :: ok, lerr\n"
                                 "  common /infoc/ infot, nout, ok, lerr\n"
                                 "  infot = value\n"
                                 "end subroutine set_error\n"
                                 "subroutine clear_error()\n"
                                 "  integer :: infot, nout\n"
                                 "  logical :: ok, lerr\n"
                                 "  common /infoc/ infot, nout, ok, lerr\n"
                                 "  infot = 0\n"
                                 "end subroutine clear_error\n";
    F2cOptions options = {"common.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "named COMMON block translates");
    expect_contains(result.code, "struct f2c_common_infoc",
                    "COMMON block emits shared namespaced storage");
    expect_contains(result.code, "f2c_common_infoc.field_0 = (*value)",
                    "COMMON members map by storage position");
    f2c_result_free(&result);
}

static void test_project_procedure_registry(void) {
    static const char caller[] = "subroutine invoke(value)\n"
                                 "  implicit none\n"
                                 "  double precision :: value, resolved\n"
                                 "  external resolved\n"
                                 "  value = resolved(value)\n"
                                 "end subroutine invoke\n";
    static const char definition[] = "double precision function resolved(value)\n"
                                     "  implicit none\n"
                                     "  double precision, intent(in) :: value\n"
                                     "  resolved = value\n"
                                     "end function resolved\n";
    F2cInput inputs[2] = {
        {caller, sizeof(caller) - 1U, {"caller.f90", F2C_SOURCE_FREE, 0}},
        {definition, sizeof(definition) - 1U, {"definition.f90", F2C_SOURCE_FREE, 0}}};
    F2cResult result = f2c_transpile_project(inputs, 2U);
    expect(result.error_count == 0U, "project inputs share a resolved procedure registry");
    expect_contains(result.code, "double resolved(const double *value);",
                    "project registry emits the definition's exact interface before callers");
    expect_contains(result.code, "= resolved(value);",
                    "cross-file function calls retain the resolved return type");
    expect_contains(result.header, "#ifndef F2C_GENERATED_INTERFACE_",
                    "project translation emits a guarded shared interface header");
    expect_contains(result.header, "void invoke(double *value);",
                    "shared interface header declares project subroutines");
    expect_contains(result.header, "double resolved(const double *value);",
                    "shared interface header preserves resolved const-qualified signatures");
    f2c_result_free(&result);

    {
        static const char incompatible[] = "subroutine incompatible(value)\n"
                                           "  implicit none\n"
                                           "  double precision :: value\n"
                                           "  real :: resolved\n"
                                           "  external resolved\n"
                                           "  value = resolved(value)\n"
                                           "end subroutine incompatible\n";
        F2cInput invalid[2] = {
            {incompatible, sizeof(incompatible) - 1U, {"incompatible.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"definition.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult mismatch = f2c_transpile_project(invalid, 2U);
        expect(mismatch.error_count != 0U,
               "project registry rejects conflicting cross-file return types");
        expect_contains(mismatch.diagnostics, "incompatible.f90:",
                        "cross-file interface diagnostics identify the caller source");
        expect_contains(mismatch.diagnostics, "has return type",
                        "cross-file interface diagnostics explain the type conflict");
        f2c_result_free(&mismatch);
    }
    {
        static const char wrong_arity[] = "subroutine wrong_arity(value)\n"
                                          "  implicit none\n"
                                          "  double precision :: value, resolved\n"
                                          "  external resolved\n"
                                          "  value = resolved(value, value)\n"
                                          "end subroutine wrong_arity\n";
        F2cInput invalid[2] = {
            {wrong_arity, sizeof(wrong_arity) - 1U, {"wrong_arity.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"definition.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult mismatch = f2c_transpile_project(invalid, 2U);
        expect(mismatch.error_count != 0U,
               "project registry rejects conflicting cross-file argument counts");
        expect_contains(mismatch.diagnostics, "called with 2 arguments",
                        "cross-file arity diagnostics report both observed interfaces");
        f2c_result_free(&mismatch);
    }
}

static void test_project_interface_semantics(void) {
    {
        static const char caller[] = "subroutine call_update(values, count)\n"
                                     "  real :: values(count)\n"
                                     "  integer :: count\n"
                                     "  call update(values, count)\n"
                                     "end subroutine call_update\n";
        static const char definition[] = "subroutine update(values, count)\n"
                                         "  real, intent ( inout ) :: values(count)\n"
                                         "  integer, intent ( in ) :: count\n"
                                         "end subroutine update\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"call_update.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"update.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count == 0U,
               "project interface validation accepts matching type, rank, and spaced INTENT");
        expect_contains(result.header, "void update(float *values, const int32_t *count);",
                        "resolved project interface preserves rank and INTENT without imposing "
                        "optimizer-only alias contracts on callers");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_vector(value)\n"
                                     "  real :: value\n"
                                     "  call consume(value)\n"
                                     "end subroutine call_vector\n";
        static const char definition[] = "subroutine consume(values)\n"
                                         "  real, intent(in) :: values(*)\n"
                                         "end subroutine consume\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"rank_mismatch.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"consume.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "project interface validation rejects a scalar actual for an array dummy");
        expect_contains(result.diagnostics, "rank_mismatch.f90:3:14:",
                        "rank mismatch diagnostic identifies the exact actual argument");
        expect_contains(result.diagnostics, "has rank 0 but dummy 'values' has rank 1",
                        "rank mismatch diagnostic reports both ranks and the dummy name");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_typed(value)\n"
                                     "  real :: value\n"
                                     "  call consume_double(value)\n"
                                     "end subroutine call_typed\n";
        static const char definition[] = "subroutine consume_double(value)\n"
                                         "  double precision, intent(in) :: value\n"
                                         "end subroutine consume_double\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"type_mismatch.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"consume_double.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "project interface validation rejects an incompatible actual type");
        expect_contains(result.diagnostics,
                        "has type REAL but dummy 'value' has type DOUBLE PRECISION",
                        "type mismatch diagnostic uses Fortran type names");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_store(value)\n"
                                     "  real :: value\n"
                                     "  call store(value + 1.0)\n"
                                     "end subroutine call_store\n";
        static const char definition[] = "subroutine store(value)\n"
                                         "  real, intent(out) :: value\n"
                                         "end subroutine store\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"intent_out.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"store.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "INTENT(OUT) validation rejects a non-definable expression actual");
        expect_contains(result.diagnostics, "is not definable but dummy 'value' has INTENT(OUT)",
                        "INTENT(OUT) diagnostic explains the definability requirement");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_inspect(value)\n"
                                     "  real :: value\n"
                                     "  call inspect(value + 1.0)\n"
                                     "end subroutine call_inspect\n";
        static const char definition[] = "subroutine inspect(value)\n"
                                         "  real, intent(in) :: value\n"
                                         "end subroutine inspect\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"intent_in.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"inspect.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count == 0U,
               "INTENT(IN) validation accepts a non-definable expression actual");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_update(values, order)\n"
                                     "  real :: values(4)\n"
                                     "  integer :: order(2)\n"
                                     "  call update(values(order))\n"
                                     "end subroutine call_update\n";
        static const char definition[] = "subroutine update(values)\n"
                                         "  real, intent(inout) :: values(:)\n"
                                         "end subroutine update\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"vector_intent.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"update.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "INTENT(INOUT) validation rejects a vector-subscript actual");
        expect_contains(result.diagnostics,
                        "uses a vector subscript but dummy 'values' has INTENT(INOUT)",
                        "vector-subscript diagnostic explains the forbidden definition context");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_sequence(values)\n"
                                     "  real :: values(3)\n"
                                     "  call consume(values(2))\n"
                                     "end subroutine call_sequence\n";
        static const char definition[] = "subroutine consume(values)\n"
                                         "  real, intent(in) :: values(*)\n"
                                         "end subroutine consume\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"sequence.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"consume.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count == 0U,
               "project interface validation preserves legacy array-element sequence association");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_matrix_storage(values)\n"
                                     "  real :: values(*)\n"
                                     "  call matrix_input(values)\n"
                                     "end subroutine call_matrix_storage\n";
        static const char definition[] = "subroutine matrix_input(values)\n"
                                         "  real, intent(in) :: values(2, *)\n"
                                         "end subroutine matrix_input\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"rank_sequence.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"matrix_input.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count == 0U,
               "project interface validation accepts whole-array storage sequence association");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_apply(value)\n"
                                     "  real :: value\n"
                                     "  call apply(value, value)\n"
                                     "end subroutine call_apply\n";
        static const char definition[] = "subroutine apply(callback, value)\n"
                                         "  real :: callback, value\n"
                                         "  external callback\n"
                                         "  value = callback(value)\n"
                                         "end subroutine apply\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"procedure_actual.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"apply.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "project interface validation rejects data where a procedure actual is required");
        expect_contains(result.diagnostics, "argument 1 of procedure 'apply' must be a procedure",
                        "procedure-actual diagnostic identifies the interface slot");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_keywords(value)\n"
                                     "  double precision :: value, combine\n"
                                     "  external combine, scale\n"
                                     "  value = combine(value, third=5.0d0, second=4.0d0)\n"
                                     "  call scale(factor=2.0d0, value=value)\n"
                                     "end subroutine call_keywords\n";
        static const char definition[] = "subroutine scale(value, factor)\n"
                                         "  double precision, intent(inout) :: value\n"
                                         "  double precision, intent(in) :: factor\n"
                                         "  value = value * factor\n"
                                         "end subroutine scale\n"
                                         "double precision function combine(first, second, third)\n"
                                         "  double precision, intent(in) :: first, second, third\n"
                                         "  combine = first + second + third\n"
                                         "end function combine\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"keywords.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"keyword_definitions.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        const char *combine_call;
        const char *scale_call;
        expect(result.error_count == 0U,
               "project interface validation accepts positional arguments followed by keywords");
        combine_call = result.code != NULL ? strstr(result.code, "combine(") : NULL;
        scale_call = result.code != NULL ? strstr(result.code, "scale(") : NULL;
        expect(combine_call != NULL && strstr(combine_call, "value") != NULL &&
                   strstr(combine_call, "4.0") != NULL && strstr(combine_call, "5.0") != NULL,
               "function keyword arguments lower through their dummy-argument order");
        expect(scale_call != NULL && strstr(scale_call, "value") != NULL &&
                   strstr(scale_call, "2.0") != NULL,
               "CALL keyword arguments lower through their dummy-argument order");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_unknown(value)\n"
                                     "  real :: value\n"
                                     "  call consume(missing=value)\n"
                                     "end subroutine call_unknown\n";
        static const char definition[] = "subroutine consume(value)\n"
                                         "  real, intent(in) :: value\n"
                                         "end subroutine consume\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"unknown_keyword.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"consume.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U, "project interface rejects an unknown keyword");
        expect_contains(result.diagnostics, "has no dummy argument named 'missing'",
                        "unknown-keyword diagnostic names the invalid keyword");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_duplicate(value)\n"
                                     "  real :: value\n"
                                     "  call pair(first=value, first=value)\n"
                                     "end subroutine call_duplicate\n";
        static const char definition[] = "subroutine pair(first, second)\n"
                                         "  real, intent(in) :: first, second\n"
                                         "end subroutine pair\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"duplicate_keyword.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"pair.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "project interface rejects duplicate dummy-argument association");
        expect_contains(result.diagnostics, "dummy argument 'first' is associated more than once",
                        "duplicate-keyword diagnostic names the associated dummy");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_positional_after_keyword(value)\n"
                                     "  real :: value\n"
                                     "  call pair(second=value, value)\n"
                                     "end subroutine call_positional_after_keyword\n";
        static const char definition[] = "subroutine pair(first, second)\n"
                                         "  real, intent(in) :: first, second\n"
                                         "end subroutine pair\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"keyword_order.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"pair.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "project interface rejects a positional argument after a keyword argument");
        expect_contains(result.diagnostics, "positional argument follows a keyword argument",
                        "argument-order diagnostic explains the Fortran association rule");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine call_unresolved(value)\n"
                                     "  real :: value\n"
                                     "  external unresolved\n"
                                     "  call unresolved(value=value)\n"
                                     "end subroutine call_unresolved\n";
        F2cOptions options = {"unresolved_keyword.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "keyword arguments require a project-visible explicit interface");
        expect_contains(result.diagnostics, "require a visible explicit interface",
                        "unresolved keyword-call diagnostic explains the missing interface");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_missing(value)\n"
                                     "  integer :: value\n"
                                     "  call optional_target(extra=value)\n"
                                     "end subroutine call_missing\n";
        static const char definition[] = "subroutine optional_target(required, extra)\n"
                                         "  integer, intent(in) :: required\n"
                                         "  integer, intent(in), optional :: extra\n"
                                         "end subroutine optional_target\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"missing_required.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"optional_target.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "OPTIONAL association rejects omission of a required dummy argument");
        expect_contains(result.diagnostics,
                        "required dummy argument 'required' of procedure 'optional_target'",
                        "missing-required diagnostic names the unassociated dummy");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine call_empty_slot(value)\n"
                                     "  integer :: value\n"
                                     "  call optional_target(value, , value)\n"
                                     "end subroutine call_empty_slot\n";
        static const char definition[] = "subroutine optional_target(first, middle, last)\n"
                                         "  integer, intent(in) :: first, last\n"
                                         "  integer, intent(in), optional :: middle\n"
                                         "end subroutine optional_target\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"empty_actual.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"optional_target.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "OPTIONAL association rejects non-Fortran empty positional slots");
        expect_contains(result.diagnostics, "cannot be omitted with an empty positional slot",
                        "empty-slot diagnostic directs callers to keyword association");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine invalid_present(value)\n"
                                     "  integer, intent(in) :: value\n"
                                     "  if (present(value)) return\n"
                                     "end subroutine invalid_present\n";
        F2cOptions options = {"invalid_present.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U, "PRESENT rejects a non-OPTIONAL dummy argument");
        expect_contains(result.diagnostics, "PRESENT argument must be an OPTIONAL dummy argument",
                        "PRESENT diagnostic states the dummy-argument requirement");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine invalid_present_expression(value)\n"
                                     "  integer, intent(in), optional :: value\n"
                                     "  if (present(value + 1)) return\n"
                                     "end subroutine invalid_present_expression\n";
        F2cOptions options = {"present_expression.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U, "PRESENT rejects a general expression argument");
        expect_contains(result.diagnostics, "PRESENT argument must be an OPTIONAL dummy argument",
                        "PRESENT expression diagnostic is a hard semantic error");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine invalid_optional_local()\n"
                                     "  integer, optional :: local\n"
                                     "end subroutine invalid_optional_local\n";
        F2cOptions options = {"optional_local.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U, "OPTIONAL rejects a non-dummy local entity");
        expect_contains(result.diagnostics, "OPTIONAL entity 'local' is not a dummy argument",
                        "OPTIONAL declaration diagnostic names the invalid entity");
        f2c_result_free(&result);
    }
}

static void test_explicit_interface_semantics(void) {
    {
        static const char source[] =
            "subroutine invoke_explicit(value)\n"
            "  implicit none(type, external)\n"
            "  real :: value\n"
            "  interface\n"
            "    subroutine update(value, increment)\n"
            "      implicit none\n"
            "      real, intent(inout) :: value\n"
            "      real, intent(in), optional :: increment\n"
            "    end subroutine update\n"
            "  end interface\n"
            "  interface evaluate\n"
            "    real function evaluate_impl(value, offset) result(answer)\n"
            "      implicit none\n"
            "      real, intent(in) :: value\n"
            "      real, intent(in), optional :: offset\n"
            "    end function evaluate_impl\n"
            "  end interface evaluate\n"
            "  interface choose\n"
            "    real function choose_integer(item)\n"
            "      integer, intent(in) :: item\n"
            "    end function choose_integer\n"
            "    real function choose_real(item)\n"
            "      real, intent(in) :: item\n"
            "    end function choose_real\n"
            "  end interface choose\n"
            "  interface dispatch\n"
            "    subroutine dispatch_integer(item)\n"
            "      integer, intent(in) :: item\n"
            "    end subroutine dispatch_integer\n"
            "    subroutine dispatch_real(item)\n"
            "      real, intent(in) :: item\n"
            "    end subroutine dispatch_real\n"
            "  end interface dispatch\n"
            "  call update(value=value)\n"
            "  call dispatch(1)\n"
            "  call dispatch(value)\n"
            "  value = evaluate(value=value) + choose(1) + choose(value)\n"
            "end subroutine invoke_explicit\n";
        F2cOptions options = {"explicit_interface.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count == 0U,
               "explicit interfaces satisfy IMPLICIT NONE(EXTERNAL) and bind keyword actuals");
        expect_contains(result.code, "extern void update(float *, const float *);",
                        "explicit subroutine interface emits its exact C17 prototype");
        expect_contains(result.code, "extern float evaluate_impl(const float *, const float *);",
                        "single-specific generic interface emits the specific symbol prototype");
        expect_contains(result.code, "update(value, NULL);",
                        "explicit CALL binding inserts a null optional actual");
        expect_contains(result.code, "evaluate_impl(value, NULL)",
                        "generic function binding resolves the specific symbol and omission");
        expect_contains(result.code, "choose_integer(&(int32_t){1})",
                        "generic resolution selects the INTEGER specific procedure");
        expect_contains(result.code, "choose_real(value)",
                        "generic resolution selects the REAL specific procedure");
        expect_contains(result.code, "dispatch_integer(&(int32_t){1});",
                        "generic CALL resolution selects the INTEGER specific subroutine");
        expect_contains(result.code, "dispatch_real(value);",
                        "generic CALL resolution selects the REAL specific subroutine");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine explicit_type_mismatch(value)\n"
                                     "  integer :: value\n"
                                     "  interface\n"
                                     "    subroutine consume(item)\n"
                                     "      real, intent(in) :: item\n"
                                     "    end subroutine consume\n"
                                     "  end interface\n"
                                     "  call consume(value)\n"
                                     "end subroutine explicit_type_mismatch\n";
        F2cOptions options = {"explicit_type_mismatch.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "explicit interface validation rejects an incompatible actual type");
        expect_contains(result.diagnostics, "has type INTEGER but dummy 'item' has type REAL",
                        "explicit-interface type diagnostic names both Fortran types");
        f2c_result_free(&result);
    }
    {
        static const char caller[] = "subroutine interface_contract(value)\n"
                                     "  real :: value\n"
                                     "  interface\n"
                                     "    subroutine consume(value)\n"
                                     "      real, intent(in) :: value\n"
                                     "    end subroutine consume\n"
                                     "  end interface\n"
                                     "  call consume(value)\n"
                                     "end subroutine interface_contract\n";
        static const char definition[] = "subroutine consume(value)\n"
                                         "  double precision, intent(in) :: value\n"
                                         "end subroutine consume\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"interface_contract.f90", F2C_SOURCE_FREE, 0}},
            {definition, sizeof(definition) - 1U, {"consume.f90", F2C_SOURCE_FREE, 0}}};
        F2cResult result = f2c_transpile_project(inputs, 2U);
        expect(result.error_count != 0U,
               "project definitions must agree with caller-visible explicit interfaces");
        expect_contains(result.diagnostics,
                        "dummy argument 1 of explicit interface 'consume' is incompatible",
                        "interface/definition conflict is rejected before code generation");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine ambiguous_generic(value)\n"
                                     "  real :: value\n"
                                     "  interface choose\n"
                                     "    real function choose_first(item)\n"
                                     "      real :: item\n"
                                     "    end function choose_first\n"
                                     "    real function choose_second(item)\n"
                                     "      real :: item\n"
                                     "    end function choose_second\n"
                                     "  end interface choose\n"
                                     "  value = choose(value)\n"
                                     "end subroutine ambiguous_generic\n";
        F2cOptions options = {"ambiguous_generic.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U, "ambiguous generic calls fail instead of miscompiling");
        expect_contains(result.diagnostics, "generic interface 'choose' is ambiguous",
                        "generic-overload diagnostic states the ambiguous call");
        f2c_result_free(&result);
    }
}

static void test_abstract_procedure_semantics(void) {
    {
        static const char source[] = "subroutine invoke_procedure(operation, value)\n"
                                     "  implicit none(type, external)\n"
                                     "  abstract interface\n"
                                     "    integer function integer_operation(item) result(answer)\n"
                                     "      integer, intent(in) :: item\n"
                                     "    end function integer_operation\n"
                                     "  end interface\n"
                                     "  procedure(integer_operation) :: operation\n"
                                     "  integer, intent(inout) :: value\n"
                                     "  value = operation(value)\n"
                                     "end subroutine invoke_procedure\n";
        F2cOptions options = {"abstract_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count == 0U,
               "ABSTRACT INTERFACE and PROCEDURE satisfy IMPLICIT NONE(EXTERNAL)");
        expect_contains(result.code, "int32_t (*operation)(const int32_t *)",
                        "PROCEDURE dummy lowers to its exact named C17 function-pointer type");
        expect(result.code == NULL ||
                   strstr(result.code, "extern int32_t integer_operation") == NULL,
               "an abstract interface template never emits a callable external prototype");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine unknown_procedure(operation)\n"
                                     "  procedure(missing_interface) :: operation\n"
                                     "end subroutine unknown_procedure\n";
        F2cOptions options = {"unknown_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U, "PROCEDURE rejects an interface name that is not visible");
        expect_contains(result.diagnostics,
                        "PROCEDURE interface 'missing_interface' is not a visible specific or "
                        "abstract interface",
                        "unknown PROCEDURE interface diagnostic names the missing contract");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine generic_procedure(operation)\n"
                                     "  interface operation_group\n"
                                     "    subroutine operation_impl()\n"
                                     "    end subroutine operation_impl\n"
                                     "  end interface operation_group\n"
                                     "  procedure(operation_group) :: operation\n"
                                     "end subroutine generic_procedure\n";
        F2cOptions options = {"generic_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "PROCEDURE rejects a generic name in place of a specific interface name");
        expect_contains(result.diagnostics, "is not a visible specific or abstract interface",
                        "generic PROCEDURE-name diagnostic explains the required interface kind");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine pointer_procedure(operation)\n"
                                     "  abstract interface\n"
                                     "    subroutine operation_interface()\n"
                                     "    end subroutine operation_interface\n"
                                     "  end interface\n"
                                     "  procedure(operation_interface), pointer :: operation\n"
                                     "end subroutine pointer_procedure\n";
        F2cOptions options = {"pointer_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count == 0U,
               "a procedure-pointer dummy with an explicit interface is accepted");
        expect_contains(result.code, "void (*operation)(void)",
                        "procedure-pointer dummy lowers to its exact C17 function-pointer type");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine optional_local_procedure()\n"
                                     "  abstract interface\n"
                                     "    subroutine operation_interface()\n"
                                     "    end subroutine operation_interface\n"
                                     "  end interface\n"
                                     "  procedure(operation_interface), optional :: operation\n"
                                     "end subroutine optional_local_procedure\n";
        F2cOptions options = {"optional_local_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "OPTIONAL PROCEDURE rejects a non-dummy local procedure entity");
        expect_contains(result.diagnostics, "OPTIONAL entity 'operation' is not a dummy argument",
                        "optional-procedure diagnostic names the invalid local entity");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine intended_procedure(operation)\n"
                                     "  abstract interface\n"
                                     "    subroutine operation_interface()\n"
                                     "    end subroutine operation_interface\n"
                                     "  end interface\n"
                                     "  procedure(operation_interface), intent(in) :: operation\n"
                                     "end subroutine intended_procedure\n";
        F2cOptions options = {"intended_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "non-pointer PROCEDURE rejects the standard-incompatible INTENT attribute");
        expect_contains(result.diagnostics, "INTENT on a PROCEDURE entity requires the POINTER",
                        "PROCEDURE INTENT diagnostic identifies the missing pointer semantics");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine malformed_procedure(operation)\n"
                                     "  abstract interface\n"
                                     "    subroutine operation_interface()\n"
                                     "    end subroutine operation_interface\n"
                                     "  end interface\n"
                                     "  procedure(operation_interface), volatile :: operation\n"
                                     "end subroutine malformed_procedure\n";
        F2cOptions options = {"malformed_procedure.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "PROCEDURE rejects attributes outside the implemented contract");
        expect_contains(result.diagnostics,
                        "unsupported or malformed PROCEDURE attribute 'volatile'",
                        "unknown PROCEDURE attributes are never silently ignored");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine recursive_interface_mismatch()\n"
                                     "  abstract interface\n"
                                     "    integer function integer_operation(item) result(answer)\n"
                                     "      integer, intent(in) :: item\n"
                                     "    end function integer_operation\n"
                                     "    real function real_operation(item) result(answer)\n"
                                     "      real, intent(in) :: item\n"
                                     "    end function real_operation\n"
                                     "    subroutine integer_executor(operation)\n"
                                     "      import :: integer_operation\n"
                                     "      procedure(integer_operation) :: operation\n"
                                     "    end subroutine integer_executor\n"
                                     "  end interface\n"
                                     "  call accept(wrong_executor)\n"
                                     "contains\n"
                                     "  subroutine accept(executor)\n"
                                     "    procedure(integer_executor) :: executor\n"
                                     "  end subroutine accept\n"
                                     "  subroutine wrong_executor(operation)\n"
                                     "    procedure(real_operation) :: operation\n"
                                     "  end subroutine wrong_executor\n"
                                     "end subroutine recursive_interface_mismatch\n";
        F2cOptions options = {"recursive_interface_mismatch.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
        expect(result.error_count != 0U,
               "procedure actual validation recursively checks nested procedure dummies");
        expect_contains(result.diagnostics, "has an incompatible explicit interface",
                        "recursive procedure-signature diagnostic rejects the nested mismatch");
        f2c_result_free(&result);
    }
}

static void test_lapack_driver_data_semantics(void) {
    static const char source[] = "program driver_data\n"
                                 "  integer, parameter :: n = 3\n"
                                 "  double precision :: values(n)\n"
                                 "  character*8 :: srnamt\n"
                                 "  common /srnamc/ srnamt\n"
                                 "  save /srnamc/\n"
                                 "  values = (/ -1.0d0, 0.0d0, 1.0d0 /)\n"
                                 "  srnamt = 'DGESV'\n"
                                 "  if (min(values(1), values(2)) /= -1.0d0) stop\n"
                                 "end program driver_data\n";
    F2cOptions options = {"driver_data.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "LAPACK driver storage and DATA semantics translate");
    expect_contains(result.code, "double *f2c_constructor_values",
                    "F90 array constructors use typed, shape-checked temporary storage");
    expect_contains(result.code, "memset(f2c_common_srnamc.field_0",
                    "character COMMON assignment uses shared storage");
    expect_contains(result.code, "F2C_FORTRAN_MIN",
                    "Fortran MIN uses NaN-propagating numeric semantics");
    expect(result.code == NULL || strstr(result.code, "/ srnamc /") == NULL,
           "SAVE /COMMON/ does not create a bogus local declaration");
    f2c_result_free(&result);
}

static void test_structured_data_initializers(void) {
    static const char source[] = "program structured_data\n"
                                 "  integer :: table(5), matrix(2,2), j\n"
                                 "  complex :: values(2)\n"
                                 "  data table / 3*1, 2*2 /\n"
                                 "  data (matrix(1,j), j=1,2) / 4, 5 /\n"
                                 "  data values / (1.0,2.0), (3.0,4.0) /\n"
                                 "end program structured_data\n";
    F2cOptions options = {"structured_data.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "DATA repetition factors, implied DO, and complex values translate");
    expect_contains(result.code, "table[0] = 1;",
                    "DATA repetition starts at the first array element");
    expect_contains(result.code, "table[2] = 1;",
                    "DATA repetition expands the requested number of values");
    expect_contains(result.code, "table[3] = 2;", "DATA advances to the next repeated value");
    expect_contains(result.code, "table[4] = 2;", "DATA fills the complete repeated initializer");
    expect_contains(result.code, "matrix[", "DATA implied DO emits explicit array targets");
    expect_contains(result.code, "= 4;", "DATA implied DO consumes its first value");
    expect_contains(result.code, "= 5;", "DATA implied DO consumes its second value");
    expect_contains(result.code, "values[0] =",
                    "complex DATA lists beginning with a parenthesis remain separate values");
    expect_contains(result.code,
                    "values[1] =", "complex DATA lists preserve every complex literal");
    f2c_result_free(&result);
}

static void test_internal_procedure_and_file_units(void) {
    static const char source[] = "program internal_io\n"
                                 "  integer :: i, j, status\n"
                                 "  real :: values(2,2), sample\n"
                                 "  data ((values(i,j),i=1,2),j=1,2) / 1.0, 2.0, 3.0, 4.0 /\n"
                                 "  open(10, file='internal-io.out', status='unknown', "
                                 "iostat=status, err=90)\n"
                                 "  rewind(10, iostat=status, err=90)\n"
                                 "  close(10, iostat=status, err=90)\n"
                                 "  call random_number(sample)\n"
                                 "  if (scale_value(values(2,2), 2) /= 8.0) error stop\n"
                                 "  stop\n"
                                 "90 error stop\n"
                                 "contains\n"
                                 "  real function scale_value(value, factor)\n"
                                 "    real :: value\n"
                                 "    integer :: factor\n"
                                 "    scale_value = value * factor\n"
                                 "  end function scale_value\n"
                                 "end program internal_io\n";
    F2cOptions options = {"internal_io.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "internal procedures, nested DATA implied-DO, and file units translate");
    expect_contains(result.code, "internal_io__scale_value",
                    "internal procedure names are scoped by their host unit");
    expect_contains(result.code, "] = 4.0f;",
                    "nested DATA implied-DO expands in Fortran element order");
    expect_contains(result.code, "f2c_open_unit",
                    "OPEN uses the generated libc-backed file-unit table");
    expect_contains(result.code, "f2c_rewind_unit", "REWIND targets the opened Fortran unit");
    expect_contains(result.code, "f2c_close_unit", "CLOSE releases the opened Fortran unit");
    expect_contains(result.code, "status = f2c_io_ok ? 0 : 1",
                    "OPEN/REWIND/CLOSE define IOSTAT without an auxiliary runtime");
    expect_contains(result.code, "if (!f2c_io_ok) goto f2c_label_90",
                    "OPEN/REWIND/CLOSE preserve ERR= control flow");
    expect_contains(result.code, "F2C_RANDOM_NUMBER",
                    "RANDOM_NUMBER uses the self-contained generated PRNG");
    expect_contains(result.code, "(float)(f2c_random_bits() >> 40) * 0x1p-24f",
                    "single-precision RANDOM_NUMBER converts integer bits explicitly");
    expect_contains(result.code, "return EXIT_SUCCESS", "plain STOP terminates successfully");
    f2c_result_free(&result);
}

static void test_dynamic_array_sections(void) {
    static const char source[] = "subroutine section_ops(n, work, matrix)\n"
                                 "  implicit none\n"
                                 "  integer, intent(in) :: n\n"
                                 "  real, intent(inout) :: work(*), matrix(n,n)\n"
                                 "  work(2:2*n:2) = 0.0\n"
                                 "  matrix(1:n,2) = matrix(1:n,1) + matrix(1:n,2)\n"
                                 "  matrix(1:n,1) = matrix(1,1) * matrix(1,1:n)\n"
                                 "end subroutine section_ops\n";
    static const char rank3_source[] = "subroutine reverse_cube(cube, n)\n"
                                       "  integer, intent(in) :: n\n"
                                       "  real :: cube(n,n,n)\n"
                                       "  cube(1:n,1:n,1:n) = cube(n:1:-1,n:1:-1,n:1:-1)\n"
                                       "end subroutine reverse_cube\n";
    F2cOptions options = {"sections.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "dynamic array-section assignments translate");
    expect_contains(result.code, "for (int32_t f2c_section_0 = 0;",
                    "array sections lower to explicit shape-driven loops");
    expect_contains(result.code, "f2c_section_0 * (2)",
                    "array-section lowering preserves non-unit strides");
    expect_contains(result.code, "+ f2c_section_0 * (1)",
                    "conformable RHS sections advance with the LHS element ordinal");
    expect_contains(result.code, "malloc(f2c_section_count * sizeof(*f2c_section_values))",
                    "overlap-safe array sections use a portable heap buffer instead of a VLA");
    expect_contains(result.code, "f2c_section_values[f2c_section_linear++]",
                    "array-section right-hand sides are materialized before overlapping writes");
    expect_contains(result.code, "free(f2c_section_values)",
                    "array-section temporary storage has an explicit lifetime");
    expect(result.code == NULL || strstr(result.code, "work[(((int32_t)(2)) - (1))] =") == NULL,
           "array-section scalar broadcast never writes only the first element");
    f2c_result_free(&result);
    {
        F2cResult rank3 = f2c_transpile(rank3_source, sizeof(rank3_source) - 1U, &options);
        expect(rank3.error_count == 0U,
               "rank-three negative-stride section assignment translates successfully");
        expect_contains(rank3.code, "int32_t f2c_extent_2",
                        "rank-three section lowering emits all shape dimensions");
        expect_contains(rank3.code, "f2c_section_2) * ((-1))",
                        "rank-three section lowering preserves negative strides");
        f2c_result_free(&rank3);
    }
}

static void test_list_directed_io(void) {
    static const char source[] = "program io_driver\n"
                                 "  integer :: n, i\n"
                                 "  real :: values(3)\n"
                                 "  read(*,*) n\n"
                                 "  read(*,*) (values(i), i=1,n)\n"
                                 "  write(*,*) 'values', (values(i), i=1,n)\n"
                                 "  print *, n\n"
                                 "end program io_driver\n";
    F2cOptions options = {"io.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "list-directed READ/WRITE/PRINT translate");
    expect_contains(result.code, "F2C_READ(stdin, &n)", "READ maps to typed libc input");
    expect_contains(result.code, "f2c_finish_read(stdin)",
                    "list-directed READ consumes the complete Fortran record");
    expect_contains(result.code, "f2c_write_character(stdout, \"values\"",
                    "WRITE preserves length-aware character output items");
    expect_contains(result.code, "for (i = 1;", "I/O implied-DO maps to a C loop");
    f2c_result_free(&result);

    {
        static const char invalid_source[] = "program invalid_io\n"
                                             "  implicit none\n"
                                             "  integer :: unit, status, iterator\n"
                                             "  logical :: bad_status\n"
                                             "  real :: value\n"
                                             "  read(unit, unit=unit, fmt=*) value\n"
                                             "  read(unit=unit) value\n"
                                             "  read(unit=unit, fmt=*, iostat=bad_status) 1.0\n"
                                             "  read(unit, fmt=*, unit) value\n"
                                             "  read(unit, fmt=*, mystery=1) value\n"
                                             "  read(*,*) (value, iterator=1,3,0)\n"
                                             "  write(unit, fmt=*, end=10) value\n"
                                             "  open(unit='bad', file=unit, status=1)\n"
                                             "10 continue\n"
                                             "end program invalid_io\n";
        F2cResult invalid = f2c_transpile(invalid_source, sizeof(invalid_source) - 1U, &options);
        expect(invalid.error_count != 0U, "invalid typed I/O semantics are hard errors");
        expect_contains(invalid.diagnostics, "duplicate UNIT= control in READ",
                        "I/O validation detects positional/keyword duplicate controls");
        expect_contains(invalid.diagnostics, "READ IOSTAT= must be a definable scalar INTEGER",
                        "I/O validation enforces IOSTAT type and definability");
        expect_contains(invalid.diagnostics, "READ item must be definable",
                        "READ rejects literal and otherwise nondefinable input items");
        expect_contains(invalid.diagnostics, "positional I/O control follows a keyword control",
                        "I/O validation rejects positional controls after keywords");
        expect_contains(invalid.diagnostics, "unknown I/O control 'mystery'",
                        "I/O validation rejects unknown controls explicitly");
        expect_contains(invalid.diagnostics, "I/O implied-DO step cannot be zero",
                        "I/O validation rejects a zero implied-DO step");
        expect_contains(invalid.diagnostics, "END= is not yet supported in WRITE",
                        "unsupported standard controls fail explicitly instead of being ignored");
        expect_contains(invalid.diagnostics, "OPEN UNIT= must be an asterisk or a scalar INTEGER",
                        "OPEN validates the external unit type");
        expect_contains(invalid.diagnostics, "OPEN FILE= must be a scalar CHARACTER expression",
                        "OPEN validates FILE= as CHARACTER");
        expect_contains(invalid.diagnostics, "OPEN STATUS= must be a scalar CHARACTER expression",
                        "OPEN validates STATUS= as CHARACTER");
        f2c_result_free(&invalid);
    }
}

static void test_formatted_record_end_branch(void) {
    static const char source[] = "program record_reader\n"
                                 "  character*72 :: line\n"
                                 "  integer :: status\n"
                                 "10 read(5, fmt='(A72)', end=90, err=80, iostat=status) line\n"
                                 "  go to 10\n"
                                 "80 stop\n"
                                 "90 continue\n"
                                 "end program record_reader\n";
    F2cOptions options = {"record_reader.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "formatted record READ control branches translate");
    expect_contains(result.code, "f2c_read_record", "A edit descriptor reads a complete record");
    expect_contains(result.code, "f2c_io_status == EOF) goto f2c_label_90",
                    "READ END target is preserved");
    expect_contains(result.code, "f2c_io_status == 0) goto f2c_label_80",
                    "READ ERR target is preserved");
    expect_contains(result.code, "status = f2c_io_status == EOF ? -1",
                    "READ IOSTAT receives a Fortran-compatible status class");
    expect_contains(result.code, "f2c_label_90", "READ-only target label is emitted");
    f2c_result_free(&result);

    {
        static const char simple_source[] = "program read_record\n"
                                            "  character*8 :: line\n"
                                            "  read(5, '(A8)') line\n"
                                            "end program read_record\n";
        F2cOptions simple_options = {"read_record.f90", F2C_SOURCE_FREE, 0};
        F2cResult simple = f2c_transpile(simple_source, strlen(simple_source), &simple_options);
        expect(simple.error_count == 0U, "formatted record READ without controls translates");
        expect_contains(simple.code, "(void)f2c_format_read_character",
                        "formatted record READ uses the typed format engine");
        f2c_result_free(&simple);
    }
}

static void test_labeled_do_terminal_branch(void) {
    static const char source[] = "      subroutine labeled_loop(enabled, total)\n"
                                 "      logical enabled(3)\n"
                                 "      integer total, i\n"
                                 "      total = 0\n"
                                 "      do 20 i = 1, 3\n"
                                 "         if (.not.enabled(i)) go to 20\n"
                                 "         total = total + i\n"
                                 "   20 continue\n"
                                 "      end\n";
    F2cOptions options = {"labeled_loop.f", F2C_SOURCE_FIXED, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    const char *label;
    const char *loop_end;
    expect(result.error_count == 0U, "branch to labeled DO terminal translates");
    label = result.code != NULL ? strstr(result.code, "f2c_label_20: ;") : NULL;
    loop_end = label != NULL ? strstr(label, "\n    }") : NULL;
    expect(label != NULL && loop_end != NULL,
           "labeled DO terminal target remains inside the generated C loop");
    f2c_result_free(&result);
}

static void test_labeled_block_if(void) {
    static const char source[] = "      subroutine labeled_if(ok, value)\n"
                                 "      logical ok\n"
                                 "      integer value\n"
                                 "  170 if (ok) then\n"
                                 "         value = 1\n"
                                 "      else\n"
                                 "         value = 2\n"
                                 "      end if\n"
                                 "      end\n";
    F2cOptions options = {"labeled_if.f", F2C_SOURCE_FIXED, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "statement label on block IF translates");
    expect_contains(result.code, "} else {",
                    "statement label does not hide its block IF from construct binding");
    f2c_result_free(&result);
}

static void test_local_kind_parameter_semantics(void) {
    static const char source[] = "double precision function scaled_norm(x)\n"
                                 "  integer, parameter :: wp = kind(1.d0)\n"
                                 "  real(wp) :: x, threshold\n"
                                 "  threshold = real(radix(0._wp), wp) ** &\n"
                                 "    ceiling((minexponent(0._wp) - 1) * 0.5_wp)\n"
                                 "  scaled_norm = threshold + x\n"
                                 "end function scaled_norm\n"
                                 "real function narrow_to_default(x)\n"
                                 "  double precision :: x\n"
                                 "  narrow_to_default = real(x)\n"
                                 "end function narrow_to_default\n"
                                 "subroutine iso_kind_value(x)\n"
                                 "  use, intrinsic :: iso_fortran_env, only: real64\n"
                                 "  integer, parameter :: rk = real64\n"
                                 "  real(kind=rk) :: x\n"
                                 "end subroutine iso_kind_value\n"
                                 "double precision function double_complex_part(z)\n"
                                 "  double complex :: z\n"
                                 "  double_complex_part = real(z)\n"
                                 "end function double_complex_part\n";
    F2cOptions options = {"kind_parameter.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "local KIND parameter expressions translate");
    expect_contains(result.code, "double scaled_norm(double *x)",
                    "REAL(wp) declarations inherit the KIND expression precision");
    expect_contains(result.code, "DBL_MIN_EXP",
                    "kind-suffixed model arguments select double-precision limits");
    expect_contains(result.code, "pow((double)",
                    "REAL(..., wp) and _wp literals select double-precision power");
    expect(result.code == NULL || strstr(result.code, "_Generic((0.f),") == NULL,
           "double KIND parameters never use a float model argument");
    expect(result.code == NULL || strstr(result.code, "powf(") == NULL,
           "double KIND parameters never select single-precision libm calls");
    expect_contains(result.code, "float narrow_to_default(double *x)",
                    "REAL without a KIND argument returns default REAL");
    expect_contains(result.code, "f2c_result = ((float)((*x)))",
                    "REAL without KIND explicitly narrows double precision");
    expect_contains(result.code, "void iso_kind_value(double *x)",
                    "ISO_FORTRAN_ENV kind aliases retain double precision");
    expect_contains(result.code, "double double_complex_part(f2c_complex_double *z)",
                    "REAL preserves a double-complex component kind");
    expect_contains(result.code, "((double)creal((*z)))",
                    "REAL extracts a double-complex component without narrowing");
    f2c_result_free(&result);
}

static void test_unsupported_semantics_are_errors(void) {
    static const char source[] = "subroutine unsupported(value)\n"
                                 "  real :: value\n"
                                 "  backspace 1\n"
                                 "end subroutine unsupported\n";
    F2cOptions options = {"unsupported.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count != 0U, "unsupported semantic constructs are hard errors");
    expect_contains(result.diagnostics, "unsupported Fortran statement: backspace 1",
                    "unsupported construct diagnostic identifies the rejected statement");
    f2c_result_free(&result);

    {
        static const char expression_source[] = "program unsupported_statement_expression\n"
                                                "  implicit none\n"
                                                "  integer :: values(2, 3)\n"
                                                "  if (any(shape(values) /= [2, 3])) error stop\n"
                                                "end program unsupported_statement_expression\n";
        F2cOptions expression_options = {"unsupported_statement_expression.f90", F2C_SOURCE_FREE,
                                         0};
        F2cResult expression_result =
            f2c_transpile(expression_source, sizeof(expression_source) - 1U, &expression_options);
        expect(expression_result.error_count != 0U && expression_result.code == NULL,
               "unsupported typed statement expressions cannot silently become false");
        expect_contains(expression_result.diagnostics,
                        "code generation does not support this typed statement expression",
                        "unsupported typed statement expressions report a hard diagnostic");
        f2c_result_free(&expression_result);
    }
}

static void test_control_flow_semantics(void) {
    static const char source[] = "subroutine invalid_control_flow(i, r)\n"
                                 "  implicit none\n"
                                 "  integer :: i\n"
                                 "  real :: r\n"
                                 "  logical :: flag\n"
                                 "  if (i) then\n"
                                 "  end if\n"
                                 "  do while (r)\n"
                                 "  end do\n"
                                 "  select case (r)\n"
                                 "  case (1)\n"
                                 "  end select\n"
                                 "  do flag = 1, 3\n"
                                 "  end do\n"
                                 "  do i = 1, 3, 0\n"
                                 "  end do\n"
                                 "  if (flag) 10, 20, 30\n"
                                 "  goto (10, 20), r\n"
                                 "  assign 10 to r\n"
                                 "10 continue\n"
                                 "20 continue\n"
                                 "30 continue\n"
                                 "end subroutine invalid_control_flow\n";
    F2cOptions options = {"invalid_control_flow.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 8U,
           "typed control-flow violations are rejected before C generation");
    expect(result.code == NULL, "invalid control-flow semantics suppress generated C17");
    expect_contains(result.diagnostics, "IF condition must be a scalar LOGICAL expression",
                    "IF condition type is validated");
    expect_contains(result.diagnostics, "DO WHILE condition must be a scalar LOGICAL expression",
                    "DO WHILE condition type is validated");
    expect_contains(result.diagnostics,
                    "SELECT CASE selector must be a scalar INTEGER, CHARACTER, or LOGICAL",
                    "SELECT CASE rejects selector types outside the standard domain");
    expect_contains(result.diagnostics, "counted DO variable must be a definable scalar",
                    "counted DO validates its control variable");
    expect_contains(result.diagnostics, "counted DO step cannot be zero",
                    "counted DO rejects a constant zero step");
    expect_contains(result.diagnostics, "arithmetic IF selector must be a scalar INTEGER or REAL",
                    "arithmetic IF validates its ordered numeric selector");
    expect_contains(result.diagnostics, "computed GOTO selector must be a scalar INTEGER",
                    "computed GOTO validates its selector");
    expect_contains(result.diagnostics, "ASSIGN target must be a definable scalar INTEGER",
                    "legacy assigned-label storage validates its target");
    f2c_result_free(&result);
}

static void test_select_case_semantics(void) {
    static const char source[] =
        "subroutine invalid_select_case(i, candidate, values, flag, text)\n"
        "  implicit none\n"
        "  integer :: i, candidate, values(2)\n"
        "  logical :: flag\n"
        "  character(len=4) :: text\n"
        "  case (1)\n"
        "  select case (i)\n"
        "  i = i + 1\n"
        "  case (candidate)\n"
        "  case (1:3)\n"
        "    if (flag) then\n"
        "    case (8)\n"
        "    end if\n"
        "  case (3:4)\n"
        "  case default\n"
        "  case default\n"
        "  end select\n"
        "  select case (flag)\n"
        "  case (.false.:.true.)\n"
        "  end select\n"
        "  select case (i)\n"
        "  case ('text')\n"
        "  end select\n"
        "  select case (values)\n"
        "  case (1)\n"
        "  end select\n"
        "  select case (text)\n"
        "  case ('a':'m')\n"
        "  case ('m':'z')\n"
        "  end select\n"
        "  select case (i)\n"
        "  case (,)\n"
        "  end select\n"
        "end subroutine invalid_select_case\n"
        "subroutine unmatched_select()\n"
        "  end select\n"
        "end subroutine unmatched_select\n"
        "subroutine missing_end_select(value)\n"
        "  integer :: value\n"
        "  select case (value)\n"
        "  case default\n"
        "    value = 1\n"
        "end subroutine missing_end_select\n"
        "subroutine mismatched_end()\n"
        "  if (.true.) then\n"
        "  end do\n"
        "end subroutine mismatched_end\n";
    F2cOptions options = {"invalid_select_case.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count != 0U && result.code == NULL,
           "invalid SELECT CASE constructs suppress generated C17");
    expect_contains(result.diagnostics, "CASE statement must be directly enclosed by SELECT CASE",
                    "orphan and structurally nested CASE statements are rejected");
    expect_contains(result.diagnostics, "SELECT CASE block must begin with CASE or END SELECT",
                    "SELECT CASE rejects executable statements before its first branch");
    expect_contains(result.diagnostics, "CASE lower value must be an initialization expression",
                    "CASE values reject nonconstant variables");
    expect_contains(result.diagnostics, "CASE value range overlaps a previous range",
                    "overlapping numeric and character CASE ranges are rejected");
    expect_contains(result.diagnostics, "SELECT CASE cannot contain more than one CASE DEFAULT",
                    "SELECT CASE rejects duplicate default branches");
    expect_contains(result.diagnostics, "LOGICAL CASE values cannot use ranges",
                    "LOGICAL CASE rejects range syntax");
    expect_contains(result.diagnostics, "does not match SELECT CASE selector type",
                    "CASE values must match the selector type");
    expect_contains(result.diagnostics,
                    "SELECT CASE selector must be a scalar INTEGER, CHARACTER, or LOGICAL",
                    "SELECT CASE rejects array selectors");
    expect_contains(result.diagnostics, "malformed CASE value range list",
                    "malformed CASE lists are syntax errors");
    expect_contains(result.diagnostics, "END SELECT has no matching opening construct",
                    "unmatched SELECT terminators are syntax errors");
    expect_contains(result.diagnostics, "SELECT CASE construct is missing END SELECT",
                    "unterminated SELECT CASE constructs are syntax errors");
    expect_contains(result.diagnostics, "END DO has no matching opening construct",
                    "typed terminators reject a mismatched opening construct");
    expect_contains(result.diagnostics, "IF construct is missing END IF",
                    "mismatched terminators retain the unclosed construct diagnostic");
    f2c_result_free(&result);

    {
        static const char mismatch_source[] = "program case_span\n"
                                              "  integer :: value\n"
                                              "  select case (value)\n"
                                              "  case ('x')\n"
                                              "  end select\n"
                                              "end program case_span\n";
        F2cInput input = {
            mismatch_source, sizeof(mismatch_source) - 1U, {"case_span.f90", F2C_SOURCE_FREE, 0}};
        F2cConfig config = limited_config();
        DiagnosticCapture capture = {0};
        config.diagnostic_callback = capture_diagnostic;
        config.diagnostic_user_data = &capture;
        result = f2c_transpile_project_config(&input, 1U, &config);
        expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC &&
                   capture.line == 4U && capture.column == 9U,
               "CASE diagnostics preserve the exact endpoint token span and stable code");
        expect(strcmp(capture.source_name, "case_span.f90") == 0,
               "CASE endpoint diagnostics preserve their physical source file");
        f2c_result_free(&result);
    }
}

static void test_named_construct_semantics(void) {
    static const char source[] = "subroutine invalid_control_targets()\n"
                                 "  implicit none\n"
                                 "  cycle\n"
                                 "  exit missing\n"
                                 "end subroutine invalid_control_targets\n"
                                 "subroutine invalid_named_if(flag)\n"
                                 "  implicit none\n"
                                 "  logical :: flag\n"
                                 "  decision: if (flag) then\n"
                                 "    cycle decision\n"
                                 "  else wrong_branch\n"
                                 "  else if (flag) then decision\n"
                                 "  end if wrong_end\n"
                                 "end subroutine invalid_named_if\n"
                                 "subroutine invalid_end_names(i)\n"
                                 "  implicit none\n"
                                 "  integer :: i\n"
                                 "  required_name: do i = 1, 2\n"
                                 "  end do\n"
                                 "  do i = 1, 2\n"
                                 "  end do unexpected\n"
                                 "end subroutine invalid_end_names\n"
                                 "subroutine duplicate_names(flag)\n"
                                 "  implicit none\n"
                                 "  logical :: flag\n"
                                 "  duplicate: do while (flag)\n"
                                 "    duplicate: do while (flag)\n"
                                 "    end do duplicate\n"
                                 "  end do duplicate\n"
                                 "end subroutine duplicate_names\n"
                                 "subroutine invalid_case_name(value)\n"
                                 "  implicit none\n"
                                 "  integer :: value\n"
                                 "  choice: select case (value)\n"
                                 "  case default wrong_choice\n"
                                 "  end select choice\n"
                                 "end subroutine invalid_case_name\n"
                                 "subroutine invalid_prefix(value)\n"
                                 "  implicit none\n"
                                 "  integer :: value\n"
                                 "  not_a_construct: value = 1\n"
                                 "end subroutine invalid_prefix\n";
    F2cOptions options = {"invalid_named_constructs.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count != 0U && result.code == NULL,
           "invalid named constructs suppress generated C17");
    expect_contains(result.diagnostics, "CYCLE statement is not enclosed by a DO construct",
                    "unnamed CYCLE requires an active DO construct");
    expect_contains(result.diagnostics, "EXIT names unknown construct 'missing'",
                    "named EXIT resolves only active construct identities");
    expect_contains(result.diagnostics, "CYCLE target 'decision' is not a DO construct",
                    "CYCLE rejects a named non-DO construct target");
    expect_contains(result.diagnostics,
                    "ELSE construct name 'wrong_branch' does not match 'decision'",
                    "IF branch names must match their owning construct");
    expect_contains(result.diagnostics, "ELSE IF cannot follow ELSE in the same IF construct",
                    "IF branch ordering is validated by the construct binder");
    expect_contains(result.diagnostics,
                    "END IF construct name 'wrong_end' does not match 'decision'",
                    "named IF terminators must match their opener");
    expect_contains(result.diagnostics, "END DO must specify construct name 'required_name'",
                    "a named DO requires its name on END DO");
    expect_contains(result.diagnostics, "END DO names an unnamed construct",
                    "an unnamed DO rejects a spurious END DO name");
    expect_contains(result.diagnostics,
                    "construct name 'duplicate' duplicates an active construct name",
                    "active construct names must be unique");
    expect_contains(result.diagnostics,
                    "CASE construct name 'wrong_choice' does not match 'choice'",
                    "CASE branch names are bound to their SELECT CASE owner");
    expect_contains(result.diagnostics, "malformed construct name or control target syntax",
                    "construct-name prefixes are rejected on non-construct statements");
    f2c_result_free(&result);
}

static void test_where_semantics(void) {
    static const char source[] = "subroutine invalid_where(mask, values, other, scalar)\n"
                                 "  implicit none\n"
                                 "  logical :: mask(2,2), scalar\n"
                                 "  integer :: values(3), other(2,2)\n"
                                 "  where (scalar) other = 1\n"
                                 "  where (mask) values = other\n"
                                 "  where (mask) other = values\n"
                                 "  where (values) values = 1\n"
                                 "  elsewhere\n"
                                 "  guarded: where (mask)\n"
                                 "    call rejected()\n"
                                 "  elsewhere guarded\n"
                                 "  elsewhere guarded\n"
                                 "  elsewhere (mask) guarded\n"
                                 "  end where guarded\n"
                                 "end subroutine invalid_where\n"
                                 "subroutine missing_where_end(mask, values)\n"
                                 "  logical :: mask(2)\n"
                                 "  integer :: values(2)\n"
                                 "  where (mask)\n"
                                 "    values = 1\n"
                                 "end subroutine missing_where_end\n";
    F2cOptions options = {"invalid_where.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count != 0U && result.code == NULL,
           "invalid WHERE constructs suppress generated C17");
    expect_contains(result.diagnostics, "WHERE mask must be a LOGICAL array expression",
                    "WHERE rejects scalar and nonlogical masks");
    expect_contains(result.diagnostics,
                    "masked assignment target rank 1 does not conform to WHERE mask rank 2",
                    "masked assignments require a target conformable with the mask");
    expect_contains(result.diagnostics,
                    "masked assignment value rank 1 does not conform to WHERE mask rank 2",
                    "array assignment values must conform with the WHERE mask");
    expect_contains(result.diagnostics, "ELSEWHERE must be directly enclosed by WHERE",
                    "orphan ELSEWHERE statements are rejected");
    expect_contains(result.diagnostics,
                    "WHERE body may contain only intrinsic assignment or nested WHERE constructs",
                    "WHERE blocks reject non-assignment executable statements");
    expect_contains(result.diagnostics,
                    "WHERE construct cannot contain more than one unmasked ELSEWHERE",
                    "WHERE permits at most one default branch");
    expect_contains(result.diagnostics, "masked ELSEWHERE cannot follow an unmasked ELSEWHERE",
                    "masked branches cannot follow a default ELSEWHERE");
    expect_contains(result.diagnostics, "WHERE construct is missing END WHERE",
                    "unterminated WHERE constructs are syntax errors");
    f2c_result_free(&result);
}

static void test_character_shape_diagnostics(void) {
    static const char data_source[] = "subroutine invalid_character_data()\n"
                                      "  character*4 values(2)\n"
                                      "  data values / 'only-one' /\n"
                                      "end subroutine invalid_character_data\n";
    static const char local_source[] = "subroutine invalid_assumed_local()\n"
                                       "  character*(*) local_value\n"
                                       "end subroutine invalid_assumed_local\n";
    static const char declaration_source[] =
        "subroutine invalid_character_declaration()\n"
        "  character*4 values(2) = (/ 'ONE ', 'TWO ', 'THRE' /)\n"
        "end subroutine invalid_character_declaration\n";
    static const char substring_source[] = "subroutine invalid_substrings(value, bound)\n"
                                           "  implicit none\n"
                                           "  character(len=4) :: value, result\n"
                                           "  real :: bound\n"
                                           "  result = value(bound:3)\n"
                                           "  result = value(1:3:2)\n"
                                           "  result = value(0:2)\n"
                                           "  result = value(1:5)\n"
                                           "  result = value(4:2)\n"
                                           "end subroutine invalid_substrings\n";
    static const char empty_substring_source[] = "subroutine empty_substring(value, result)\n"
                                                 "  implicit none\n"
                                                 "  character(len=4) :: value, result\n"
                                                 "  result = value(1:0)\n"
                                                 "end subroutine empty_substring\n";
    static const char intent_source[] = "subroutine invalid_intent_targets(number, text)\n"
                                        "  implicit none\n"
                                        "  integer, intent(in) :: number\n"
                                        "  character(len=4), intent(in) :: text\n"
                                        "  number = 2\n"
                                        "  text(1:1) = 'X'\n"
                                        "  read(*,*) number\n"
                                        "end subroutine invalid_intent_targets\n";
    F2cOptions options = {"invalid_character.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(data_source, strlen(data_source), &options);
    expect(result.error_count != 0U, "invalid character DATA shape is a hard error");
    expect_contains(result.diagnostics, "invalid DATA array initializer",
                    "character DATA value-count mismatch is diagnosed");
    f2c_result_free(&result);
    result = f2c_transpile(local_source, strlen(local_source), &options);
    expect(result.error_count != 0U, "invalid assumed-length local is a hard error");
    expect_contains(result.diagnostics, "assumed-length CHARACTER 'local_value'",
                    "local assumed-length CHARACTER is diagnosed before C emission");
    f2c_result_free(&result);
    result = f2c_transpile(declaration_source, strlen(declaration_source), &options);
    expect(result.error_count != 0U,
           "shape-incompatible character declaration initializer is a hard error");
    expect_contains(result.diagnostics, "shape-incompatible CHARACTER declaration initializer",
                    "character declaration initializer shape mismatch is diagnosed");
    f2c_result_free(&result);
    result = f2c_transpile(substring_source, strlen(substring_source), &options);
    expect(result.error_count == 5U,
           "invalid CHARACTER substring bounds are rejected before code generation");
    expect(result.code == NULL, "invalid substring semantics suppress generated C17");
    expect_contains(result.diagnostics, "lower bound must be a scalar INTEGER",
                    "substring lower-bound type is checked from the AST");
    expect_contains(result.diagnostics, "substring range cannot have a stride",
                    "substring strides are rejected explicitly");
    expect_contains(result.diagnostics, "lower bound must be at least one",
                    "substring lower bounds enforce Fortran one-based indexing");
    expect_contains(result.diagnostics, "upper bound exceeds declared length 4",
                    "constant substring upper bounds are checked against declared length");
    expect_contains(result.diagnostics, "may exceed the upper bound by at most one",
                    "substring ranges reject invalid reversed intervals");
    f2c_result_free(&result);
    result = f2c_transpile(empty_substring_source, strlen(empty_substring_source), &options);
    expect(result.error_count == 0U,
           "a lower bound exactly one past the upper bound is a legal empty substring");
    f2c_result_free(&result);
    result = f2c_transpile(intent_source, strlen(intent_source), &options);
    expect(result.error_count == 3U,
           "INTENT(IN) names, elements, and substrings are not definable targets");
    expect_contains(result.diagnostics, "assignment target is not definable",
                    "assignment validation rejects INTENT(IN) scalar and substring targets");
    expect_contains(result.diagnostics, "READ item must be definable",
                    "READ validation rejects INTENT(IN) input targets");
    f2c_result_free(&result);
}

static void test_empty_declaration_diagnostic(void) {
    static const char source[] = "program malformed\n"
                                 "  integer = 1\n"
                                 "end program malformed\n";
    F2cOptions options = {"empty_declaration.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count != 0U, "empty declaration target is a hard error");
    expect_contains(result.diagnostics, "invalid or empty declaration target",
                    "empty declaration diagnostic identifies the malformed target");
    f2c_result_free(&result);
}

static void test_declaration_expression_semantics(void) {
    static const char source[] = "subroutine invalid_declaration_expressions()\n"
                                 "  implicit none\n"
                                 "  integer :: bad_bounds(1.5:4)\n"
                                 "  character(len=.true.) :: bad_length\n"
                                 "  integer :: bad_type = 'text'\n"
                                 "  integer :: bad_rank(2,2) = [1, 2, 3, 4]\n"
                                 "end subroutine invalid_declaration_expressions\n";
    F2cOptions options = {"invalid_declaration_expressions.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 4U,
           "invalid typed declaration expressions are rejected before code generation");
    expect(result.code == NULL, "invalid declaration semantics suppress generated C17");
    expect_contains(result.diagnostics, "array lower bound must be a scalar INTEGER expression",
                    "non-integer array bounds are diagnosed from the cached specification AST");
    expect_contains(result.diagnostics, "character length must be a scalar INTEGER expression",
                    "non-integer CHARACTER lengths are diagnosed semantically");
    expect_contains(result.diagnostics, "declaration initializer type is incompatible",
                    "incompatible declaration initializer types are diagnosed");
    expect_contains(result.diagnostics, "declaration initializer rank 1 does not match rank-2",
                    "declaration initializer rank mismatch is diagnosed");
    f2c_result_free(&result);
}

static void test_declaration_attribute_semantics(void) {
    static const char source[] =
        "subroutine invalid_attributes(saved_arg, initialized_arg, alloc_arg)\n"
        "  implicit none\n"
        "  integer, save :: saved_arg\n"
        "  integer :: initialized_arg = 1\n"
        "  integer, allocatable :: alloc_arg\n"
        "  integer, intent(in) :: local_value\n"
        "  integer, parameter :: missing_value\n"
        "  integer, parameter, allocatable :: conflicting_value = 1\n"
        "end subroutine invalid_attributes\n";
    F2cOptions options = {"invalid_attributes.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 5U,
           "conflicting and misplaced declaration attributes are hard errors");
    expect(result.code == NULL, "invalid declaration attributes suppress generated C17");
    expect_contains(result.diagnostics, "dummy argument 'saved_arg' cannot have the SAVE",
                    "SAVE is rejected on dummy arguments");
    expect_contains(result.diagnostics,
                    "dummy argument 'initialized_arg' cannot have a declaration initializer",
                    "dummy declaration initialization is rejected");
    expect(result.diagnostics == NULL ||
               strstr(result.diagnostics, "ALLOCATABLE dummy argument 'alloc_arg'") == NULL,
           "ALLOCATABLE dummy arguments are accepted by the descriptor ABI");
    expect_contains(result.diagnostics, "INTENT attribute on 'local_value' requires",
                    "INTENT is restricted to dummy arguments");
    expect_contains(result.diagnostics, "PARAMETER entity 'missing_value' requires an initializer",
                    "PARAMETER declarations require a constant definition");
    expect_contains(result.diagnostics,
                    "PARAMETER entity 'conflicting_value' cannot be ALLOCATABLE",
                    "PARAMETER and ALLOCATABLE conflicts are rejected");
    f2c_result_free(&result);
}

static void test_intrinsic_signature_diagnostics(void) {
    static const char source[] = "subroutine invalid_intrinsics(value)\n"
                                 "  implicit none\n"
                                 "  real :: value\n"
                                 "  value = abs()\n"
                                 "  value = mod(1)\n"
                                 "  value = max(1.0)\n"
                                 "end subroutine invalid_intrinsics\n";
    F2cOptions options = {"invalid_intrinsics.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 3U, "intrinsic arity violations are hard semantic errors");
    expect_contains(result.diagnostics, "abs requires exactly 1 argument",
                    "unary intrinsic arity is diagnosed from the signature table");
    expect_contains(result.diagnostics, "mod requires exactly 2 arguments",
                    "binary intrinsic arity is diagnosed from the signature table");
    expect_contains(result.diagnostics, "max requires between 2 and 64 arguments",
                    "variadic intrinsic arity is diagnosed from the signature table");
    f2c_result_free(&result);
}

static void test_malformed_expression_diagnostics(void) {
    static const char source[] = "subroutine malformed_expression(x)\n"
                                 "  real :: x\n"
                                 "\n"
                                 "  x = (1.0 + )\n"
                                 "  x = 2.0 trailing\n"
                                 "end subroutine malformed_expression\n";
    F2cOptions options = {"malformed_expression.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 2U,
           "each malformed or partially consumed expression is a hard error");
    expect(result.code == NULL, "expression parse errors prevent C17 emission");
    expect_contains(result.diagnostics, "malformed_expression.f90:4:12:",
                    "expression diagnostic retains source file, physical line, and logical column");
    expect_contains(result.diagnostics, "unexpected token near ')'",
                    "expression diagnostic identifies the first unexpected token");
    expect_contains(result.diagnostics, "malformed_expression.f90:5:9:",
                    "unconsumed expression suffix is diagnosed on its own line");
    expect_contains(result.diagnostics, "unexpected token near 'trailing'",
                    "unconsumed expression suffix is shown to the user");
    f2c_result_free(&result);
}

static void test_array_constructor_diagnostics(void) {
    static const char extent_mismatch[] = "subroutine extent_mismatch()\n"
                                          "  implicit none\n"
                                          "  integer :: i, values(3)\n"
                                          "  values = [(i, i=1,4)]\n"
                                          "end subroutine extent_mismatch\n";
    static const char invalid_iterator[] = "subroutine invalid_iterator()\n"
                                           "  implicit none\n"
                                           "  integer :: values(2)\n"
                                           "  real :: r\n"
                                           "  values = [(1, r=1,2)]\n"
                                           "end subroutine invalid_iterator\n";
    static const char zero_step[] = "subroutine zero_step()\n"
                                    "  implicit none\n"
                                    "  integer :: i, values(2)\n"
                                    "  values = [(i, i=1,2,0)]\n"
                                    "end subroutine zero_step\n";
    static const char incompatible_type[] = "subroutine incompatible_type()\n"
                                            "  implicit none\n"
                                            "  integer :: values(2)\n"
                                            "  values = ['A', 'B']\n"
                                            "end subroutine incompatible_type\n";
    static const char misplaced_implied_do[] = "subroutine misplaced_implied_do()\n"
                                               "  implicit none\n"
                                               "  integer :: i, value\n"
                                               "  value = (i, i=1,2)\n"
                                               "end subroutine misplaced_implied_do\n";
    F2cOptions options = {"constructor_invalid.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(extent_mismatch, sizeof(extent_mismatch) - 1U, &options);
    expect(result.error_count != 0U, "static array-constructor extent mismatch is a hard error");
    expect_contains(result.diagnostics, "array-constructor extent 4 does not match target extent 3",
                    "constructor shape diagnostic reports both exact extents");
    expect(result.code == NULL, "constructor shape errors suppress generated output");
    f2c_result_free(&result);

    result = f2c_transpile(invalid_iterator, sizeof(invalid_iterator) - 1U, &options);
    expect(result.error_count != 0U, "non-integer implied-DO iterator is a hard error");
    expect_contains(result.diagnostics, "iterator 'r' must be a scalar INTEGER",
                    "constructor iterator diagnostic reports the offending name");
    f2c_result_free(&result);

    result = f2c_transpile(zero_step, sizeof(zero_step) - 1U, &options);
    expect(result.error_count != 0U, "constant zero implied-DO step is a hard error");
    expect_contains(result.diagnostics, "implied-DO step must not be zero",
                    "constructor step diagnostic explains the invalid control");
    f2c_result_free(&result);

    result = f2c_transpile(incompatible_type, sizeof(incompatible_type) - 1U, &options);
    expect(result.error_count != 0U, "incompatible constructor element type is a hard error");
    expect_contains(result.diagnostics, "element type is incompatible with target 'values'",
                    "constructor type diagnostic reports the target");
    f2c_result_free(&result);

    result = f2c_transpile(misplaced_implied_do, sizeof(misplaced_implied_do) - 1U, &options);
    expect(result.error_count != 0U, "an implied DO outside a constructor is a hard error");
    expect_contains(result.diagnostics, "valid only inside an array constructor",
                    "misplaced implied-DO diagnostic explains its required context");
    f2c_result_free(&result);
}

static void test_implicit_mapping_semantics(void) {
    static const char source[] =
        "subroutine mapped_types(alpha, index, cword)\n"
        "  implicit real(kind=8) (a-b, d-h, o-z), integer (i-n), character(len=4) (c)\n"
        "end subroutine mapped_types\n"
        "function mapped_result() result(number)\n"
        "  implicit integer (i-n)\n"
        "  number = 23\n"
        "end function mapped_result\n";
    F2cOptions options = {"implicit_mapping.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "explicit IMPLICIT maps and untyped function results translate");
    expect_contains(result.header,
                    "void mapped_types(double *alpha, "
                    "int32_t *index, char *cword, "
                    "size_t f2c_len_cword);",
                    "IMPLICIT kind, integer, and CHARACTER mappings define the public ABI");
    expect_contains(result.header, "int32_t mapped_result(void);",
                    "an untyped function RESULT uses its active IMPLICIT letter map");
    expect_contains(result.code, "int32_t f2c_result = {0};",
                    "the generated function result storage uses the mapped integer type");
    f2c_result_free(&result);

    {
        static const char overlap[] = "subroutine overlap()\n"
                                      "  implicit integer (a-c), real (c-z)\n"
                                      "end subroutine overlap\n";
        F2cOptions invalid_options = {"implicit_overlap.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid = f2c_transpile(overlap, strlen(overlap), &invalid_options);
        expect(invalid.error_count != 0U, "overlapping letters in IMPLICIT maps are hard errors");
        expect_contains(invalid.diagnostics, "implicit_overlap.f90:2:33:",
                        "IMPLICIT overlap diagnostics retain source position");
        expect_contains(invalid.diagnostics, "letter 'c' appears in more than one",
                        "IMPLICIT overlap diagnostics identify the conflicting letter");
        expect(invalid.code == NULL, "invalid IMPLICIT maps suppress generated output");
        f2c_result_free(&invalid);
    }
    {
        static const char reversed[] = "subroutine reversed()\n"
                                       "  implicit integer (z-a)\n"
                                       "end subroutine reversed\n";
        F2cOptions invalid_options = {"implicit_reversed.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid = f2c_transpile(reversed, strlen(reversed), &invalid_options);
        expect(invalid.error_count != 0U, "a descending IMPLICIT letter range is a hard error");
        expect_contains(invalid.diagnostics, "invalid IMPLICIT letter range 'z-a'",
                        "descending IMPLICIT diagnostics retain the malformed range");
        f2c_result_free(&invalid);
    }
    {
        static const char missing_type[] = "subroutine missing_type()\n"
                                           "  implicit none(type)\n"
                                           "  value = 1\n"
                                           "end subroutine missing_type\n";
        F2cOptions invalid_options = {"implicit_none_type.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid = f2c_transpile(missing_type, strlen(missing_type), &invalid_options);
        expect(invalid.error_count != 0U, "IMPLICIT NONE(TYPE) rejects an undeclared local scalar");
        expect_contains(invalid.diagnostics, "implicit_none_type.f90:3:1:",
                        "undeclared implicit symbol diagnostics use the first reference line");
        expect_contains(invalid.diagnostics, "'value' has no type under IMPLICIT NONE",
                        "IMPLICIT NONE(TYPE) names the untyped symbol");
        f2c_result_free(&invalid);
    }
    {
        static const char none_conflict[] = "subroutine none_conflict()\n"
                                            "  implicit none(type)\n"
                                            "  implicit real (a-z)\n"
                                            "end subroutine none_conflict\n";
        F2cOptions invalid_options = {"implicit_none_conflict.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid = f2c_transpile(none_conflict, strlen(none_conflict), &invalid_options);
        expect(invalid.error_count != 0U, "IMPLICIT NONE(TYPE) conflicts with a letter type map");
        expect_contains(invalid.diagnostics, "conflicts with IMPLICIT NONE(TYPE)",
                        "conflicting IMPLICIT modes have an actionable diagnostic");
        f2c_result_free(&invalid);
    }
    {
        static const char missing_external[] = "subroutine missing_external()\n"
                                               "  implicit none(external)\n"
                                               "  call helper()\n"
                                               "end subroutine missing_external\n";
        F2cOptions invalid_options = {"implicit_none_external.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid =
            f2c_transpile(missing_external, strlen(missing_external), &invalid_options);
        expect(invalid.error_count != 0U,
               "IMPLICIT NONE(EXTERNAL) rejects an undeclared external procedure");
        expect_contains(invalid.diagnostics, "implicit_none_external.f90:3:1:",
                        "implicit external diagnostics use the call-site line");
        expect_contains(invalid.diagnostics, "requires an EXTERNAL declaration",
                        "implicit external diagnostics explain the required interface");
        f2c_result_free(&invalid);
    }
    {
        static const char explicit_external[] = "subroutine explicit_external()\n"
                                                "  implicit none(type, external)\n"
                                                "  external helper\n"
                                                "  call helper()\n"
                                                "end subroutine explicit_external\n";
        F2cOptions explicit_options = {"explicit_external.f90", F2C_SOURCE_FREE, 0};
        F2cResult explicit_result =
            f2c_transpile(explicit_external, strlen(explicit_external), &explicit_options);
        expect(explicit_result.error_count == 0U,
               "an explicit EXTERNAL declaration satisfies IMPLICIT NONE(EXTERNAL)");
        expect_contains(explicit_result.code, "extern void helper(void);",
                        "an explicitly declared external subroutine retains a void prototype");
        f2c_result_free(&explicit_result);
    }
    {
        static const char fixed_lexical[] =
            "      SUBROUTINE IMPLICIT_LEXICAL(OUTPUT)\n"
            "      IMPLICIT NONE\n"
            "      REAL OUTPUT\n"
            "      OUTPUT = 100.E+0\n"
            "      IF (OUTPUT.GT.0.E0 . AND. OUTPUT.LT.200.E0) OUTPUT = 1.E0\n"
            "  100 FORMAT('VALUE ',A,I2)\n"
            "      END\n";
        F2cOptions fixed_options = {"implicit_lexical.f", F2C_SOURCE_FIXED, 0};
        F2cResult fixed_result =
            f2c_transpile(fixed_lexical, strlen(fixed_lexical), &fixed_options);
        expect(fixed_result.error_count == 0U,
               "implicit symbol discovery respects fixed-form exponent, dotted operator, and "
               "FORMAT boundaries");
        expect(fixed_result.code == NULL || strstr(fixed_result.code, "int32_t i2") == NULL,
               "FORMAT descriptors never become implicit local variables");
        expect(fixed_result.code == NULL || strstr(fixed_result.code, "float and") == NULL,
               "spaced dotted operators never become implicit local variables");
        f2c_result_free(&fixed_result);
    }
}

static void test_expression_shape_semantics(void) {
    static const char source[] = "program shape_mismatch\n"
                                 "  implicit none\n"
                                 "  real :: left(2), right(3), result(2)\n"
                                 "  result = left + right\n"
                                 "end program shape_mismatch\n";
    F2cOptions options = {"shape_mismatch.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count != 0U,
           "known nonconformable elemental operands are rejected before code generation");
    expect_contains(result.diagnostics, "nonconformable array operands in dimension 1",
                    "shape diagnostics identify the mismatching dimension");
    expect(result.code == NULL, "shape errors suppress generated C17 output");
    f2c_result_free(&result);

    {
        static const char invalid_matmul[] = "program invalid_matmul\n"
                                             "  implicit none\n"
                                             "  real :: vector_a(2), vector_b(2), scalar\n"
                                             "  scalar = matmul(vector_a, vector_b)\n"
                                             "end program invalid_matmul\n";
        F2cOptions invalid_options = {"invalid_matmul.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid = f2c_transpile(invalid_matmul, strlen(invalid_matmul), &invalid_options);
        expect(invalid.error_count != 0U && invalid.code == NULL,
               "MATMUL rejects the nonstandard vector-by-vector combination");
        expect_contains(invalid.diagnostics, "use DOT_PRODUCT",
                        "MATMUL rank diagnostics identify the standard alternative");
        f2c_result_free(&invalid);
    }
    {
        static const char invalid_extents[] = "program invalid_matmul_extents\n"
                                              "  implicit none\n"
                                              "  real :: left(2, 3), right(4, 2), result(2, 2)\n"
                                              "  result = matmul(left, right)\n"
                                              "end program invalid_matmul_extents\n";
        F2cOptions invalid_options = {"invalid_matmul_extents.f90", F2C_SOURCE_FREE, 0};
        F2cResult invalid =
            f2c_transpile(invalid_extents, strlen(invalid_extents), &invalid_options);
        expect(invalid.error_count != 0U && invalid.code == NULL,
               "known nonconformable MATMUL inner extents are rejected semantically");
        expect_contains(invalid.diagnostics, "inner extents are not conformable (3 and 4)",
                        "MATMUL extent diagnostics report both conflicting extents");
        f2c_result_free(&invalid);
    }
}

static void test_local_array_bounds_contract(void) {
    static const char source[] = "subroutine select_local(index, result)\n"
                                 "  implicit none\n"
                                 "  integer, intent(in) :: index\n"
                                 "  integer, intent(out) :: result\n"
                                 "  integer :: values(2)\n"
                                 "  values(1) = 11\n"
                                 "  values(2) = 22\n"
                                 "  result = values(index)\n"
                                 "end subroutine select_local\n";
    F2cOptions options = {"local_bounds.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U, "explicit-shape local array access translates");
    expect_contains(result.code, "static inline F2C_UNUSED size_t f2c_array_offset(",
                    "generated C exposes a checked local-array offset contract");
    expect_contains(result.code, "values[f2c_array_offset(",
                    "local explicit-shape indexing enforces the declared bounds");
    f2c_result_free(&result);
}

static void test_type_identifier_assignment(void) {
    static const char source[] = "subroutine assign_type(type)\n"
                                 "  implicit none\n"
                                 "  character, intent(out) :: type\n"
                                 "  type = 'P'\n"
                                 "end subroutine assign_type\n";
    F2cOptions options = {"type_identifier.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "a variable named TYPE remains assignable after derived-type parsing is enabled");
    expect_contains(result.code, "memmove(type, \"P\"",
                    "TYPE assignment is emitted as a CHARACTER assignment, not a declaration");
    f2c_result_free(&result);
}

static void test_token_driven_scope_and_reference_boundaries(void) {
    static const char source[] = "subroutine token_boundaries(value, output)\n"
                                 "  implicit none\n"
                                 "  real, intent(in) :: value\n"
                                 "  real, intent(out) :: output\n"
                                 "  character(len=32) :: marker\n"
                                 "  marker = 'call hidden(value); end subroutine'\n"
                                 "  if (value > 0.0) then\n"
                                 "    output = value\n"
                                 "  else\n"
                                 "    output = -value\n"
                                 "  end if\n"
                                 "  output = output + 1.0\n"
                                 "end subroutine token_boundaries\n";
    F2cOptions options = {"token_boundaries.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "strings containing procedure keywords and nested END statements translate");
    expect(result.code == NULL || strstr(result.code, "extern void hidden") == NULL,
           "CALL-like text inside a character literal cannot create an external procedure");
    expect_contains(result.code, "(*output) + 1.0f",
                    "END IF does not truncate the enclosing procedure body");
    f2c_result_free(&result);

    {
        static const char fixed_call[] = "      SUBROUTINE CALL_IF(FLAG, N)\n"
                                         "      LOGICAL FLAG\n"
                                         "      INTEGER N\n"
                                         "      IF (FLAG)\n"
                                         "     $   CALL CONSUME(N)\n"
                                         "      END\n"
                                         "      SUBROUTINE CONSUME(N)\n"
                                         "      INTEGER N\n"
                                         "      N = N + 1\n"
                                         "      END\n";
        F2cOptions fixed_options = {"call_if.f", F2C_SOURCE_FIXED, 0};
        F2cResult fixed_result = f2c_transpile(fixed_call, strlen(fixed_call), &fixed_options);
        expect(fixed_result.error_count == 0U,
               "single-line IF with a continued CALL discovers its subroutine target");
        expect(fixed_result.code == NULL || strstr(fixed_result.code, "float consume =") == NULL,
               "a nested CALL target cannot degrade into an implicitly typed scalar");
        expect_contains(fixed_result.code, "consume(n);",
                        "the nested CALL uses the project procedure interface");
        f2c_result_free(&fixed_result);
    }
}

static void test_statement_function_typed_lowering(void) {
    static const char source[] = "subroutine statement_function_case(x, y)\n"
                                 "  real :: x, y, square, affine\n"
                                 "  square(t) = t * t\n"
                                 "  affine(t) = square(t) + x\n"
                                 "  y = affine(x + 1.0)\n"
                                 "end subroutine statement_function_case\n";
    F2cOptions options = {"statement_function.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.error_count == 0U,
           "legacy statement functions build typed IR and translate without extern shims");
    expect_contains(result.code, "f2c_statement_argument_",
                    "statement-function calls materialize typed per-call argument temporaries");
    expect_contains(result.code, "= ((*x) + 1.0f),",
                    "statement-function actuals are sequenced and evaluated exactly once");
    expect(result.code == NULL || strstr(result.code, "extern float square") == NULL,
           "statement functions never leak as unresolved C procedures");
    f2c_result_free(&result);
}

static void test_tokenized_use_association(void) {
    static const char provider[] = "module use_provider\n"
                                   "  integer, parameter :: remote_value = 7\n"
                                   "end module use_provider\n";
    static const char consumer[] =
        "subroutine use_consumer(result)\n"
        "  use, non_intrinsic :: use_provider, only : local_value => remote_value\n"
        "  integer, intent(out) :: result\n"
        "  result = local_value\n"
        "end subroutine use_consumer\n";
    F2cInput inputs[] = {
        {provider, sizeof(provider) - 1U, {"use_provider.f90", F2C_SOURCE_FREE, 0}},
        {consumer, sizeof(consumer) - 1U, {"use_consumer.f90", F2C_SOURCE_FREE, 0}},
    };
    F2cResult result = f2c_transpile_project(inputs, 2U);
    expect(result.error_count == 0U,
           "tokenized USE syntax accepts module nature, spacing, ONLY, and renaming together");
    expect_contains(result.code, "f2c_module_use_provider_remote_value",
                    "USE renaming retains the provider's collision-free storage identity");
    f2c_result_free(&result);
}

int main(void) {
    test_version_contract();
    test_empty_source();
    test_resource_limits();
    test_physical_source_mapping();
    test_program_and_control_flow();
    test_wide_do_trip_count();
    test_blas_style_subroutine();
    test_nested_loop_optimization_hints();
    test_fixed_form_continuation();
    test_fixed_form_spaced_exponent();
    test_extended_fixed_form_width();
    test_function_result_and_power();
    test_legacy_blas_constructs();
    test_character_and_external_interface();
    test_complex_temporary_arguments();
    test_typed_integer_and_nested_call_expressions();
    test_single_complex_specific_abs();
    test_lapack_f90_semantics();
    test_standalone_lapack_constants_module();
    test_allocatable_arrays();
    test_allocatable_dummy_descriptor_abi();
    test_allocatable_function_result_descriptor();
    test_deferred_character_allocation();
    test_common_block_storage();
    test_project_procedure_registry();
    test_project_interface_semantics();
    test_explicit_interface_semantics();
    test_abstract_procedure_semantics();
    test_lapack_driver_data_semantics();
    test_structured_data_initializers();
    test_internal_procedure_and_file_units();
    test_dynamic_array_sections();
    test_list_directed_io();
    test_formatted_record_end_branch();
    test_labeled_do_terminal_branch();
    test_labeled_block_if();
    test_local_kind_parameter_semantics();
    test_character_shape_diagnostics();
    test_empty_declaration_diagnostic();
    test_declaration_expression_semantics();
    test_declaration_attribute_semantics();
    test_intrinsic_signature_diagnostics();
    test_malformed_expression_diagnostics();
    test_array_constructor_diagnostics();
    test_expression_shape_semantics();
    test_local_array_bounds_contract();
    test_type_identifier_assignment();
    test_token_driven_scope_and_reference_boundaries();
    test_statement_function_typed_lowering();
    test_tokenized_use_association();
    test_implicit_mapping_semantics();
    test_control_flow_semantics();
    test_select_case_semantics();
    test_named_construct_semantics();
    test_where_semantics();
    test_unsupported_semantics_are_errors();
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all f2c tests passed");
    return EXIT_SUCCESS;
}
