#include "core/generated/private.h"

void f2c_emit_numeric_model_contract(Buffer *output) {
    f2c_buffer_append(
        output,
        "_Static_assert(CHAR_BIT == 8, \"f2c INTEGER kind model requires 8-bit bytes\");\n"
        "_Static_assert(INT8_MAX == INT8_C(127), \"f2c INTEGER(1) model mismatch\");\n"
        "_Static_assert(INT16_MAX == INT16_C(32767), \"f2c INTEGER(2) model mismatch\");\n"
        "_Static_assert(INT32_MAX == INT32_C(2147483647), \"f2c INTEGER(4) model mismatch\");\n"
        "_Static_assert(INT64_MAX == INT64_C(9223372036854775807), "
        "\"f2c INTEGER(8) model mismatch\");\n"
        "_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_DIG == 6 && "
        "FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128 && FLT_MIN_10_EXP == -37, "
        "\"f2c REAL(4) model requires IEEE binary32\");\n"
        "_Static_assert(FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_DIG == 15 && "
        "DBL_MIN_EXP == -1021 && DBL_MAX_EXP == 1024 && DBL_MIN_10_EXP == -307, "
        "\"f2c REAL(8) model requires IEEE binary64\");\n");
}

void f2c_emit_numeric_model_support(Buffer *output) {
    f2c_buffer_append(
        output, "static inline F2C_UNUSED int32_t f2c_selected_int_kind(int64_t range) { "
                "if (range <= INT64_C(2)) return INT32_C(1); "
                "if (range <= INT64_C(4)) return INT32_C(2); "
                "if (range <= INT64_C(9)) return INT32_C(4); "
                "if (range <= INT64_C(18)) return INT32_C(8); return -INT32_C(1); }\n"
                "static inline F2C_UNUSED int32_t f2c_selected_real_kind("
                "int64_t precision, bool has_precision, int64_t range, bool has_range, "
                "int64_t radix, bool has_radix) { bool precision_supported; bool range_supported; "
                "if (has_radix && radix != INT64_C(2)) return -INT32_C(5); "
                "if ((!has_precision || precision <= INT64_C(6)) && "
                "(!has_range || range <= INT64_C(37))) return INT32_C(4); "
                "if ((!has_precision || precision <= INT64_C(15)) && "
                "(!has_range || range <= INT64_C(307))) return INT32_C(8); "
                "precision_supported = !has_precision || precision <= INT64_C(15); "
                "range_supported = !has_range || range <= INT64_C(307); "
                "if (!precision_supported && !range_supported) return -INT32_C(3); "
                "if (!precision_supported) return -INT32_C(1); "
                "if (!range_supported) return -INT32_C(2); return -INT32_C(4); }\n");
}
