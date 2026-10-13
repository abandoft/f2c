#include "internal/f2c.h"
#include "semantic/intrinsic/result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int condition, const char *expression, const char *contract) {
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s: %s\n", expression, contract);
        ++failures;
    }
}

typedef struct ResultCase {
    const char *source;
    Type type;
    int kind;
    size_t rank;
    uint64_t extents[3];
} ResultCase;

static void initialize_symbol(Symbol *symbol, const char *name, Type type, int kind, size_t rank,
                              uint64_t first, uint64_t second) {
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = (char *)name;
    symbol->c_name = (char *)name;
    symbol->type = type;
    symbol->kind = kind;
    symbol->rank = rank;
    symbol->shape.kind = rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPLICIT;
    symbol->shape.rank = rank;
    for (size_t dimension = 0U; dimension < rank; ++dimension) {
        F2cShapeDimension *shape = &symbol->shape.dimensions[dimension];
        shape->kind = F2C_DIMENSION_EXPLICIT;
        shape->lower_known = 1;
        shape->lower = -3;
        shape->extent_known = 1;
        shape->extent = dimension == 0U ? first : second;
    }
}

static void test_registered_results(void) {
    /* Expected values are language contracts, not computed from the registry. */
    static const ResultCase cases[] = {
        {"digits(x=i8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"range(x=i8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"radix(x=i8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"kind(x=i8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"bit_size(i=i8)", TYPE_INTEGER, 8, 0U, {0U}},
        {"huge(x=i1)", TYPE_INTEGER, 1, 0U, {0U}},
        {"epsilon(x=r8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"tiny(x=r8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"precision(x=z8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"maxexponent(x=r8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"minexponent(x=r8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"exponent(x=r8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"present(a=i8)", TYPE_LOGICAL, 4, 0U, {0U}},
        {"allocated(array=i8)", TYPE_LOGICAL, 4, 0U, {0U}},
        {"associated(pointer=i8)", TYPE_LOGICAL, 4, 0U, {0U}},
        {"all(mask=l8)", TYPE_LOGICAL, 8, 0U, {0U}},
        {"any(mask=l1)", TYPE_LOGICAL, 1, 0U, {0U}},
        {"all(dim=1, mask=l8)", TYPE_LOGICAL, 8, 1U, {3U}},
        {"count(mask=l8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"count(kind=seed, mask=l8)", TYPE_INTEGER, 8, 0U, {0U}},
        {"logical(l=.true._8)", TYPE_LOGICAL, 4, 0U, {0U}},
        {"logical(kind=seed, l=.true._1)", TYPE_LOGICAL, 8, 0U, {0U}},
        {"real(a=z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"real(a=r8)", TYPE_REAL, 4, 0U, {0U}},
        {"real(kind=4, a=z8)", TYPE_REAL, 4, 0U, {0U}},
        {"real(kind=seed/2, a=i8)", TYPE_REAL, 4, 0U, {0U}},
        {"cmplx(x=z8)", TYPE_COMPLEX, 4, 0U, {0U}},
        {"cmplx(kind=seed, x=i1)", TYPE_DOUBLE_COMPLEX, 8, 0U, {0U}},
        {"abs(a=z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"aimag(z=z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"aint(a=r8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"anint(kind=seed/2, a=r8)", TYPE_REAL, 4, 0U, {0U}},
        {"int(a=r8)", TYPE_INTEGER, 4, 0U, {0U}},
        {"int(kind=seed, a=r8)", TYPE_INTEGER, 8, 0U, {0U}},
        {"dprod(y=r4, x=r4)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"cabs(z4)", TYPE_REAL, 4, 0U, {0U}},
        {"dabs(r8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"ifix(r4)", TYPE_INTEGER, 4, 0U, {0U}},
        {"sngl(r8)", TYPE_REAL, 4, 0U, {0U}},
        /* Processor extensions keep their explicit component model. */
        {"abs1(z4)", TYPE_REAL, 4, 0U, {0U}},
        {"abs1(z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"abssq(z4)", TYPE_REAL, 4, 0U, {0U}},
        {"abssq(z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"cabs1(z4)", TYPE_REAL, 4, 0U, {0U}},
        {"cabs1(z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"cabs2(z4)", TYPE_REAL, 4, 0U, {0U}},
        {"cabs2(z8)", TYPE_DOUBLE, 8, 0U, {0U}},
        {"dot_product(vector_b=v1, vector_a=v1)", TYPE_INTEGER, 1, 0U, {0U}},
        {"dot_product(vector_b=rv, vector_a=iv)", TYPE_REAL, 4, 0U, {0U}},
        {"dot_product(vector_b=lv8, vector_a=lv1)", TYPE_LOGICAL, 8, 0U, {0U}},
        {"matmul(matrix_b=m2, matrix_a=m1)", TYPE_INTEGER, 1, 2U, {2U, 4U}},
        {"matmul(matrix_b=v2, matrix_a=m1)", TYPE_INTEGER, 1, 1U, {2U}},
        {"matmul(matrix_b=m1, matrix_a=v1)", TYPE_INTEGER, 1, 1U, {3U}},
        {"transpose(matrix=m1)", TYPE_INTEGER, 1, 2U, {3U, 2U}},
        {"reshape(shape=[3,2], source=m1)", TYPE_INTEGER, 1, 2U, {3U, 2U}},
        {"reshape(shape=[(i,i=2,3)], source=m1)", TYPE_INTEGER, 1, 2U, {2U, 3U}},
        {"spread(ncopies=-2, dim=2, source=m1)", TYPE_INTEGER, 1, 3U, {2U, 0U, 3U}},
        {"cshift(shift=1, array=m1, dim=2)", TYPE_INTEGER, 1, 2U, {2U, 3U}},
        {"eoshift(boundary=0_1, shift=1, array=m1)", TYPE_INTEGER, 1, 2U, {2U, 3U}},
        {"pack(vector=v2, mask=l8, array=m1)", TYPE_INTEGER, 1, 1U, {3U}},
        {"unpack(field=0_1, mask=l8, vector=v1)", TYPE_INTEGER, 1, 2U, {2U, 3U}},
        {"merge(mask=.true., fsource=i1, tsource=i1)", TYPE_INTEGER, 1, 0U, {0U}},
        {"merge(mask=.true., fsource=d, tsource=d)", TYPE_DERIVED, 0, 0U, {0U}},
        {"transfer(mold=d, source=i8)", TYPE_DERIVED, 0, 0U, {0U}},
        {"transfer(mold=z8, source=i1)", TYPE_DOUBLE_COMPLEX, 8, 0U, {0U}},
        {"transfer(size=3, mold=i1, source=z8)", TYPE_INTEGER, 1, 1U, {3U}},
        {"transfer(mold=v1, source='ABCD')", TYPE_INTEGER, 1, 1U, {4U}},
        {"findloc(back=.true., kind=seed, value=1, array=m1)", TYPE_INTEGER, 8, 1U, {2U}},
        {"findloc(kind=seed/4, dim=1, value=1, array=m1)", TYPE_INTEGER, 2, 1U, {3U}},
        {"shape(kind=seed, source=m1)", TYPE_INTEGER, 8, 1U, {2U}},
        {"lbound(array=m1)", TYPE_INTEGER, 4, 1U, {2U}},
        {"ubound(kind=seed, array=m1, dim=1)", TYPE_INTEGER, 8, 0U, {0U}},
        {"index(kind=seed, substring='B', string='ABC')", TYPE_INTEGER, 8, 0U, {0U}},
    };
    Unit unit = {0};
    Symbol symbols[19];
    F2cDerivedType derived = {0};
    const char *error = NULL;
    unit.symbols = symbols;
    unit.symbol_count = sizeof(symbols) / sizeof(symbols[0]);
    initialize_symbol(&symbols[0], "i1", TYPE_INTEGER, 1, 0U, 0U, 0U);
    initialize_symbol(&symbols[1], "i8", TYPE_INTEGER, 8, 0U, 0U, 0U);
    initialize_symbol(&symbols[2], "r4", TYPE_REAL, 4, 0U, 0U, 0U);
    initialize_symbol(&symbols[3], "r8", TYPE_DOUBLE, 8, 0U, 0U, 0U);
    initialize_symbol(&symbols[4], "z4", TYPE_COMPLEX, 4, 0U, 0U, 0U);
    initialize_symbol(&symbols[5], "z8", TYPE_DOUBLE_COMPLEX, 8, 0U, 0U, 0U);
    initialize_symbol(&symbols[6], "l1", TYPE_LOGICAL, 1, 2U, 2U, 3U);
    initialize_symbol(&symbols[7], "l8", TYPE_LOGICAL, 8, 2U, 2U, 3U);
    initialize_symbol(&symbols[8], "m1", TYPE_INTEGER, 1, 2U, 2U, 3U);
    initialize_symbol(&symbols[9], "m2", TYPE_INTEGER, 1, 2U, 3U, 4U);
    initialize_symbol(&symbols[10], "v1", TYPE_INTEGER, 1, 1U, 2U, 0U);
    initialize_symbol(&symbols[11], "v2", TYPE_INTEGER, 1, 1U, 3U, 0U);
    initialize_symbol(&symbols[12], "rv", TYPE_REAL, 4, 1U, 2U, 0U);
    initialize_symbol(&symbols[13], "iv", TYPE_INTEGER, 8, 1U, 2U, 0U);
    initialize_symbol(&symbols[14], "lv1", TYPE_LOGICAL, 1, 1U, 2U, 0U);
    initialize_symbol(&symbols[15], "lv8", TYPE_LOGICAL, 8, 1U, 2U, 0U);
    initialize_symbol(&symbols[16], "seed", TYPE_INTEGER, 4, 0U, 0U, 0U);
    initialize_symbol(&symbols[17], "i", TYPE_INTEGER, 4, 0U, 0U, 0U);
    initialize_symbol(&symbols[18], "d", TYPE_DERIVED, 0, 0U, 0U, 0U);
    symbols[18].derived_type = &derived;
    symbols[16].parameter = 1;
    symbols[16].initializer_expression = f2c_parse_expression_ast(&unit, "8", &error);
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const ResultCase *item = &cases[index];
        F2cExpr *expression = f2c_parse_expression_ast(&unit, item->source, &error);
        check(expression != NULL && error == NULL, item->source, "parses typed expression");
        if (expression == NULL)
            continue;
        for (size_t refresh = 0U; refresh < 2U; ++refresh) {
            check(expression->type == item->type && expression->type_kind == item->kind,
                  item->source, "exact scalar model before and after semantic refresh");
            check(expression->derived_type == (item->type == TYPE_DERIVED ? &derived : NULL),
                  item->source, "derived result identity follows its registered model argument");
            check(expression->rank == item->rank && expression->shape.rank == item->rank,
                  item->source, "rank follows associated formal arguments");
            for (size_t dimension = 0U; dimension < item->rank; ++dimension) {
                const F2cShapeDimension *shape = &expression->shape.dimensions[dimension];
                check(shape->lower_known && shape->lower == 1, item->source,
                      "array value results have unit lower bounds");
                check(shape->extent_known && shape->extent == item->extents[dimension],
                      item->source, "shape uses resolved arguments and expanded SHAPE values");
            }
            f2c_intrinsic_refresh_result(&unit, expression);
        }
        f2c_expr_free(expression);
    }
    F2cExpr *dynamic = f2c_parse_expression_ast(&unit, "int(r8, kind=i8)", &error);
    check(dynamic != NULL && dynamic->type_kind == 0, "int(r8, kind=i8)",
          "nonconstant KIND is not replaced by an invented default model");
    f2c_expr_free(dynamic);
    f2c_expr_free(symbols[16].initializer_expression);
}

static void test_procedure_precedence(void) {
    Symbol symbol = {0};
    F2cExpr expression = {0};
    expression.kind = F2C_EXPR_CALL;
    expression.text = "digits";
    expression.symbol = &symbol;
    expression.type = TYPE_DOUBLE;
    expression.type_kind = 8;
    for (size_t variant = 0U; variant < 3U; ++variant) {
        symbol.external = variant == 0U;
        symbol.statement_function = variant == 1U;
        symbol.type_bound = variant == 2U;
        f2c_intrinsic_refresh_result(NULL, &expression);
        check(expression.type == TYPE_DOUBLE && expression.type_kind == 8 &&
                  expression.intrinsic == F2C_INTRINSIC_NONE,
              "digits", "user procedures retain priority over intrinsic spelling");
    }
}

int main(void) {
    test_registered_results();
    test_procedure_precedence();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
