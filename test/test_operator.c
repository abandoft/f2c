#include "codegen/operator.h"
#include "core/numeric/power.h"
#include "internal/f2c.h"
#include "semantic/constant/private.h"
#include "semantic/operator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

typedef struct OperatorDiagnostic {
    size_t count;
    size_t line;
    size_t column;
    size_t end_line;
    size_t end_column;
} OperatorDiagnostic;

static void capture_operator_diagnostic(const F2cDiagnostic *diagnostic, void *data) {
    OperatorDiagnostic *capture = data;
    if (strstr(diagnostic->message, "invalid operand types for intrinsic operator") == NULL)
        return;
    ++capture->count;
    capture->line = diagnostic->begin.line;
    capture->column = diagnostic->begin.column;
    capture->end_line = diagnostic->end.line;
    capture->end_column = diagnostic->end.column;
}

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static F2cExpr *parse(const char *text) {
    const char *error = NULL;
    F2cExpr *expression = f2c_parse_expression_ast(NULL, text, &error);
    expect(expression != NULL && error == NULL, text);
    return expression;
}

static void test_spellings(void) {
    static const struct {
        const char *text;
        F2cOperator kind;
    } cases[] = {
        {".EQ.", F2C_OPERATOR_EQUAL},       {". e q .", F2C_OPERATOR_EQUAL},
        {"==", F2C_OPERATOR_EQUAL},         {".NE.", F2C_OPERATOR_NOT_EQUAL},
        {"/=", F2C_OPERATOR_NOT_EQUAL},     {".LE.", F2C_OPERATOR_LESS_EQUAL},
        {"<=", F2C_OPERATOR_LESS_EQUAL},    {".gE.", F2C_OPERATOR_GREATER_EQUAL},
        {".custom.", F2C_OPERATOR_DEFINED}, {".true._8", F2C_OPERATOR_NONE},
        {":", F2C_OPERATOR_NONE},           {".bad2.", F2C_OPERATOR_NONE},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        F2cToken token = {0};
        token.kind = F2C_TOKEN_OPERATOR;
        token.begin = cases[i].text;
        token.length = strlen(token.begin);
        expect(f2c_token_operator(&token) == cases[i].kind, token.begin);
    }
}

static void test_syntax(void) {
    F2cExpr *expression = parse(".false. .eqv. .true. .or. .true. .and. .false.");
    if (expression != NULL) {
        F2cExpr *clone = f2c_expr_clone_substitute_integers(expression, NULL, 0U);
        expect(expression->operator_kind == F2C_OPERATOR_EQUIVALENT &&
                   expression->children[1]->operator_kind == F2C_OPERATOR_OR &&
                   expression->children[1]->children[1]->operator_kind == F2C_OPERATOR_AND,
               "EQV, OR and AND have distinct Fortran precedence");
        expect(clone != NULL && clone->operator_kind == expression->operator_kind &&
                   clone->children[1]->operator_kind == F2C_OPERATOR_OR &&
                   clone->span.begin.column == expression->span.begin.column &&
                   clone->span.end.column == expression->span.end.column,
               "cloning retains canonical operators and physical ranges");
        f2c_expr_free(clone);
    }
    f2c_expr_free(expression);
    expression = parse("-2 * 3 ** 2 ** 2 + 1");
    if (expression != NULL) {
        const F2cExpr *unary = expression->children[0];
        expect(expression->operator_kind == F2C_OPERATOR_ADD && unary->kind == F2C_EXPR_UNARY &&
                   unary->children[0]->operator_kind == F2C_OPERATOR_MULTIPLY &&
                   unary->children[0]->children[1]->operator_kind == F2C_OPERATOR_POWER &&
                   unary->children[0]->children[1]->children[1]->operator_kind ==
                       F2C_OPERATOR_POWER,
               "unary sign follows multiplication, and exponentiation is right associative");
    }
    f2c_expr_free(expression);
    expression = parse(".not. 2_8 .lt. 3_1 .or. .false._2");
    expect(expression != NULL && expression->operator_kind == F2C_OPERATOR_OR &&
               expression->children[0]->operator_kind == F2C_OPERATOR_NOT &&
               expression->children[0]->children[0]->operator_kind == F2C_OPERATOR_LESS &&
               expression->type_kind == 4,
           "NOT follows relational comparison, which produces default logical kind");
    f2c_expr_free(expression);
    expression = parse("2_1 ** 3_8");
    expect(expression != NULL && expression->type == TYPE_INTEGER && expression->type_kind == 8,
           "integer exponentiation chooses the greater integer model range");
    f2c_expr_free(expression);
    expression = parse("2_8 ** 0.5_8");
    expect(expression != NULL && expression->type == TYPE_DOUBLE && expression->type_kind == 8,
           "integer base with a real exponent produces a real result");
    f2c_expr_free(expression);
    expression = parse("2.0_4 ** 3_8");
    expect(expression != NULL && expression->type == TYPE_REAL && expression->type_kind == 4,
           "integer exponent kind does not widen a real base");
    f2c_expr_free(expression);
}

static void test_typing_matrix(void) {
    static const F2cScalarType numeric[] = {
        {TYPE_INTEGER, 1}, {TYPE_INTEGER, 2},        {TYPE_INTEGER, 4}, {TYPE_INTEGER, 8},
        {TYPE_REAL, 4},    {TYPE_REAL, 8},           {TYPE_DOUBLE, 8},  {TYPE_COMPLEX, 4},
        {TYPE_COMPLEX, 8}, {TYPE_DOUBLE_COMPLEX, 8},
    };
    for (size_t i = 0U; i < sizeof(numeric) / sizeof(numeric[0]); ++i) {
        for (size_t j = 0U; j < sizeof(numeric) / sizeof(numeric[0]); ++j) {
            const int complex_result = i >= 7U || j >= 7U;
            const Type expected_type = complex_result       ? TYPE_COMPLEX
                                       : i >= 4U || j >= 4U ? TYPE_REAL
                                                            : TYPE_INTEGER;
            const int expected_kind = i < 4U && j >= 4U                   ? numeric[j].kind
                                      : j < 4U && i >= 4U                 ? numeric[i].kind
                                      : numeric[i].kind > numeric[j].kind ? numeric[i].kind
                                                                          : numeric[j].kind;
            for (F2cOperator op = F2C_OPERATOR_ADD; op <= F2C_OPERATOR_POWER; ++op) {
                F2cOperatorTyping typing;
                const F2cOperatorStatus status =
                    f2c_operator_typing(op, 0, f2c_scalar_type(numeric[i].type, numeric[i].kind),
                                        f2c_scalar_type(numeric[j].type, numeric[j].kind), &typing);
                const F2cScalarType expected = f2c_scalar_type(expected_type, expected_kind);
                expect(status == F2C_OPERATOR_VALID && typing.result.type == expected.type &&
                           typing.result.kind == expected.kind,
                       "full kind-aware numeric operator matrix");
                if (op == F2C_OPERATOR_POWER && j < 4U)
                    expect(typing.right.type == TYPE_INTEGER &&
                               typing.right.kind == numeric[j].kind,
                           "integer exponent remains exact in the typed operand contract");
            }
        }
    }
    for (int kind = 1; kind <= 8; kind *= 2) {
        F2cOperatorTyping typing;
        expect(f2c_operator_typing(F2C_OPERATOR_NOT, 1, f2c_scalar_type(TYPE_LOGICAL, kind),
                                   f2c_scalar_type(TYPE_UNKNOWN, 0),
                                   &typing) == F2C_OPERATOR_VALID &&
                   typing.result.kind == kind,
               "logical unary operations preserve all supported kinds");
        expect(f2c_operator_typing(F2C_OPERATOR_EQUAL, 0, f2c_scalar_type(TYPE_LOGICAL, kind),
                                   f2c_scalar_type(TYPE_LOGICAL, kind),
                                   &typing) == F2C_OPERATOR_INVALID_OPERANDS,
               "LOGICAL equality requires EQV, not a relational operator");
    }
}

static void test_diagnostics(void) {
    static const char *const expressions[] = {
        "1 .and. .true.", ".not. 3",  ".true. == .false.", "(1.0,2.0) < 3.0",
        "'x' + 2",        "1 // 'x'", "- .true.",
    };
    for (size_t i = 0U; i < sizeof(expressions) / sizeof(expressions[0]); ++i) {
        Buffer source = {0};
        const F2cOptions options = {"operator.f90", F2C_SOURCE_FREE, 0};
        F2cResult result;
        f2c_buffer_printf(&source, "program p\nimplicit none\nlogical :: x\nx = %s\nend\n",
                          expressions[i]);
        result = f2c_transpile(source.data, source.length, &options);
        expect(result.code == NULL && result.error_count != 0U && result.diagnostics != NULL &&
                   strstr(result.diagnostics, "invalid operand types for intrinsic operator") !=
                       NULL,
               expressions[i]);
        if (result.diagnostics != NULL &&
            strstr(result.diagnostics, "invalid operand types") == NULL)
            fprintf(stderr, "%s\n", result.diagnostics);
        f2c_result_free(&result);
        free(source.data);
    }
    {
        const char *source = "program p\nimplicit none\nlogical :: x\nx = .true. == .false.\nend\n";
        F2cInput input = {source, strlen(source), {"operator-span.f90", F2C_SOURCE_FREE, 0}};
        OperatorDiagnostic capture = {0};
        F2cConfig config = {.structure_size = sizeof(config),
                            .diagnostic_callback = capture_operator_diagnostic,
                            .diagnostic_user_data = &capture};
        F2cResult result = f2c_transpile_project_config(&input, 1U, &config);
        expect(result.code == NULL && capture.count == 1U && capture.line == 4U &&
                   capture.column == 5U && capture.end_line == 4U && capture.end_column == 22U,
               "operator type errors carry the full physical expression span");
        f2c_result_free(&result);
    }
    {
        const char *source = "program p\ncharacter(kind=4,len=1)::a\ncharacter::b\n"
                             "print *, a // b\nend\n";
        const F2cOptions options = {"operator-character.f90", F2C_SOURCE_FREE, 0};
        F2cResult result = f2c_transpile(source, strlen(source), &options);
        expect(result.code == NULL && result.diagnostics != NULL &&
                   strstr(result.diagnostics, "matching CHARACTER kinds") != NULL,
               "character concatenation diagnoses incompatible kinds before generation");
        f2c_result_free(&result);
    }
}

static void test_constants(void) {
    static const struct {
        const char *text;
        int64_t expected;
    } cases[] = {
        {"3_8 ** 39_8", INT64_C(4052555153018976267)},
        {"(-2_1) ** 7_1", INT8_MIN},
        {"(-2_8) ** 63_8", INT64_MIN},
        {"2_8 ** (-huge(0_8))", 0},
        {"(-1_8) ** (-huge(0_8))", -1},
        {".false. .eqv. .true. .or. .true.", 0},
        {".not. 2_8 .lt. 3_1", 0},
        {"9007199254740993_8 > 9007199254740992_8", 1},
        {"(1.0_4,2.0_4) == (1.0_8,2.0_8)", 1},
        {"'a' == 'a '", 1},
        {"'a' // achar(0) < 'a '", 1},
        {".not. (.true._1 .neqv. .true._8)", 1},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        F2cExpr *expression = parse(cases[i].text);
        int64_t value = 0;
        expect(expression != NULL && f2c_evaluate_integer_constant(NULL, expression, &value) &&
                   value == cases[i].expected,
               cases[i].text);
        f2c_expr_free(expression);
    }
    {
        F2cExpr *expression = parse("(.true. .eqv. (.not. .false.))");
        F2cConstantEvaluation evaluation = {0};
        int64_t value;
        expect(!f2c_constant_evaluate_integer(&evaluation, expression, &value,
                                              F2C_DEFAULT_MAX_PARSE_DEPTH - 1U),
               "logical operator folding cannot reset or bypass the recursive depth budget");
        f2c_expr_free(expression);
    }
    {
        F2cExpr *expression = parse("(-1.0_8) ** 9007199254740993_8");
        double value = 0.0;
        expect(f2c_evaluate_real_constant(NULL, expression, &value) && value == -1.0,
               "real constant powers retain an integer exponent beyond double precision");
        f2c_expr_free(expression);
        expression = parse("2.0_8 ** (-1074_8)");
        expect(f2c_evaluate_real_constant(NULL, expression, &value) && value > 0.0 &&
                   value / 0x1p-1022 == 0x1p-52,
               "negative constant powers retain representable subnormal results");
        f2c_expr_free(expression);
    }
    {
        int64_t value;
        expect(!f2c_numeric_integer_power(2, 63, INT64_MIN, INT64_MAX, &value),
               "wide positive overflow is rejected before any C undefined operation");
        expect(!f2c_numeric_integer_power(2, 7, INT8_MIN, INT8_MAX, &value),
               "narrow result overflow is checked against its own model");
        expect(!f2c_numeric_integer_power(0, -1, INT64_MIN, INT64_MAX, &value),
               "zero raised to a negative integer power is rejected");
        expect(f2c_numeric_integer_power(-1, INT64_MIN, INT64_MIN, INT64_MAX, &value) && value == 1,
               "negative exponent magnitude never evaluates ABS(INT64_MIN)");
    }
    {
        const F2cOptions options = {"operator-case.f90", F2C_SOURCE_FREE, 0};
        const char *source = "program p\nlogical :: value\nselect case(value)\n"
                             "case(.false. .eqv. .true. .or. .true.)\n"
                             "case(.false.)\nend select\nend\n";
        F2cResult result = f2c_transpile(source, strlen(source), &options);
        expect(result.code == NULL && result.diagnostics != NULL &&
                   strstr(result.diagnostics, "overlap") != NULL,
               "SELECT CASE shares logical constant typing, folding and precedence");
        f2c_result_free(&result);
    }
}

static void test_lowering_contract(void) {
    const F2cScalarOperand integer = {"base", {TYPE_INTEGER, 8}};
    const F2cScalarOperand exponent = {"exponent", {TYPE_INTEGER, 8}};
    const F2cScalarOperand real = {"real_value", {TYPE_REAL, 8}};
    const F2cScalarOperand character = {"character_value", {TYPE_CHARACTER, 1}};
    char *code = f2c_emit_scalar_operator(F2C_OPERATOR_POWER, 0, integer, exponent,
                                          (F2cScalarType){TYPE_INTEGER, 8});
    expect(code != NULL && strstr(code, "f2c_pow_i64(base, (int64_t)(exponent))") != NULL &&
               strstr(code, "double") == NULL,
           "wide integer power lowering does not invent a floating-point intermediate");
    free(code);
    code = f2c_emit_scalar_operator(F2C_OPERATOR_POWER, 0, integer, real,
                                    (F2cScalarType){TYPE_DOUBLE, 8});
    expect(code != NULL && strstr(code, "pow(((double)(base)), real_value)") != NULL,
           "mixed power consumes the resolved real result and full operand kinds");
    free(code);
    code = f2c_emit_scalar_operator(F2C_OPERATOR_POWER, 0, integer, real,
                                    (F2cScalarType){TYPE_INTEGER, 8});
    expect(code == NULL, "lowering rejects an inconsistent IR result instead of guessing a type");
    free(code);
    code = f2c_emit_scalar_operator(F2C_OPERATOR_EQUAL, 0, character, character,
                                    (F2cScalarType){TYPE_LOGICAL, 4});
    expect(code == NULL, "character comparison cannot fall back to a C pointer comparison");
    free(code);
    code = f2c_emit_scalar_operator(
        F2C_OPERATOR_MULTIPLY, 0, (F2cScalarOperand){"left", {TYPE_REAL, 16}},
        (F2cScalarOperand){"right", {TYPE_COMPLEX, 16}}, (F2cScalarType){TYPE_COMPLEX, 16});
    expect(
        code != NULL && strstr(code, "f2c_qmul(f2c_make_q((long double)(left)") != NULL,
        "extended-kind lowering retains its existing long-double C ABI without reducing to float");
    free(code);
}

static void test_conversion_contract(void) {
    static const struct {
        F2cScalarOperand operand;
        F2cScalarType target;
        const char *expected;
    } cases[] = {
        {{"effect()", {TYPE_COMPLEX, 4}}, {TYPE_COMPLEX, 4}, "effect()"},
        {{"effect()", {TYPE_COMPLEX, 8}}, {TYPE_DOUBLE_COMPLEX, 8}, "effect()"},
        {{"effect()", {TYPE_COMPLEX, 4}}, {TYPE_COMPLEX, 8}, "f2c_c_to_z(effect())"},
        {{"effect()", {TYPE_COMPLEX, 8}}, {TYPE_COMPLEX, 4}, "f2c_z_to_c(effect())"},
        {{"effect()", {TYPE_COMPLEX, 16}}, {TYPE_COMPLEX, 4}, "f2c_q_to_c(effect())"},
        {{"effect()", {TYPE_REAL, 8}}, {TYPE_COMPLEX, 4}, "f2c_make_c((float)(effect()), 0.0f)"},
        {{"effect()", {TYPE_INTEGER, 8}}, {TYPE_COMPLEX, 8}, "f2c_make_z((double)(effect()), 0.0)"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char *code = f2c_emit_scalar_conversion(cases[index].operand, cases[index].target);
        expect(code != NULL && strcmp(code, cases[index].expected) == 0,
               "typed scalar conversion retains kind, evaluates once and never casts a complex "
               "struct");
        free(code);
    }
    {
        char *code = f2c_emit_scalar_conversion((F2cScalarOperand){"value", {TYPE_CHARACTER, 1}},
                                                (F2cScalarType){TYPE_INTEGER, 4});
        expect(code == NULL, "numeric conversion rejects unrelated operand categories");
        free(code);
        code = f2c_emit_scalar_conversion((F2cScalarOperand){NULL, {TYPE_INTEGER, 4}},
                                          (F2cScalarType){TYPE_INTEGER, 4});
        expect(code == NULL, "typed conversion rejects missing emitted operands");
        free(code);
    }
}

int main(void) {
    test_spellings();
    test_syntax();
    test_typing_matrix();
    test_diagnostics();
    test_constants();
    test_lowering_contract();
    test_conversion_contract();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
