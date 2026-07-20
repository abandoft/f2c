#include "semantic/numeric_model.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_integer_models(void) {
    static const int kinds[] = {1, 2, 4, 8};
    static const int digits[] = {7, 15, 31, 63};
    static const int ranges[] = {2, 4, 9, 18};
    static const int64_t huge[] = {INT8_MAX, INT16_MAX, INT32_MAX, INT64_MAX};
    size_t index;
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        const F2cNumericModel *model = f2c_numeric_model(TYPE_INTEGER, kinds[index]);
        expect(model != NULL, "supported INTEGER kind has a numeric model");
        if (model == NULL)
            continue;
        expect(model->radix == 2, "INTEGER model uses radix two");
        expect(model->digits == digits[index], "INTEGER model has the expected value bits");
        expect(model->range == ranges[index], "INTEGER model has the expected decimal range");
        expect(model->integer_huge == huge[index], "INTEGER model has the expected maximum");
    }
    expect(f2c_numeric_model(TYPE_INTEGER, 3) == NULL,
           "unsupported INTEGER kinds do not acquire an accidental model");
}

static void test_real_models(void) {
    const F2cNumericModel *single = f2c_numeric_model(TYPE_REAL, 4);
    const F2cNumericModel *double_precision = f2c_numeric_model(TYPE_DOUBLE, 8);
    expect(single != NULL && single->digits == 24 && single->precision == 6 &&
               single->range == 37 && single->min_exponent == -125 &&
               single->max_exponent == 128,
           "REAL(4) uses the binary32 model");
    expect(double_precision != NULL && double_precision->digits == 53 &&
               double_precision->precision == 15 && double_precision->range == 307 &&
               double_precision->min_exponent == -1021 &&
               double_precision->max_exponent == 1024,
           "REAL(8) uses the binary64 model");
    expect(f2c_numeric_model(TYPE_COMPLEX, 4) == single,
           "COMPLEX(4) shares its component numeric model");
    expect(f2c_numeric_model(TYPE_DOUBLE_COMPLEX, 8) == double_precision,
           "COMPLEX(8) shares its component numeric model");
    expect(f2c_numeric_model(TYPE_REAL, 16) == NULL,
           "unimplemented extended REAL kinds are explicit rather than host-dependent");
}

static void test_kind_selection(void) {
    expect(f2c_selected_int_kind_value(-1) == 1, "negative integer range selects the smallest kind");
    expect(f2c_selected_int_kind_value(2) == 1, "two decimal digits select INTEGER(1)");
    expect(f2c_selected_int_kind_value(3) == 2, "three decimal digits select INTEGER(2)");
    expect(f2c_selected_int_kind_value(9) == 4, "nine decimal digits select INTEGER(4)");
    expect(f2c_selected_int_kind_value(10) == 8, "ten decimal digits select INTEGER(8)");
    expect(f2c_selected_int_kind_value(19) == -1, "unavailable integer range returns minus one");

    expect(f2c_selected_real_kind_value(6, 1, 0, 0, 0, 0) == 4,
           "six decimal digits select REAL(4)");
    expect(f2c_selected_real_kind_value(7, 1, 0, 0, 0, 0) == 8,
           "seven decimal digits select REAL(8)");
    expect(f2c_selected_real_kind_value(6, 1, 38, 1, 0, 0) == 8,
           "combined precision and range select the smallest matching model");
    expect(f2c_selected_real_kind_value(16, 1, 37, 1, 0, 0) == -1,
           "unavailable precision returns minus one");
    expect(f2c_selected_real_kind_value(6, 1, 308, 1, 0, 0) == -2,
           "unavailable exponent range returns minus two");
    expect(f2c_selected_real_kind_value(16, 1, 308, 1, 0, 0) == -3,
           "unavailable precision and range return minus three");
    expect(f2c_selected_real_kind_value(6, 1, 37, 1, 10, 1) == -5,
           "an unavailable radix returns minus five");
}

int main(void) {
    test_integer_models();
    test_real_models();
    test_kind_selection();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
