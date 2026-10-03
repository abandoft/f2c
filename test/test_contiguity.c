#include "internal/f2c.h"

#include <stdio.h>
#include <string.h>

static int failures;

typedef struct StorageDiagnostic {
    const char *marker;
    size_t count;
    size_t begin_line;
    size_t begin_column;
    size_t end_line;
    size_t end_column;
} StorageDiagnostic;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void capture_storage_diagnostic(const F2cDiagnostic *diagnostic, void *data) {
    StorageDiagnostic *capture = data;
    if (capture->marker == NULL || strstr(diagnostic->message, capture->marker) == NULL)
        return;
    expect(diagnostic->code == F2C_DIAGNOSTIC_SEMANTIC &&
               diagnostic->severity == F2C_DIAGNOSTIC_ERROR,
           "illegal storage association is a stable semantic error");
    expect(diagnostic->begin.source_name != NULL &&
               strcmp(diagnostic->begin.source_name, "qualified_association.f90") == 0,
           "the storage diagnostic retains its original source name");
    ++capture->count;
    capture->begin_line = diagnostic->begin.line;
    capture->begin_column = diagnostic->begin.column;
    capture->end_line = diagnostic->end.line;
    capture->end_column = diagnostic->end.column;
}

static void test_designators(void) {
    Symbol symbol = {.name = (char *)"matrix",
                     .c_name = (char *)"matrix",
                     .type = TYPE_INTEGER,
                     .kind = 4,
                     .rank = 2U,
                     .value_category = F2C_VALUE_VARIABLE};
    Unit unit = {.symbols = &symbol, .symbol_count = 1U};
    static const struct {
        const char *source;
        int contiguous;
    } cases[] = {{"matrix", 1},          {"matrix(:,2)", 1},     {"matrix(2:3,2)", 1},
                 {"matrix(:,2:3)", 1},   {"matrix(:,:)", 1},     {"matrix(1:4,:)", 0},
                 {"matrix(2,:)", 0},     {"matrix(:,1:4:1)", 0}, {"matrix(4:1:-1,2)", 0},
                 {"matrix([1,3],2)", 0}, {"matrix(1,2)", 0},     {"(matrix)", 0},
                 {"matrix+1", 0}};
    size_t dimension;
    size_t index;
    for (dimension = 0U; dimension < symbol.rank; ++dimension) {
        symbol.dimensions[dimension].lower_expression = f2c_expr_new_integer_constant(1);
        symbol.dimensions[dimension].upper_expression = f2c_expr_new_integer_constant(4);
    }
    f2c_shape_from_symbol(NULL, &symbol.shape, &symbol);
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const char *error = NULL;
        F2cExpr *expression = f2c_parse_expression_ast(&unit, cases[index].source, &error);
        expect(expression != NULL && error == NULL, cases[index].source);
        expect(f2c_expression_is_simply_contiguous(expression) == cases[index].contiguous,
               cases[index].source);
        f2c_expr_free(expression);
    }
    {
        F2cExpr whole = {.kind = F2C_EXPR_NAME, .rank = 2U, .symbol = &symbol};
        symbol.pointer = 1;
        expect(!f2c_expression_is_simply_contiguous(&whole),
               "a pointer without CONTIGUOUS has no static contiguity guarantee");
        symbol.contiguous = 1;
        expect(f2c_expression_is_simply_contiguous(&whole),
               "CONTIGUOUS pointer objects carry the guarantee");
        symbol.pointer = 0;
        symbol.contiguous = 0;
        symbol.dimensions[0].kind = F2C_DIMENSION_ASSUMED_SHAPE;
        expect(!f2c_expression_is_simply_contiguous(&whole),
               "ordinary assumed-shape objects may have non-unit strides");
        symbol.contiguous = 1;
        expect(f2c_expression_is_simply_contiguous(&whole),
               "CONTIGUOUS assumed-shape objects carry the guarantee");
        symbol.contiguous = 0;
        whole.kind = F2C_EXPR_CALL;
        symbol.external_result_pointer = 1;
        symbol.external_result_contiguous = 1;
        expect(f2c_expression_is_simply_contiguous(&whole),
               "a CONTIGUOUS pointer result is a simply contiguous array variable");
        symbol.external_result_contiguous = 0;
        expect(!f2c_expression_is_simply_contiguous(&whole),
               "a general pointer result is not simply contiguous");
    }
    for (dimension = 0U; dimension < symbol.rank; ++dimension) {
        f2c_expr_free(symbol.dimensions[dimension].lower_expression);
        f2c_expr_free(symbol.dimensions[dimension].upper_expression);
    }
    expect(!f2c_expression_is_simply_contiguous(NULL), "an absent designator has no guarantee");
}

static void test_associations(void) {
    static const struct {
        const char *actual_attributes;
        const char *actual;
        const char *dummy;
        const char *diagnostic;
    } cases[] = {
        {"volatile", "values(6:1:-2)", "integer, volatile :: dummy(3)", "C1539"},
        {"asynchronous", "values(6:1:-2)", "integer, volatile :: dummy(3)", "C1539"},
        {"volatile", "values(6:1:-2)", "integer, asynchronous :: dummy(3)", "C1539"},
        {"volatile", "values(1:3:1)", "integer, volatile :: dummy(3)", "C1539"},
        {"volatile", "values(1:3)", "integer, volatile :: dummy(3)", NULL},
        {"volatile", "values(6:1:-2)", "integer, volatile :: dummy(:)", NULL},
        {"volatile", "values(6:1:-2)", "integer, volatile, contiguous :: dummy(:)", "C1539"},
        {"volatile, pointer", "values", "integer, volatile :: dummy(6)", "C1540"},
        {"volatile, pointer", "values", "integer, volatile, contiguous :: dummy(:)", "C1540"},
        {"volatile, pointer", "values", "integer, volatile :: dummy(:)", NULL},
        {"volatile, pointer", "values", "integer, volatile, pointer :: dummy(:)", NULL},
        {"volatile, pointer, contiguous", "values", "integer, volatile :: dummy(6)", NULL},
        {"pointer", "values", "integer, contiguous, pointer :: dummy(:)", "C1541"},
        {"pointer, contiguous", "values", "integer, contiguous, pointer :: dummy(:)", NULL},
        {"volatile", "values(6:1:-2)", "integer :: dummy(3)", NULL},
        {"target", "values(6:1:-2)", "integer, volatile :: dummy(3)", NULL},
        {"target", "values([1,3,5])", "integer, volatile :: dummy(:)", "vector-subscript"},
        {"target", "values([1,3,5])", "integer, asynchronous :: dummy(:)", "vector-subscript"},
        {"volatile", "(values(6:1:-2))", "integer, volatile :: dummy(3)", NULL}};
    size_t index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char source[1024];
        StorageDiagnostic capture = {.marker = cases[index].diagnostic};
        F2cConfig config = {.structure_size = sizeof(config),
                            .diagnostic_callback = capture_storage_diagnostic,
                            .diagnostic_user_data = &capture};
        F2cInput input;
        F2cResult result;
        (void)snprintf(source, sizeof(source),
                       "program association_case\n"
                       "  integer, %s :: values%s\n"
                       "  call change(%s)\n"
                       "contains\n"
                       "  subroutine change(dummy)\n"
                       "    %s\n"
                       "  end subroutine change\n"
                       "end program association_case\n",
                       cases[index].actual_attributes,
                       strstr(cases[index].actual_attributes, "pointer") != NULL ? "(:)" : "(6)",
                       cases[index].actual, cases[index].dummy);
        input =
            (F2cInput){source, strlen(source), {"qualified_association.f90", F2C_SOURCE_FREE, 0}};
        result = f2c_transpile_project_config(&input, 1U, &config);
        if (cases[index].diagnostic == NULL) {
            expect(result.error_count == 0U, source);
        } else {
            expect(result.error_count != 0U && result.diagnostics != NULL &&
                       strstr(result.diagnostics, cases[index].diagnostic) != NULL,
                   source);
            expect(result.code == NULL, "invalid storage association emits no partial C");
            expect(capture.count == 1U && capture.begin_line == 3U && capture.begin_column == 15U &&
                       capture.end_line == 3U &&
                       capture.end_column == 15U + strlen(cases[index].actual),
                   "association constraints report the complete actual's physical span");
        }
        f2c_result_free(&result);
    }
}

int main(void) {
    test_designators();
    test_associations();
    return failures == 0 ? 0 : 1;
}
