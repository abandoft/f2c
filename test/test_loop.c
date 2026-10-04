#include "core/numeric/loop.h"
#include "internal/f2c.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition && failures++ < 12)
        fprintf(stderr, "FAIL: %s\n", message);
}

static void test_small_intervals(void) {
    for (int64_t first = -127; first <= 127; ++first) {
        for (int64_t last = -127; last <= 127; ++last) {
            for (int64_t step = -127; step <= 127; step += 3) {
                uint64_t remaining = UINT64_MAX;
                const int active = f2c_integer_loop_begin(first, last, step, &remaining);
                const int64_t count = (last - first + step) / step;
                const int nonempty = step > 0 ? first <= last : first >= last;
                expect(active == nonempty, "all small intervals have the correct active state");
                if (nonempty)
                    expect(remaining + 1U == (uint64_t)count,
                           "small trip counts match independent widened signed arithmetic");
                else
                    expect(remaining == 0U, "zero-trip loops have no remaining iterations");
            }
        }
    }
}

static void test_wide_intervals(void) {
    static const struct {
        int64_t first;
        int64_t last;
        int64_t step;
        int active;
        uint64_t remaining;
    } cases[] = {
        {INT64_MIN, INT64_MAX, 1, 1, UINT64_MAX},
        {INT64_MAX, INT64_MIN, -1, 1, UINT64_MAX},
        {INT64_MIN, INT64_MAX, INT64_MAX, 1, 2},
        {INT64_MAX, INT64_MIN, INT64_MIN, 1, 1},
        {0, INT64_MIN, INT64_MIN, 1, 1},
        {INT64_MIN, INT64_MIN, INT64_MIN, 1, 0},
        {INT64_MIN, INT64_MAX, -1, 0, 0},
        {INT64_MAX, INT64_MIN, 1, 0, 0},
        {4294967296LL, 4294967300LL, 2, 1, 2},
        {-4294967296LL, -4294967300LL, -2, 1, 2},
        {1, 2, 0, -1, 0},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint64_t remaining = 17U;
        const int active = f2c_integer_loop_begin(cases[index].first, cases[index].last,
                                                  cases[index].step, &remaining);
        expect(active == cases[index].active && remaining == cases[index].remaining,
               "64-bit distances, minimum negative steps and full-width intervals are exact");
    }
    uint64_t count;
    expect(!f2c_integer_iteration_count(INT64_MIN, INT64_MAX, 1, &count),
           "constant expansion rejects a cardinality that cannot be materialized");
    expect(f2c_integer_iteration_count(INT64_MIN, INT64_MAX, 2, &count) &&
               count == ((uint64_t)INT64_MAX + 1U),
           "constant expansion shares the exact nonoverflowing loop model");
    expect(f2c_integer_loop_parameters_fit(1, -100, 100, 17) &&
               !f2c_integer_loop_parameters_fit(1, -100, 128, 17) &&
               f2c_integer_loop_parameters_fit(8, INT64_MIN, INT64_MAX, INT64_MIN) &&
               !f2c_integer_loop_parameters_fit(16, 1, 3, 1),
           "constant loop controls must be representable in the selected integer storage kind");
}

static void test_advancement(void) {
    for (int64_t value = INT8_MIN; value <= INT8_MAX; ++value) {
        for (int64_t step = INT8_MIN; step <= INT8_MAX; ++step) {
            int64_t expected = value + step;
            if (expected < INT8_MIN)
                expected += 256;
            else if (expected > INT8_MAX)
                expected -= 256;
            expect(f2c_integer_loop_add(value, step, INT8_MIN, INT8_MAX) == expected,
                   "exhaustive narrow advancement has explicit modular storage semantics");
            expect(f2c_integer_loop_add_i8((int8_t)value, (int8_t)step) == expected,
                   "branch-free typed advancement matches every narrow arithmetic reference");
        }
    }
    expect(f2c_integer_loop_add(INT64_MAX, 1, INT64_MIN, INT64_MAX) == INT64_MIN,
           "wide final positive overflow is defined without C signed overflow");
    expect(f2c_integer_loop_add(INT64_MIN, -1, INT64_MIN, INT64_MAX) == INT64_MAX,
           "wide final negative overflow is defined without out-of-range signed casts");
    expect(f2c_integer_loop_add(INT64_MAX, INT64_MIN, INT64_MIN, INT64_MAX) == -1,
           "minimum negative increment is never negated in signed arithmetic");
    expect(f2c_integer_loop_add(INT32_MAX, 1, INT32_MIN, INT32_MAX) == INT32_MIN &&
               f2c_integer_loop_add(INT16_MIN, -1, INT16_MIN, INT16_MAX) == INT16_MAX,
           "every supported integer storage kind uses the same advancement contract");
    expect(f2c_integer_loop_add_i64(INT64_MAX, 1) == INT64_MIN &&
               f2c_integer_loop_add_i64(INT64_MIN, -1) == INT64_MAX &&
               f2c_integer_loop_add_i64(INT64_MAX, INT64_MIN) == -1 &&
               f2c_integer_loop_add_i32(INT32_MAX, 1) == INT32_MIN &&
               f2c_integer_loop_add_i16(INT16_MIN, -1) == INT16_MAX,
           "branch-free typed updates retain full-width boundary behavior");
    expect(f2c_integer_loop_value_i32((int64_t)INT32_MAX + INT32_MAX) == -2 &&
               f2c_integer_loop_value_i32((int64_t)INT32_MIN + INT32_MIN) == 0 &&
               f2c_integer_loop_value_i32((int64_t)INT32_MAX + 1) == INT32_MIN &&
               f2c_integer_loop_value_i32((int64_t)INT32_MIN - 1) == INT32_MAX,
           "wide default-integer final indices preserve the modular storage policy");
}

static void test_default_integer_induction_proof(void) {
    static const int32_t endpoints[] = {INT32_MIN, INT32_MIN + 1, -65536,        -1,       0,
                                        1,         65536,         INT32_MAX - 1, INT32_MAX};
    static const int32_t strides[] = {INT32_MIN, -INT32_MAX, -5, -2, -1, 1, 2, 5, INT32_MAX};
    for (size_t first = 0U; first < sizeof(endpoints) / sizeof(endpoints[0]); ++first) {
        for (size_t last = 0U; last < sizeof(endpoints) / sizeof(endpoints[0]); ++last) {
            for (size_t stride = 0U; stride < sizeof(strides) / sizeof(strides[0]); ++stride) {
                uint64_t remaining;
                const int active = f2c_integer_loop_begin(endpoints[first], endpoints[last],
                                                          strides[stride], &remaining);
                const int64_t count = active > 0 ? (int64_t)(remaining + 1U) : 0;
                const int64_t final = (int64_t)endpoints[first] + count * (int64_t)strides[stride];
                expect(count >= 0 && (uint64_t)count <= (uint64_t)UINT32_MAX + 1U,
                       "default-integer intervals have at most 2^32 iterations");
                expect(final >= (int64_t)INT32_MIN + INT32_MIN &&
                           final <= (int64_t)INT32_MAX + INT32_MAX,
                       "the final mathematical value fits the proven widened setup domain");
                if (active > 0) {
                    const int64_t last_active = final - (int64_t)strides[stride];
                    expect(last_active >= INT32_MIN && last_active <= INT32_MAX,
                           "the last active induction value is always source-width representable");
                    if (count > 1) {
                        const int64_t next = (int64_t)endpoints[first] + strides[stride];
                        expect(next >= INT32_MIN && next <= INT32_MAX,
                               "nonfinal active updates cannot overflow signed source storage");
                    }
                    if (final >= INT32_MIN && final <= INT32_MAX)
                        expect(f2c_integer_loop_add_i32((int32_t)last_active, strides[stride]) ==
                                   final,
                               "the invariant signed fast path agrees with defined-width updates");
                }
            }
        }
    }
}

static void test_diagnostics(void) {
    static const struct {
        const char *source;
        const char *message;
    } cases[] = {
        {"program bad\ninteger :: iterator\ndo iterator=1,3,0\nend do\nend program\n",
         "counted DO step cannot be zero"},
        {"program bad\ninteger :: iterator\ndo iterator=1,3,0.5\nend do\nend program\n",
         "counted DO step cannot be zero after conversion"},
        {"program bad\ninteger :: iterator(2)\ndo iterator(1)=1,3\nend do\nend program\n",
         "counted DO variable must be a definable scalar"},
        {"program bad\ninteger :: iterator\ndo iterator=1,[2,3]\nend do\nend program\n",
         "counted DO initial value, limit, and step must be scalar"},
        {"program bad\ninteger(kind=1) :: iterator\ndo iterator=1,128_8\nend do\nend program\n",
         "counted DO control exceeds the iteration variable kind 1"},
        {"program bad\ninteger :: iterator\ndo iterator=1,1.0d300\nend do\nend program\n",
         "counted DO control exceeds the iteration variable kind 4"},
        {"program bad\ninteger(kind=1) :: iterator, values(3)\n"
         "values=[(iterator,iterator=126_8,128_8)]\nend program\n",
         "array-constructor limit exceeds the iteration variable kind 1"},
        {"program bad\ninteger(kind=1) :: iterator\ncharacter(len=32) :: record\n"
         "write(record,*) (iterator,iterator=126_8,128_8)\nend program\n",
         "I/O implied-DO limit exceeds the iteration variable kind 1"},
    };
    const F2cOptions options = {"loop-invalid.f90", F2C_SOURCE_FREE, 0};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        F2cResult result =
            f2c_transpile(cases[index].source, strlen(cases[index].source), &options);
        expect(result.error_count != 0U && result.diagnostics != NULL &&
                   strstr(result.diagnostics, cases[index].message) != NULL,
               "invalid loop controls have explicit semantic diagnostics");
        expect(result.code == NULL || result.code[0] == '\0',
               "invalid controls do not leak partial generated output");
        f2c_result_free(&result);
    }
}

int main(void) {
    test_small_intervals();
    test_wide_intervals();
    test_advancement();
    test_default_integer_induction_proof();
    test_diagnostics();
    if (failures != 0)
        return 1;
    puts("integer loop count and advancement contracts passed");
    return 0;
}
