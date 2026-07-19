#include "internal/f2c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static Symbol *add_symbol(Symbol *symbols, size_t index, const char *name, Type type, size_t rank,
                          int external) {
    Symbol *symbol = &symbols[index];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = (char *)name;
    symbol->c_name = (char *)name;
    symbol->type = type;
    symbol->kind = f2c_default_kind(type);
    symbol->rank = rank;
    f2c_shape_from_symbol(NULL, &symbol->shape, symbol);
    symbol->external = external;
    return symbol;
}

static int parse_unit_header_tokens(const char *source, Unit *unit) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    Line line = {0};
    int parsed = 0;
    f2c_token_stream_init(&stream, source, 1U, 1U);
    for (;;) {
        F2cToken *replacement;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            goto cleanup;
        if (count == capacity) {
            const size_t next = capacity == 0U ? 16U : capacity * 2U;
            replacement = (F2cToken *)realloc(tokens, next * sizeof(*replacement));
            if (replacement == NULL)
                goto cleanup;
            tokens = replacement;
            capacity = next;
        }
        tokens[count++] = stream.token;
    }
    line.text = (char *)source;
    line.tokens = tokens;
    line.token_count = count;
    parsed = f2c_parse_unit_header(NULL, &line, unit) == F2C_UNIT_HEADER_PARSED;

cleanup:
    free(tokens);
    return parsed;
}

static void test_kind_shape_and_value_category(void) {
    Symbol symbols[3];
    Unit unit;
    Unit function;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "matrix", TYPE_DOUBLE, 2U, 0);
    symbols[0].kind = 8;
    symbols[0].dimensions[0].kind = F2C_DIMENSION_EXPLICIT;
    symbols[0].dimensions[0].lower = "2";
    symbols[0].dimensions[0].upper = "5";
    symbols[0].dimensions[0].lower_expression = f2c_expr_new_integer_constant(2);
    symbols[0].dimensions[0].upper_expression = f2c_expr_new_integer_constant(5);
    symbols[0].dimensions[1].kind = F2C_DIMENSION_ASSUMED_SHAPE;
    symbols[0].dimensions[1].lower = "1";
    symbols[0].dimensions[1].upper = "*";
    f2c_shape_from_symbol(&unit, &symbols[0].shape, &symbols[0]);
    add_symbol(symbols, 1U, "scale", TYPE_DOUBLE, 0U, 0);
    symbols[1].kind = 8;
    symbols[1].parameter = 1;
    symbols[1].value_category = F2C_VALUE_CONSTANT;
    add_symbol(symbols, 2U, "wide", TYPE_REAL, 0U, 0);
    symbols[2].kind = 16;
    unit.symbols = symbols;
    unit.symbol_count = 3U;

    expect(symbols[0].shape.kind == F2C_SHAPE_ASSUMED_SHAPE && symbols[0].shape.rank == 2U,
           "symbol shape distinguishes assumed-shape from explicit-shape arrays");
    expect(symbols[0].shape.dimensions[0].lower_known &&
               symbols[0].shape.dimensions[0].lower == 2 &&
               symbols[0].shape.dimensions[0].extent_known &&
               symbols[0].shape.dimensions[0].extent == 4U,
           "shape metadata retains constant lower bounds and extents");

    expression = f2c_parse_expression_ast(&unit, "matrix + scale", &error_at);
    expect(expression != NULL && error_at == NULL && expression->type_kind == 8,
           "expression kind propagates through numeric operators");
    expect(expression != NULL && expression->rank == 2U &&
               expression->shape.kind == F2C_SHAPE_EXPRESSION &&
               expression->shape.dimensions[0].extent_known &&
               expression->shape.dimensions[0].extent == 4U,
           "array expression shape, rank, and known extents propagate through operators");
    expect(expression != NULL && expression->value_category == F2C_VALUE_TEMPORARY &&
               expression->children[0]->value_category == F2C_VALUE_VARIABLE &&
               expression->children[1]->value_category == F2C_VALUE_CONSTANT,
           "AST distinguishes variables, named constants, and temporary values");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "matrix(2:5, 1) + wide", &error_at);
    expect(expression != NULL && error_at == NULL && expression->rank == 1U &&
               expression->shape.dimensions[0].extent_known &&
               expression->shape.dimensions[0].extent == 4U,
           "array sections preserve the exact result extent in elemental expressions");
    expect(expression != NULL && expression->type_kind == 16,
           "numeric promotion preserves a wider kind across legacy DOUBLE/REAL type tags");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "[1_8, 2_8, 3_8]", &error_at);
    expect(expression != NULL && error_at == NULL && expression->type_kind == 8 &&
               expression->shape.dimensions[0].extent_known &&
               expression->shape.dimensions[0].extent == 3U,
           "array constructors retain element kind and a known one-dimensional extent");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "size(array=matrix, kind=8)", &error_at);
    expect(expression != NULL && error_at == NULL && expression->type == TYPE_INTEGER &&
               expression->type_kind == 8 && expression->rank == 0U,
           "SIZE retains its selected INTEGER kind and scalar result category");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "shape(source=matrix, kind=8)", &error_at);
    expect(expression != NULL && error_at == NULL && expression->type_kind == 8 &&
               expression->rank == 1U && expression->shape.dimensions[0].extent_known &&
               expression->shape.dimensions[0].extent == 2U,
           "SHAPE typed IR records the source rank as its vector extent");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "lbound(matrix)", &error_at);
    expect(expression != NULL && error_at == NULL && expression->rank == 1U &&
               expression->shape.dimensions[0].extent_known &&
               expression->shape.dimensions[0].extent == 2U,
           "LBOUND without DIM produces a rank-sized vector");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "ubound(matrix, dim=1, kind=8)", &error_at);
    expect(expression != NULL && error_at == NULL && expression->type_kind == 8 &&
               expression->rank == 0U,
           "UBOUND with DIM produces a scalar of the selected kind");
    f2c_expr_free(expression);

    memset(&function, 0, sizeof(function));
    expect(parse_unit_header_tokens("integer(kind=8) function wide()", &function) &&
               function.return_kind == 8 &&
               strcmp(f2c_c_type_kind(function.return_type, function.return_kind), "int64_t") == 0,
           "function headers preserve an explicit INTEGER kind in their ABI");
    f2c_free_unit(&function);

    memset(&function, 0, sizeof(function));
    expect(parse_unit_header_tokens(
               "recursive pure real(kind=8) function evaluate(value) result(answer)", &function) &&
               function.kind == UNIT_FUNCTION && function.recursive && function.pure &&
               function.return_type == TYPE_DOUBLE && function.return_kind == 8 &&
               function.argument_count == 1U && strcmp(function.arguments[0], "value") == 0 &&
               strcmp(function.result_name, "answer") == 0,
           "canonical header tokens accept attributes before a typed function prefix");
    f2c_free_unit(&function);

    memset(&function, 0, sizeof(function));
    expect(parse_unit_header_tokens("real(kind=8) recursive function after_type()", &function) &&
               function.recursive && function.return_type == TYPE_DOUBLE,
           "canonical header tokens accept procedure attributes after a type prefix");
    f2c_free_unit(&function);

    memset(&function, 0, sizeof(function));
    expect(parse_unit_header_tokens("elemental subroutine apply(value)", &function) &&
               function.kind == UNIT_SUBROUTINE && function.elemental &&
               function.argument_count == 1U,
           "canonical header tokens retain ELEMENTAL procedure metadata");
    f2c_free_unit(&function);

    memset(&function, 0, sizeof(function));
    expect(parse_unit_header_tokens("type(cell) function make_cell(value)", &function) &&
               function.return_type == TYPE_DERIVED && function.result_derived_type_name != NULL &&
               strcmp(function.result_derived_type_name, "cell") == 0,
           "derived function headers retain the concrete result type name");
    f2c_free_unit(&function);
    f2c_expr_free(symbols[0].dimensions[0].lower_expression);
    f2c_expr_free(symbols[0].dimensions[0].upper_expression);
}

static void test_typed_numeric_tree(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "n", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 1U, "nwork", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 2U, "dnrm2", TYPE_DOUBLE, 0U, 1);
    unit.symbols = symbols;
    unit.symbol_count = 3U;

    expression = f2c_parse_expression_ast(&unit, "5*n+2*n**2.gt.nwork", &error_at);
    expect(expression != NULL, "numeric comparison produces an AST");
    expect(error_at == NULL, "numeric comparison consumes the complete expression");
    expect(expression != NULL && expression->kind == F2C_EXPR_BINARY,
           "comparison root is a binary expression");
    expect(expression != NULL && expression->type == TYPE_LOGICAL,
           "comparison has LOGICAL result type");
    expect(expression != NULL && expression->child_count == 2U &&
               expression->children[0]->type == TYPE_INTEGER,
           "integer exponentiation remains INTEGER inside a larger expression");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "dnrm2(n, n, 1)", &error_at);
    expect(expression != NULL && expression->kind == F2C_EXPR_CALL,
           "external function reference is represented as a call");
    expect(expression != NULL && expression->type == TYPE_DOUBLE,
           "external function call preserves its declared return type");
    expect(expression != NULL && expression->child_count == 3U,
           "call AST owns all actual arguments");
    f2c_expr_free(expression);
}

static void test_defined_operator_tree(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "left", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 1U, "right", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 2U, "third", TYPE_INTEGER, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 3U;

    expression = f2c_parse_expression_ast(&unit, ".invert. left ** 2", &error_at);
    expect(expression != NULL && error_at == NULL && expression->kind == F2C_EXPR_BINARY &&
               strcmp(expression->text, "**") == 0 &&
               expression->children[0]->kind == F2C_EXPR_UNARY &&
               strcmp(expression->children[0]->text, ".invert.") == 0,
           "a defined unary operator binds more tightly than exponentiation");
    f2c_expr_free(expression);

    error_at = NULL;
    expression = f2c_parse_expression_ast(&unit, "left + right .combine. third * left", &error_at);
    expect(expression != NULL && error_at == NULL && expression->kind == F2C_EXPR_BINARY &&
               strcmp(expression->text, ".combine.") == 0 &&
               expression->children[0]->kind == F2C_EXPR_BINARY &&
               strcmp(expression->children[0]->text, "+") == 0 &&
               expression->children[1]->kind == F2C_EXPR_BINARY &&
               strcmp(expression->children[1]->text, "*") == 0,
           "a defined binary operator has lower precedence than every intrinsic operator");
    f2c_expr_free(expression);
}

static void test_array_section(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "n", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 1U, "work", TYPE_DOUBLE, 1U, 0);
    add_symbol(symbols, 2U, "matrix", TYPE_DOUBLE, 3U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 3U;

    expression = f2c_parse_expression_ast(&unit, "work(1:n:2)", &error_at);
    expect(expression != NULL && error_at == NULL, "strided array section parses completely");
    expect(expression != NULL && expression->kind == F2C_EXPR_ARRAY_REFERENCE,
           "array designator has an explicit array-reference node");
    expect(expression != NULL && expression->rank == 1U, "array section retains rank information");
    expect(expression != NULL && expression->child_count == 1U &&
               expression->children[0]->kind == F2C_EXPR_ARRAY_SECTION,
           "lower, upper, and stride are grouped in an array-section node");
    expect(expression != NULL && expression->children[0]->child_count == 3U,
           "array-section AST owns lower, upper, and stride expressions");
    f2c_expr_free(expression);

    expression = f2c_parse_expression_ast(&unit, "matrix(:, 2, :)", &error_at);
    expect(expression != NULL && error_at == NULL,
           "mixed scalar and triplet subscripts parse completely");
    expect(expression != NULL && expression->rank == 2U,
           "array-reference rank counts every retained section dimension");
    f2c_expr_free(expression);
}

static void test_single_precision_intrinsic_expression(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *first;
    F2cExpr *second;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "z1", TYPE_REAL, 0U, 0);
    add_symbol(symbols, 1U, "phi", TYPE_REAL, 1U, 0);
    add_symbol(symbols, 2U, "i", TYPE_INTEGER, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    first = f2c_parse_expression_ast(&unit, "z1*cos(phi(i-1))", &error_at);
    second = f2c_parse_expression_ast(&unit, "(-z1)*z1*sin(phi(i))", &error_at);
    expect(first != NULL && first->type == TYPE_REAL,
           "single-precision intrinsic expression remains REAL");
    expect(second != NULL && second->type == TYPE_REAL,
           "nested single-precision arithmetic remains REAL");
    f2c_expr_free(first);
    f2c_expr_free(second);
}

static void test_intrinsic_type_registry(void) {
    const Type real_argument[] = {TYPE_REAL};
    const Type complex_argument[] = {TYPE_COMPLEX};
    const Type transfer_arguments[] = {TYPE_REAL, TYPE_COMPLEX, TYPE_INTEGER};
    F2cExpr integer_eight;
    F2cExpr *kind_arguments[1];
    F2cExpr array_argument;
    F2cExpr scalar_argument;
    F2cExpr *rank_arguments[3];
    const F2cIntrinsicSignature *mod_signature;
    expect(f2c_resolve_intrinsic_type("sqrt", real_argument, 1U) == TYPE_REAL,
           "generic SQRT preserves REAL kind");
    expect(f2c_resolve_intrinsic_type("abs", complex_argument, 1U) == TYPE_REAL,
           "complex ABS has a REAL result");
    expect(f2c_resolve_intrinsic_type("transfer", transfer_arguments, 3U) == TYPE_COMPLEX,
           "TRANSFER result type is determined by its mold");
    memset(&integer_eight, 0, sizeof(integer_eight));
    integer_eight.type = TYPE_INTEGER;
    integer_eight.type_kind = 8;
    integer_eight.rank = 2U;
    kind_arguments[0] = &integer_eight;
    expect(f2c_resolve_intrinsic_kind("iand", kind_arguments, 1U) == 8,
           "bit intrinsic result kind is inherited from I");
    expect(f2c_find_intrinsic("ishftc") != NULL &&
               f2c_find_intrinsic("ishftc")->id == F2C_INTRINSIC_ISHFTC,
           "bit intrinsics have stable typed-IR identities");
    expect(f2c_resolve_intrinsic_rank("bit_size", kind_arguments, 1U) == 0U,
           "BIT_SIZE is scalar even when its model argument is not elemental");
    mod_signature = f2c_find_intrinsic("mod");
    expect(mod_signature != NULL && mod_signature->minimum_arguments == 2U &&
               mod_signature->maximum_arguments == 2U,
           "intrinsic registry owns the MOD arity contract");
    memset(&array_argument, 0, sizeof(array_argument));
    memset(&scalar_argument, 0, sizeof(scalar_argument));
    array_argument.rank = 2U;
    rank_arguments[0] = &array_argument;
    rank_arguments[1] = &scalar_argument;
    rank_arguments[2] = &scalar_argument;
    expect(f2c_resolve_intrinsic_rank("len", rank_arguments, 1U) == 2U,
           "elemental LEN rank is derived from its value argument");
    expect(f2c_resolve_intrinsic_rank("kind", rank_arguments, 1U) == 0U,
           "inquiry KIND has a scalar result for an array model");
    expect(f2c_resolve_intrinsic_rank("transfer", rank_arguments, 3U) == 1U,
           "TRANSFER with SIZE has a rank-one result");
}

static void test_fixed_form_spaced_operator_emission(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    int supported = 0;
    char *code;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "lwork", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 1U, "lwmin", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 2U, "lquery", TYPE_LOGICAL, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    expression = f2c_parse_expression_ast(&unit, "lwork.lt.lwmin. and. (.not.lquery)", &error_at);
    code = f2c_emit_expression_ast(&unit, expression, &supported);
    expect(expression != NULL && error_at == NULL,
           "fixed-form spaces inside dotted operators parse completely");
    expect(supported && code != NULL && strstr(code, "&&") != NULL,
           "spaced dotted AND emits a C logical operator");
    free(code);
    f2c_expr_free(expression);
}

static void test_keyword_and_section_emission(void) {
    Symbol symbols[4];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    int supported = 0;
    char *code;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "x", TYPE_REAL, 0U, 0);
    add_symbol(symbols, 1U, "zero", TYPE_REAL, 0U, 0);
    add_symbol(symbols, 2U, "wp", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 3U, "work", TYPE_REAL, 1U, 0);
    symbols[3].dimensions[0].lower = "1";
    symbols[3].dimensions[0].upper = "n";
    unit.symbols = symbols;
    unit.symbol_count = 4U;

    expression = f2c_parse_expression_ast(&unit, "cmplx(x, zero, kind=wp)", &error_at);
    code = f2c_emit_expression_ast(&unit, expression, &supported);
    expect(expression != NULL && error_at == NULL && expression->child_count == 3U &&
               expression->children[2]->kind == F2C_EXPR_KEYWORD_ARGUMENT,
           "intrinsic keyword arguments have explicit AST nodes");
    expect(supported && code != NULL && strstr(code, "kind") == NULL,
           "CMPLX KIND is resolved semantically and not emitted as a C argument");
    free(code);
    f2c_expr_free(expression);

    error_at = NULL;
    supported = 0;
    expression = f2c_parse_expression_ast(&unit, "maxloc(work(1:7), 1)", &error_at);
    code = f2c_emit_expression_ast(&unit, expression, &supported);
    expect(expression != NULL && error_at == NULL,
           "array-section reduction parses through the typed AST path");
    expect(supported && code != NULL && strstr(code, "F2C_MAXIMUM_LOCATION((&work[") != NULL,
           "MAXLOC section emits a strided pointer view and explicit extent");
    free(code);
    f2c_expr_free(expression);
}

static void test_transfer_array_constructor(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "rwork", TYPE_REAL, 1U, 0);
    add_symbol(symbols, 1U, "zero", TYPE_REAL, 0U, 0);
    add_symbol(symbols, 2U, "n", TYPE_INTEGER, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    expression =
        f2c_parse_expression_ast(&unit, "transfer(rwork(1:2*n), (/ (zero, zero) /), n)", &error_at);
    expect(expression != NULL && error_at == NULL,
           "TRANSFER with an array-constructor mold parses completely");
    expect(expression != NULL && expression->kind == F2C_EXPR_CALL,
           "TRANSFER remains an explicit intrinsic call node");
    expect(expression != NULL && expression->type == TYPE_COMPLEX,
           "TRANSFER result type is taken from the complex mold");
    expect(expression != NULL && expression->child_count >= 2U &&
               expression->children[1]->kind == F2C_EXPR_ARRAY_CONSTRUCTOR,
           "TRANSFER mold owns an array-constructor AST");
    f2c_expr_free(expression);
}

static void test_array_constructor_implied_do(void) {
    Symbol symbols[3];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "i", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 1U, "j", TYPE_INTEGER, 0U, 0);
    add_symbol(symbols, 2U, "n", TYPE_INTEGER, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 3U;

    expression = f2c_parse_expression_ast(&unit, "[(i, i=1,n), ((i*j, i=1,2), j=1,3)]", &error_at);
    expect(expression != NULL && error_at == NULL,
           "nested array-constructor implied DO parses completely");
    expect(expression != NULL && expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR &&
               expression->rank == 1U && expression->type == TYPE_INTEGER,
           "array-constructor implied DO retains sequence rank and element type");
    expect(expression != NULL && expression->child_count == 2U &&
               expression->children[0]->kind == F2C_EXPR_IMPLIED_DO &&
               expression->children[0]->child_count == 4U,
           "implied-DO AST owns its value, initial, limit, and default step");
    expect(expression != NULL && expression->child_count == 2U &&
               expression->children[1]->kind == F2C_EXPR_IMPLIED_DO &&
               expression->children[1]->children[0]->kind == F2C_EXPR_IMPLIED_DO,
           "nested implied DO is represented recursively without raw-text reparsing");
    f2c_expr_free(expression);

    error_at = NULL;
    expression = f2c_parse_expression_ast(&unit, "[(i, i=1,4), [5,6]]", &error_at);
    expect(expression != NULL && error_at == NULL && expression->shape.dimensions[0].extent_known &&
               expression->shape.dimensions[0].extent == 6U,
           "constructor shape sums nested values and implied-DO iteration extents");
    f2c_expr_free(expression);
}

static void test_malformed_expression_locations(void) {
    static const char *const invalid[] = {
        "1 + )", "1 trailing", "'unterminated", "1e+", "*100",
    };
    Unit unit;
    size_t i;
    memset(&unit, 0, sizeof(unit));
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        const char *error_at = NULL;
        F2cExpr *expression = f2c_parse_expression_ast(&unit, invalid[i], &error_at);
        expect(expression != NULL, "malformed input still produces an owned recovery AST");
        expect(error_at != NULL, "malformed or unconsumed input retains its error location");
        expect(expression != NULL && expression->source != NULL &&
                   strcmp(expression->source, invalid[i]) == 0,
               "expression root owns the exact source used for diagnostics");
        expect(expression != NULL && expression->parse_error_offset != SIZE_MAX,
               "expression root retains the parser error offset");
        {
            char *lowered = f2c_emit_typed_expression(&unit, expression);
            expect(lowered == NULL, "malformed expressions never lower to a placeholder C value");
            free(lowered);
        }
        f2c_expr_free(expression);
    }
}

static void test_nested_expression_source_ranges(void) {
    Symbol symbols[1];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "x", TYPE_REAL, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 1U;
    expression = f2c_parse_expression_ast(&unit, "max(x, x + abs(x))", &error_at);
    expect(expression != NULL && error_at == NULL && expression->child_count == 2U,
           "nested intrinsic expression parses with owned children");
    expect(expression != NULL && expression->children[0]->source != NULL &&
               strcmp(expression->children[0]->source, "x") == 0 &&
               expression->children[0]->source_offset == 4U,
           "first actual argument retains its exact lexical source range");
    expect(expression != NULL && expression->children[1]->source != NULL &&
               strcmp(expression->children[1]->source, "x + abs(x)") == 0 &&
               expression->children[1]->source_offset == 7U,
           "compound actual argument retains its exact lexical source range");
    expect(expression != NULL && expression->children[1]->child_count == 2U &&
               expression->children[1]->children[1]->source != NULL &&
               strcmp(expression->children[1]->children[1]->source, "abs(x)") == 0 &&
               expression->children[1]->children[1]->source_offset == 11U,
           "nested call retains a stable offset even when identifiers repeat");
    f2c_expr_free(expression);
}

static void test_integer_substitution_clone(void) {
    Symbol symbols[1];
    Unit unit;
    F2cExpr *expression;
    F2cExpr *clone;
    F2cIntegerSubstitution substitution;
    int64_t value = 0;
    const char *error_at = NULL;
    memset(&unit, 0, sizeof(unit));
    add_symbol(symbols, 0U, "i", TYPE_INTEGER, 0U, 0);
    unit.symbols = symbols;
    unit.symbol_count = 1U;
    expression = f2c_parse_expression_ast(&unit, "i * 2 + 1", &error_at);
    if (expression != NULL)
        expression->lowered_c = f2c_strdup("stale_i_expression");
    substitution.symbol = &symbols[0];
    substitution.name = "i";
    substitution.value = 3;
    clone = f2c_expr_clone_substitute_integers(expression, &substitution, 1U);
    expect(expression != NULL && error_at == NULL && clone != NULL &&
               f2c_evaluate_integer_constant(&unit, clone, &value) && value == 7,
           "typed AST cloning substitutes implied-DO integers structurally");
    expect(expression != NULL && expression->children[0]->children[0]->kind == F2C_EXPR_NAME,
           "integer substitution never mutates the canonical source AST");
    expect(clone != NULL && clone->span.begin.line == expression->span.begin.line &&
               clone->source_offset == expression->source_offset,
           "substituted ASTs preserve source and typed metadata");
    expect(clone != NULL && clone->lowered_c == NULL,
           "integer substitution invalidates stale expression-lowering caches");
    f2c_expr_free(clone);
    f2c_expr_free(expression);
}

static void test_integer_iteration_counts(void) {
    uint64_t count = 0U;
    expect(f2c_integer_iteration_count(1, 9, 2, &count) && count == 5U,
           "positive implied-DO iteration counts include both reachable endpoints");
    expect(f2c_integer_iteration_count(9, 1, -2, &count) && count == 5U,
           "negative implied-DO iteration counts are computed without signed overflow");
    expect(f2c_integer_iteration_count(3, 1, 1, &count) && count == 0U,
           "an unreachable implied-DO range has zero iterations");
    expect(f2c_integer_iteration_count(INT64_MIN, INT64_MAX, INT64_MAX, &count) && count == 3U,
           "iteration counting handles the complete signed integer domain");
    expect(!f2c_integer_iteration_count(INT64_MIN, INT64_MAX, 1, &count),
           "an unrepresentable full-domain iteration count is rejected");
    expect(!f2c_integer_iteration_count(1, 2, 0, &count),
           "a zero implied-DO step is rejected before expansion");
}

int main(void) {
    test_kind_shape_and_value_category();
    test_typed_numeric_tree();
    test_defined_operator_tree();
    test_array_section();
    test_single_precision_intrinsic_expression();
    test_intrinsic_type_registry();
    test_fixed_form_spaced_operator_emission();
    test_keyword_and_section_emission();
    test_transfer_array_constructor();
    test_array_constructor_implied_do();
    test_malformed_expression_locations();
    test_nested_expression_source_ranges();
    test_integer_substitution_clone();
    test_integer_iteration_counts();
    if (failures != 0) {
        fprintf(stderr, "%d AST test(s) failed\n", failures);
        return 1;
    }
    puts("all AST tests passed");
    return 0;
}
