#include "core/numeric/shift.h"
#include "internal/f2c.h"
#include "semantic/constant/array.h"

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

static F2cExpr *parse(Unit *unit, const char *source) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U, capacity = 0U;
    F2cExpr *expression = NULL;
    const char *error = NULL;
    f2c_token_stream_init(&stream, source, 1U, 1U);
    for (;;) {
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            goto cleanup;
        if (count == capacity) {
            const size_t next = capacity == 0U ? 16U : capacity * 2U;
            F2cToken *replacement = (F2cToken *)realloc(tokens, next * sizeof(*tokens));
            if (replacement == NULL)
                goto cleanup;
            capacity = next;
            tokens = replacement;
        }
        tokens[count++] = stream.token;
    }
    expression = f2c_parse_expression_tokens(unit, tokens, count, source, &error);
    if (error != NULL) {
        f2c_expr_free(expression);
        expression = NULL;
    }
cleanup:
    free(tokens);
    return expression;
}

static int evaluate(Unit *unit, const char *source, F2cConstantArray *result) {
    F2cExpr *expression = parse(unit, source);
    const int success = expression != NULL && f2c_evaluate_constant_array(unit, expression, result);
    f2c_expr_free(expression);
    return success;
}

static void integers(Unit *unit, const char *source, size_t rank, const uint64_t *extents,
                     const int64_t *values, size_t count) {
    F2cConstantArray result = {0};
    int matched = evaluate(unit, source, &result) && result.type.type == TYPE_INTEGER &&
                  result.shape.rank == rank && result.count == count;
    for (size_t axis = 0U; matched && axis < rank; ++axis)
        matched = result.shape.dimensions[axis].extent == extents[axis] &&
                  result.shape.dimensions[axis].lower_known &&
                  result.shape.dimensions[axis].lower == 1;
    for (size_t index = 0U; matched && index < count; ++index)
        matched = result.values[index].payload.integer == values[index];
    expect(matched, source);
    f2c_constant_array_free(&result);
}

static void rejected(Unit *unit, const char *source) {
    F2cConstantArray result = {0};
    expect(!evaluate(unit, source, &result) && result.values == NULL && result.count == 0U, source);
    f2c_constant_array_free(&result);
}

static void test_reorder(Unit *unit) {
    const uint64_t matrix[] = {2U, 3U}, transposed[] = {3U, 2U}, cube[] = {2U, 2U, 2U};
    const uint64_t empty[] = {0U, 2U};
    const int64_t natural[] = {1, 2, 3, 4, 5, 6}, ordered[] = {1, 4, 2, 5, 3, 6};
    const int64_t repeated[] = {1, 2, 1, 2, 3, 4, 3, 4};
    const int64_t padded[] = {1, 2, 8, 9, 8, 9};
    integers(unit, "reshape([1,2,3,4,5,6],[2,3])", 2U, matrix, natural, 6U);
    integers(unit, "reshape(order=[2,1],shape=[2,3],source=[1,2,3,4,5,6])", 2U, matrix, ordered,
             6U);
    integers(unit, "transpose(reshape([1,2,3,4,5,6],[2,3],order=[2,1]))", 2U, transposed, natural,
             6U);
    integers(unit, "reshape(reshape([1,2],[1,2]),[2,3],pad=reshape([8,9],[1,2]))", 2U, matrix,
             padded, 6U);
    integers(unit, "spread(reshape([1,2,3,4],[2,2]),2,2)", 3U, cube, repeated, 8U);
    integers(unit, "spread([1,2],1,-7)", 2U, empty, NULL, 0U);
    integers(unit, "reshape([1],[0,2],order=[2,1])", 2U, empty, NULL, 0U);
    rejected(unit, "reshape([1],[2,3])");
    rejected(unit, "reshape([1,2],[2,1],order=[1,1])");
    rejected(unit, "reshape([1,2],[-1,2])");
    rejected(unit, "reshape([1],[9223372036854775807_8,9223372036854775807_8])");
    rejected(unit, "reshape([1_8],[2],pad=[2])");
    rejected(unit, "spread([1,2],0,1)");
    rejected(unit, "transpose([1,2])");
}

static void test_pack(Unit *unit) {
    const uint64_t two[] = {2U}, four[] = {4U}, matrix[] = {2U, 2U}, empty[] = {0U};
    const int64_t compact[] = {1, 4}, padded[] = {1, 4, 12, 13}, unpacked[] = {1, -7, -7, 4};
    integers(unit, "pack(reshape([1,2,3,4],[2,2]),reshape([.true.,.false.,.false.,.true.],[2,2]))",
             1U, two, compact, 2U);
    integers(unit, "pack([1,2,3,4],[.true.,.false.,.false.,.true.],vector=[10,11,12,13])", 1U, four,
             padded, 4U);
    integers(unit, "unpack([1,4],reshape([.true.,.false.,.false.,.true.],[2,2]),-7)", 2U, matrix,
             unpacked, 4U);
    integers(unit, "pack([1,2],.false.)", 1U, empty, NULL, 0U);
    rejected(unit, "pack([1,2],[.true.])");
    rejected(unit, "pack([1,2],.true.,vector=[7])");
    rejected(unit, "unpack([1],[.true.,.true.],0)");
    rejected(unit, "unpack([1],[.true.,.false.],[0])");
}

static void test_shift(Unit *unit) {
    const uint64_t matrix[] = {2U, 3U}, three[] = {3U}, empty[] = {0U, 2U};
    const int64_t shifted[] = {2, 1, 4, 3, 5, 6}, boundaries[] = {2, 9, 8, 3, 7, 7};
    const int64_t min_shift[] = {2, 3, 1}, zeros[] = {0, 0, 0};
    integers(unit, "cshift(reshape([1,2,3,4,5,6],[2,3]),[1,-1,0],dim=1)", 2U, matrix, shifted, 6U);
    integers(unit, "eoshift(reshape([1,2,3,4,5,6],[2,3]),[1,-1,3],boundary=[9,8,7],dim=1)", 2U,
             matrix, boundaries, 6U);
    integers(unit, "cshift([1,2,3],-9223372036854775807_8-1_8)", 1U, three, min_shift, 3U);
    integers(unit, "eoshift([1,2,3],-9223372036854775807_8-1_8)", 1U, three, zeros, 3U);
    integers(unit, "cshift(reshape([1],[0,2]),0)", 2U, empty, NULL, 0U);
    rejected(unit, "cshift(reshape([1,2,3,4],[2,2]),[1])");
    rejected(unit, "eoshift(reshape([1,2,3,4],[2,2]),1,boundary=[1])");
}

static void test_findloc(Unit *unit) {
    const uint64_t two[] = {2U}, three[] = {3U}, one[] = {1U};
    const int64_t first[] = {2, 1}, last[] = {1, 3}, slices[] = {2, 0, 1};
    const int64_t scalar[] = {3}, padded[] = {1}, missing[] = {0}, zero_slices[] = {0, 0};
    integers(unit, "findloc(reshape([1,2,3,4,2,6],[2,3]),2)", 1U, two, first, 2U);
    integers(unit, "findloc(reshape([1,2,3,4,2,6],[2,3]),2,back=.true.,kind=8)", 1U, two, last, 2U);
    integers(unit, "findloc(reshape([1,2,3,4,2,6],[2,3]),2,dim=1)", 1U, three, slices, 3U);
    integers(unit, "findloc([1,2,2],2,dim=1,back=.true.)", 0U, NULL, scalar, 1U);
    integers(unit, "findloc(['A ','B '],'A')", 1U, one, padded, 1U);
    integers(unit, "findloc([1,2],2,mask=.false.)", 1U, one, missing, 1U);
    integers(unit, "findloc(reshape([1],[0,2]),1,dim=1)", 1U, two, zero_slices, 2U);
    rejected(unit, "findloc([1,2],2,mask=[.true.])");
    rejected(unit, "findloc([1,2],2,kind=3)");
    rejected(unit, "findloc([1,2],2,dim=0)");
    int64_t folded = 0;
    F2cExpr *expression = parse(unit, "findloc([1,2,2],2,dim=1,back=.true.)+7");
    expect(expression != NULL && f2c_evaluate_integer_constant(unit, expression, &folded) &&
               folded == 10,
           "scalar FINDLOC composes with ordinary constant arithmetic");
    f2c_expr_free(expression);
}

static void test_characters(Unit *unit) {
    F2cConstantArray result = {0};
    expect(evaluate(unit, "eoshift(pack(['AA','BB','CC'],[.true.,.false.,.true.]),1)", &result) &&
               result.type.type == TYPE_CHARACTER && result.character_length == 2U &&
               result.count == 2U &&
               memcmp(result.values[0].payload.character.bytes, "CC", 2U) == 0 &&
               memcmp(result.values[1].payload.character.bytes, "  ", 2U) == 0,
           "character transformations preserve length and synthesize blank boundaries");
    F2cConstantArray copy = {0};
    expect(f2c_constant_array_copy(&copy, &result), "owned character arrays are deep copied");
    f2c_constant_array_free(&result);
    expect(copy.count == 2U && memcmp(copy.values[0].payload.character.bytes, "CC", 2U) == 0,
           "copied character payloads outlive their source");
    f2c_constant_array_free(&copy);
    rejected(unit, "['A','BB']");
    rejected(unit, "[]");
    rejected(unit, "[1_8,2_4]");
    rejected(unit, "reshape(['AA'],[2],pad=['B'])");
}

static void test_storage(Unit *unit) {
    Symbol storage = {0};
    storage.type = TYPE_INTEGER;
    storage.kind = 1;
    storage.rank = 2U;
    storage.dimensions[0].lower_expression = parse(unit, "-2");
    storage.dimensions[0].upper_expression = parse(unit, "-1");
    storage.dimensions[1].lower_expression = parse(unit, "4");
    storage.dimensions[1].upper_expression = parse(unit, "5");
    storage.initializer_expression = parse(unit, "reshape([1,2,3,4],[2,2])");
    F2cConstantArray result = {0};
    expect(f2c_evaluate_constant_storage(unit, &storage, &result) && result.type.kind == 1 &&
               result.shape.dimensions[0].lower == -2 && result.shape.dimensions[1].lower == 4 &&
               result.count == 4U && result.values[3].payload.integer == 4,
           "storage preserves declared bounds and converts every element to declared kind");
    f2c_constant_array_free(&result);
    f2c_expr_free(storage.initializer_expression);
    storage.initializer_expression = parse(unit, "[1,2,3,4]");
    expect(!f2c_evaluate_constant_storage(unit, &storage, &result),
           "equal element counts do not hide a declaration rank mismatch");
    f2c_expr_free(storage.initializer_expression);
    storage.initializer_expression = parse(unit, "reshape([1,2,3,4],[1,4])");
    expect(!f2c_evaluate_constant_storage(unit, &storage, &result),
           "equal element counts do not hide a declaration extent mismatch");
    f2c_expr_free(storage.initializer_expression);
    storage.initializer_expression = parse(unit, "128");
    expect(!f2c_evaluate_constant_storage(unit, &storage, &result),
           "broadcast conversion rejects a value outside the declared integer kind");
    f2c_expr_free(storage.initializer_expression);
    for (size_t axis = 0U; axis < 2U; ++axis) {
        f2c_expr_free(storage.dimensions[axis].lower_expression);
        f2c_expr_free(storage.dimensions[axis].upper_expression);
    }
}

static void test_budget(void) {
    Context context = {0};
    Unit unit = {.context = &context};
    F2cExpr *expression = parse(&unit, "spread(1,1,1000000000)");
    F2cConstantArray result = {0};
    context.limits.max_constant_steps = 100U;
    expect(!f2c_evaluate_constant_array(&unit, expression, &result) && result.values == NULL &&
               context.result.error_count == 1U,
           "large constant transformations fail before allocating outside the work budget");
    f2c_expr_free(expression);
    free(context.diagnostics.data);
}

static void test_shift_kernel(void) {
    const int64_t extremes[] = {INT64_MIN, INT64_MIN + 1, INT64_MAX - 1, INT64_MAX};
    for (size_t extent = 1U; extent < 8U; ++extent)
        for (size_t position = 0U; position < extent; ++position)
            for (size_t choice = 0U; choice < 25U; ++choice) {
                const int64_t amount = choice < 4U ? extremes[choice] : (int64_t)choice - 14;
                size_t result = SIZE_MAX;
                int64_t remainder = amount % (int64_t)extent;
                if (remainder < 0)
                    remainder += (int64_t)extent;
                const size_t expected = (position + (size_t)remainder) % extent;
                expect(f2c_array_shift_position(position, extent, amount, 1, &result) &&
                           result == expected,
                       "circular shift kernel handles every signed magnitude");
                const int inside =
                    amount >= -(int64_t)position && amount < (int64_t)(extent - position);
                expect(f2c_array_shift_position(position, extent, amount, 0, &result) == inside &&
                           (!inside || result == (size_t)((int64_t)position + amount)),
                       "end-off shift kernel never adds an unrepresentable signed shift");
            }
    size_t result = 7U;
    expect(!f2c_array_shift_position(0U, 0U, INT64_MIN, 1, &result) && result == 0U,
           "empty extent is checked before modulo");
    expect(!f2c_array_shift_position(2U, 2U, 0, 1, &result),
           "invalid source coordinates are rejected");
#if SIZE_MAX > UINT32_MAX
    expect(f2c_array_shift_position(SIZE_MAX - 1U, SIZE_MAX, INT64_MAX, 1, &result) &&
               result == (size_t)INT64_MAX - 1U,
           "unsigned index models do not require an extent to fit signed int64");
#endif
}

static void test_parameter_cache(void) {
    Unit unit = {0};
    Symbol symbols[3] = {0};
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    symbols[0].name = (char *)"rounded";
    symbols[0].type = TYPE_REAL;
    symbols[0].kind = 4;
    symbols[0].parameter = 1;
    symbols[0].rank = 1U;
    symbols[1].name = (char *)"promoted";
    symbols[1].type = TYPE_REAL;
    symbols[1].kind = 8;
    symbols[1].parameter = 1;
    symbols[1].rank = 1U;
    symbols[2].name = (char *)"cycle";
    symbols[2].type = TYPE_INTEGER;
    symbols[2].kind = 4;
    symbols[2].parameter = 1;
    symbols[2].rank = 1U;
    symbols[0].dimensions[0].lower_expression = parse(&unit, "-1");
    symbols[0].dimensions[0].upper_expression = parse(&unit, "-1");
    symbols[1].dimensions[0].upper_expression = parse(&unit, "2");
    symbols[2].dimensions[0].upper_expression = parse(&unit, "1");
    symbols[0].initializer_expression = parse(&unit, "[1.0000000596046448_8]");
    symbols[1].initializer_expression = parse(&unit, "[real(rounded(-1),8),real(rounded(-1),8)]");
    symbols[2].initializer_expression = parse(&unit, "[1]");
    F2cConstantArray result = {0};
    expect(evaluate(&unit, "promoted", &result) && result.count == 2U && result.type.kind == 8 &&
               result.values[0].payload.number.real == (double)(float)1.0000000596046448 &&
               result.values[1].payload.number.real == result.values[0].payload.number.real,
           "dependent parameter arrays observe conversion to the source parameter's declared kind");
    f2c_constant_array_free(&result);
    F2cExpr *root = parse(&unit, "cycle");
    F2cExpr *reference = parse(&unit, "cycle(1)");
    f2c_expr_free(symbols[2].dimensions[0].upper_expression);
    symbols[2].dimensions[0].upper_expression = reference;
    expect(
        !f2c_evaluate_constant_array(&unit, root, &result) && result.values == NULL,
        "cycles through parameter array specification bounds retain one shared evaluation cache");
    f2c_expr_free(root);
    for (size_t index = 0U; index < 3U; ++index) {
        f2c_expr_free(symbols[index].dimensions[0].lower_expression);
        f2c_expr_free(symbols[index].dimensions[0].upper_expression);
        f2c_expr_free(symbols[index].initializer_expression);
    }
}

int main(void) {
    Unit unit = {0};
    test_reorder(&unit);
    test_pack(&unit);
    test_shift(&unit);
    test_findloc(&unit);
    test_characters(&unit);
    test_storage(&unit);
    test_budget();
    test_parameter_cache();
    test_shift_kernel();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
