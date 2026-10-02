#include "internal/f2c.h"
#include "semantic/constant/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static F2cExpr *parse(Unit *unit, const char *text) {
    const char *error = NULL;
    F2cExpr *expression = f2c_parse_expression_ast(unit, text, &error);
    expect(expression != NULL && error == NULL, text);
    return expression;
}

static void test_metadata(void) {
    Symbol symbol = {.name = (char *)"array",
                     .c_name = (char *)"array",
                     .type = TYPE_REAL,
                     .kind = 8,
                     .rank = 2U,
                     .pointer = 1,
                     .target = 1,
                     .volatile_entity = 1,
                     .value_category = F2C_VALUE_VARIABLE};
    Unit unit = {.symbols = &symbol, .symbol_count = 1U};
    F2cExpr *expression;
    symbol.shape.kind = F2C_SHAPE_EXPLICIT;
    symbol.shape.rank = 2U;
    symbol.shape.dimensions[0] = (F2cShapeDimension){F2C_DIMENSION_EXPLICIT, 1, 1, -3, 4U};
    symbol.shape.dimensions[1] = (F2cShapeDimension){F2C_DIMENSION_EXPLICIT, 1, 1, 7, 2U};
    expression = parse(&unit, "((array))");
    if (expression != NULL) {
        const F2cExpr *source = f2c_expr_value_source(expression);
        F2cExpr *clone;
        expect(expression->kind == F2C_EXPR_PARENTHESIZED && expression->child_count == 1U &&
                   expression->children[0]->kind == F2C_EXPR_PARENTHESIZED,
               "each pair of parentheses survives as an owned syntax node");
        expect(expression->type == TYPE_REAL && expression->type_kind == 8 &&
                   expression->rank == 2U && expression->shape.kind == F2C_SHAPE_EXPRESSION &&
                   expression->shape.dimensions[0].lower == 1 &&
                   expression->shape.dimensions[0].extent == 4U &&
                   expression->shape.dimensions[1].lower == 1 &&
                   expression->shape.dimensions[1].extent == 2U,
               "array values inherit type, kind and extents but have unit lower bounds");
        expect(!expression->definable && expression->symbol == NULL &&
                   expression->resolved_procedure == NULL &&
                   expression->value_category == F2C_VALUE_TEMPORARY &&
                   expression->storage_qualifiers == F2C_STORAGE_UNQUALIFIED,
               "a parenthesized value is not a pointer, target or qualified storage alias");
        expect(source != NULL && source->symbol == &symbol &&
                   source->storage_qualifiers == F2C_STORAGE_VOLATILE && source->definable &&
                   source->shape.dimensions[0].lower == -3,
               "the producing object retains its original access and bounds");
        expect(expression->source_length == 9U && expression->children[0]->source_length == 7U &&
                   source != NULL && source->source_length == 5U,
               "nested parentheses retain independent full physical ranges");
        expect(expression->span.begin.column == 1U && expression->span.end.column == 10U &&
                   expression->children[0]->span.begin.column == 2U &&
                   expression->children[0]->span.end.column == 9U && source != NULL &&
                   source->span.begin.column == 3U && source->span.end.column == 8U,
               "every nested node retains its own complete structured span");
        clone = f2c_expr_clone_substitute_integers(expression, NULL, 0U);
        expect(clone != NULL && clone->kind == F2C_EXPR_PARENTHESIZED && clone->symbol == NULL &&
                   clone->rank == 2U && !clone->definable &&
                   clone->storage_qualifiers == F2C_STORAGE_UNQUALIFIED &&
                   f2c_expr_value_source(clone)->storage_qualifiers == F2C_STORAGE_VOLATILE,
               "semantic cloning preserves the value/object distinction");
        f2c_expr_free(clone);
    }
    f2c_expr_free(expression);
}

static void test_constants(void) {
    F2cExpr *expression = parse(NULL, "((2 + (3)))");
    int64_t integer = 0;
    double real = 0.0;
    double imaginary = 0.0;
    char *character = NULL;
    size_t length = 0U;
    expect(expression != NULL && f2c_expression_is_initialization_constant(expression) &&
               f2c_evaluate_integer_constant(NULL, expression, &integer) && integer == 5,
           "parenthesized integer initialization expressions fold");
    if (expression != NULL) {
        F2cConstantEvaluation limited = {0};
        expect(!f2c_constant_evaluate_integer(&limited, expression, &integer,
                                              F2C_DEFAULT_MAX_PARSE_DEPTH - 1U),
               "grouping does not bypass constant evaluation depth limits");
    }
    f2c_expr_free(expression);
    expression = parse(NULL, "((1.25_8))");
    expect(expression != NULL && f2c_evaluate_real_constant(NULL, expression, &real) &&
               real == 1.25 && expression->type_kind == 8,
           "real constants retain the producing kind");
    f2c_expr_free(expression);
    expression = parse(NULL, "(((1.0_8,2.0_8)))");
    expect(expression != NULL &&
               f2c_evaluate_complex_constant(NULL, expression, &real, &imaginary) && real == 1.0 &&
               imaginary == 2.0,
           "parenthesized complex constants retain both components");
    f2c_expr_free(expression);
    expression = parse(NULL, "(('a' // achar(0) // 'b'))");
    expect(expression != NULL &&
               f2c_evaluate_character_constant(NULL, expression, &character, &length) &&
               length == 3U && character != NULL && character[0] == 'a' && character[1] == '\0' &&
               character[2] == 'b',
           "character constant grouping preserves byte lengths and embedded NUL");
    free(character);
    f2c_expr_free(expression);
}

static void check_source(const char *source, const char *diagnostic) {
    const F2cOptions options = {"parenthesized.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.code == NULL && result.error_count != 0U && result.diagnostics != NULL &&
               strstr(result.diagnostics, diagnostic) != NULL,
           diagnostic);
    if (result.code != NULL || result.diagnostics == NULL ||
        strstr(result.diagnostics, diagnostic) == NULL)
        fprintf(stderr, "%s\n", result.diagnostics != NULL ? result.diagnostics : "no diagnostic");
    f2c_result_free(&result);
}

typedef struct PolymorphicDiagnostic {
    size_t count;
    size_t line;
    size_t column;
    size_t end_line;
    size_t end_column;
} PolymorphicDiagnostic;

static void capture_polymorphic_diagnostic(const F2cDiagnostic *diagnostic, void *data) {
    PolymorphicDiagnostic *capture = data;
    if (strstr(diagnostic->message, "parenthesized polymorphic values") == NULL)
        return;
    expect(diagnostic->code == F2C_DIAGNOSTIC_UNSUPPORTED &&
               diagnostic->severity == F2C_DIAGNOSTIC_ERROR,
           "unsupported dynamic values are hard errors, not warnings");
    expect(diagnostic->begin.source_name != NULL &&
               strcmp(diagnostic->begin.source_name, "poly_group.f90") == 0,
           "polymorphic grouping diagnostics retain the original source name");
    ++capture->count;
    capture->line = diagnostic->begin.line;
    capture->column = diagnostic->begin.column;
    capture->end_line = diagnostic->end.line;
    capture->end_column = diagnostic->end.column;
}

static void test_polymorphic_diagnostic(void) {
    const char *source = "module m\ntype :: base\ninteger :: n\nend type\ncontains\n"
                         "subroutine p(a)\nclass(base),intent(in)::a\ncall consume(((a)))\nend\n"
                         "subroutine consume(a)\nclass(base),intent(in)::a\nend\nend module\n";
    F2cInput input = {source, strlen(source), {"poly_group.f90", F2C_SOURCE_FREE, 0}};
    PolymorphicDiagnostic capture = {0};
    F2cConfig config = {.structure_size = sizeof(config),
                        .diagnostic_callback = capture_polymorphic_diagnostic,
                        .diagnostic_user_data = &capture};
    F2cResult result = f2c_transpile_project_config(&input, 1U, &config);
    expect(result.code == NULL && result.error_count != 0U && capture.count == 1U,
           "a nested polymorphic value fails atomically with one specific diagnostic");
    expect(capture.line == 8U && capture.column == 15U && capture.end_line == 8U &&
               capture.end_column == 18U,
           "the unsupported value reports the innermost complete parenthesized span");
    if (capture.line != 8U || capture.column != 15U || capture.end_line != 8U ||
        capture.end_column != 18U)
        fprintf(stderr, "polymorphic diagnostic range: %zu:%zu-%zu:%zu\n", capture.line,
                capture.column, capture.end_line, capture.end_column);
    f2c_result_free(&result);
}

static void test_constraints(void) {
    check_source("program p\ninteger :: x\ncall change((x))\ncontains\n"
                 "subroutine change(v)\ninteger,intent(out)::v\nv=1\nend\nend\n",
                 "definable");
    check_source("program p\ninteger,allocatable :: x(:)\ncall consume((x))\ncontains\n"
                 "subroutine consume(v)\ninteger,allocatable,intent(in)::v(:)\nend\nend\n",
                 "ALLOCATABLE");
    check_source("program p\ninteger,pointer :: x\ncall consume((x))\ncontains\n"
                 "subroutine consume(v)\ninteger,pointer,intent(inout)::v\nend\nend\n",
                 "POINTER");
    check_source("subroutine p(x)\ninteger,optional :: x\nlogical :: value\n"
                 "value=present((x))\nend\n",
                 "PRESENT");
    check_source("program p\ninteger,allocatable :: x(:)\nlogical :: value\n"
                 "value=allocated((x))\nend\n",
                 "ALLOCATED");
    check_source("program p\ninteger,pointer :: x\nlogical :: value\n"
                 "value=associated((x))\nend\n",
                 "ASSOCIATED");
    check_source("program p\ninteger, external :: procedure\ninteger :: x\n"
                 "x=(procedure)\nend\n",
                 "a parenthesized primary requires a data expression");
    check_source("subroutine p(a)\ninteger :: a(*)\ninteger :: n\nn=size((a))\nend\n",
                 "whole assumed-size array");
    check_source("module m\ntype :: base\ninteger :: n\nend type\ncontains\n"
                 "subroutine p(a)\nclass(base),intent(in)::a\ncall consume(((a)))\nend\n"
                 "subroutine consume(a)\nclass(base),intent(in)::a\nend\nend module\n",
                 "parenthesized polymorphic values require dynamic-type-aware owned storage");
    check_source("module m\ntype :: base\ninteger :: n\nend type\ncontains\n"
                 "subroutine p(a)\nclass(base),intent(in)::a(:)\ncall consume((a))\nend\n"
                 "subroutine consume(a)\nclass(base),intent(in)::a(:)\nend\nend module\n",
                 "parenthesized polymorphic values require dynamic-type-aware owned storage");
    check_source("module m\ntype :: base\ninteger :: n\nend type\n"
                 "type :: holder\nclass(base),allocatable::value\nend type\ncontains\n"
                 "subroutine p(a)\ntype(holder),intent(in)::a\ncall consume((a%value))\nend\n"
                 "subroutine consume(a)\nclass(base),intent(in)::a\nend\nend module\n",
                 "parenthesized polymorphic values require dynamic-type-aware owned storage");
    check_source("module m\ntype :: base\ninteger :: n\nend type\ncontains\n"
                 "subroutine p()\ncall consume((make()))\nend\n"
                 "function make() result(value)\nclass(base),allocatable::value\n"
                 "allocate(value)\nend\nsubroutine consume(a)\n"
                 "class(base),intent(in)::a\nend\nend module\n",
                 "parenthesized polymorphic values require dynamic-type-aware owned storage");
}

static void test_owned_plan(void) {
    Symbol symbol = {.name = (char *)"array", .type = TYPE_INTEGER, .kind = 4, .rank = 2U};
    Context context = {0};
    F2cStatement statement = {.kind = F2C_STMT_ASSIGNMENT};
    Unit unit = {.context = &context,
                 .symbols = &symbol,
                 .symbol_count = 1U,
                 .phase = F2C_UNIT_TYPED_IR,
                 .statements = &statement,
                 .statement_count = 1U};
    statement.right = parse(&unit, "(array)");
    expect(statement.right != NULL && f2c_plan_expression_lifetimes(&context, &unit) &&
               unit.owned_temporary_count == 1U &&
               unit.owned_temporaries[0].kind == F2C_OWNED_TEMPORARY_ELEMENTAL_ARRAY_VALUE &&
               unit.owned_temporaries[0].rank == 2U &&
               statement.right->temporary_ownership_analyzed &&
               statement.right->owned_temporary_index == 0U &&
               statement.temporary_plan.owned_temporary_count == 1U,
           "the typed statement owns an explicit array snapshot and cleanup plan");
    free(statement.temporary_plan.owned_temporaries);
    free(unit.owned_temporaries);
    f2c_expr_free(statement.right);
}

int main(void) {
    test_metadata();
    test_constants();
    test_constraints();
    test_polymorphic_diagnostic();
    test_owned_plan();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
