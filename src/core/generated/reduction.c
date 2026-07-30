#include "core/generated/private.h"

void f2c_emit_reduction_support(Buffer *output, int needs_complex) {
    f2c_buffer_append(
        output,
        "#define F2C_DEFINE_REDUCTIONS(s, t, zero, one, low, high) "
        "static inline F2C_UNUSED t f2c_sum_##s(const t *v, size_t n, ptrdiff_t d) { "
        "size_t i; t r = (zero); for (i = 0U; i < n; ++i) r = (t)(r + v[(ptrdiff_t)i "
        "* d]); return r; } "
        "static inline F2C_UNUSED t f2c_product_##s(const t *v, size_t n, ptrdiff_t d) "
        "{ size_t i; t r = (one); for (i = 0U; i < n; ++i) r = (t)(r * "
        "v[(ptrdiff_t)i * d]); return r; } "
        "static inline F2C_UNUSED t f2c_maxval_##s(const t *v, size_t n, ptrdiff_t d) { "
        "size_t i; t r = (low); for (i = 0U; i < n; ++i) if (v[(ptrdiff_t)i * d] > r) "
        "r = v[(ptrdiff_t)i * d]; return r; } "
        "static inline F2C_UNUSED t f2c_minval_##s(const t *v, size_t n, ptrdiff_t d) { "
        "size_t i; t r = (high); for (i = 0U; i < n; ++i) if (v[(ptrdiff_t)i * d] < r) "
        "r = v[(ptrdiff_t)i * d]; return r; } "
        "static inline F2C_UNUSED int32_t f2c_maxloc_##s(const t *v, size_t n, "
        "ptrdiff_t d) { size_t i; int32_t p = n != 0U ? 1 : 0; for (i = 1U; i < n; "
        "++i) if (v[(ptrdiff_t)i * d] > v[(ptrdiff_t)(p - 1) * d]) p = (int32_t)i + "
        "1; return p; } "
        "static inline F2C_UNUSED int32_t f2c_minloc_##s(const t *v, size_t n, "
        "ptrdiff_t d) { size_t i; int32_t p = n != 0U ? 1 : 0; for (i = 1U; i < n; "
        "++i) if (v[(ptrdiff_t)i * d] < v[(ptrdiff_t)(p - 1) * d]) p = (int32_t)i + "
        "1; return p; }\n"
        "F2C_DEFINE_REDUCTIONS(i8, int8_t, INT8_C(0), INT8_C(1), INT8_MIN, INT8_MAX)\n"
        "F2C_DEFINE_REDUCTIONS(i16, int16_t, INT16_C(0), INT16_C(1), INT16_MIN, "
        "INT16_MAX)\n"
        "F2C_DEFINE_REDUCTIONS(i32, int32_t, INT32_C(0), INT32_C(1), INT32_MIN, "
        "INT32_MAX)\n"
        "F2C_DEFINE_REDUCTIONS(i64, int64_t, INT64_C(0), INT64_C(1), INT64_MIN, "
        "INT64_MAX)\n"
        "F2C_DEFINE_REDUCTIONS(f, float, 0.0f, 1.0f, -HUGE_VALF, HUGE_VALF)\n"
        "F2C_DEFINE_REDUCTIONS(d, double, 0.0, 1.0, -HUGE_VAL, HUGE_VAL)\n"
        "#undef F2C_DEFINE_REDUCTIONS\n");
    if (needs_complex) {
        f2c_buffer_append(
            output,
            "static inline F2C_UNUSED f2c_complex_float f2c_sum_c(const f2c_complex_float *v, "
            "size_t n, ptrdiff_t d) { size_t i; f2c_complex_float r = f2c_make_c(0.0f, 0.0f); "
            "for (i = 0U; i < n; ++i) r = f2c_cadd(r, v[(ptrdiff_t)i * d]); return r; }\n"
            "static inline F2C_UNUSED f2c_complex_double f2c_sum_z(const f2c_complex_double *v, "
            "size_t n, ptrdiff_t d) { size_t i; f2c_complex_double r = f2c_make_z(0.0, 0.0); "
            "for (i = 0U; i < n; ++i) r = f2c_zadd(r, v[(ptrdiff_t)i * d]); return r; }\n"
            "static inline F2C_UNUSED f2c_complex_float f2c_product_c("
            "const f2c_complex_float *v, size_t n, ptrdiff_t d) { size_t i; "
            "f2c_complex_float r = f2c_make_c(1.0f, 0.0f); for (i = 0U; i < n; ++i) "
            "r = f2c_cmul(r, v[(ptrdiff_t)i * d]); return r; }\n"
            "static inline F2C_UNUSED f2c_complex_double f2c_product_z("
            "const f2c_complex_double *v, size_t n, ptrdiff_t d) { size_t i; "
            "f2c_complex_double r = f2c_make_z(1.0, 0.0); for (i = 0U; i < n; ++i) "
            "r = f2c_zmul(r, v[(ptrdiff_t)i * d]); return r; }\n");
    }
    f2c_buffer_append(
        output,
        "enum { F2C_REDUCTION_I8 = 1, F2C_REDUCTION_I16, F2C_REDUCTION_I32, "
        "F2C_REDUCTION_I64, F2C_REDUCTION_F, F2C_REDUCTION_D, F2C_REDUCTION_C, "
        "F2C_REDUCTION_Z };\n"
        "static inline F2C_UNUSED int64_t f2c_reduction_integer_at(const void *v, int type, "
        "ptrdiff_t i) { switch (type) { case F2C_REDUCTION_I8: return ((const int8_t *)v)[i]; "
        "case F2C_REDUCTION_I16: return ((const int16_t *)v)[i]; case F2C_REDUCTION_I32: "
        "return ((const int32_t *)v)[i]; case F2C_REDUCTION_I64: return ((const int64_t *)v)[i]; "
        "default: abort(); } }\n"
        "static inline F2C_UNUSED double f2c_reduction_real_at(const void *v, int type, "
        "ptrdiff_t i) { switch (type) { case F2C_REDUCTION_I8: return ((const int8_t *)v)[i]; "
        "case F2C_REDUCTION_I16: return ((const int16_t *)v)[i]; case F2C_REDUCTION_I32: "
        "return ((const int32_t *)v)[i]; case F2C_REDUCTION_I64: return (double)((const "
        "int64_t *)v)[i]; case F2C_REDUCTION_F: return ((const float *)v)[i]; case "
        "F2C_REDUCTION_D: return ((const double *)v)[i]; ");
    if (needs_complex)
        f2c_buffer_append(output,
                          "case F2C_REDUCTION_C: return crealf(((const f2c_complex_float *)v)[i]); "
                          "case F2C_REDUCTION_Z: return creal(((const f2c_complex_double *)v)[i]); ");
    f2c_buffer_append(
        output,
        "default: abort(); } }\n"
        "static inline F2C_UNUSED double f2c_reduction_imaginary_at(const void *v, int type, "
        "ptrdiff_t i) { ");
    if (needs_complex)
        f2c_buffer_append(output,
                          "if (type == F2C_REDUCTION_C) return "
                          "cimagf(((const f2c_complex_float *)v)[i]); "
                          "if (type == F2C_REDUCTION_Z) return "
                          "cimag(((const f2c_complex_double *)v)[i]); ");
    f2c_buffer_append(
        output,
        "(void)v; (void)type; (void)i; return 0.0; }\n"
        "#define F2C_DEFINE_INTEGER_DOT(s, t) static inline F2C_UNUSED t f2c_dot_##s("
        "const void *a, int at, ptrdiff_t ad, const void *b, int bt, ptrdiff_t bd, size_t n) "
        "{ size_t i; t r = 0; for (i = 0U; i < n; ++i) r = (t)(r + "
        "(t)f2c_reduction_integer_at(a, at, (ptrdiff_t)i * ad) * "
        "(t)f2c_reduction_integer_at(b, bt, (ptrdiff_t)i * bd)); return r; }\n"
        "F2C_DEFINE_INTEGER_DOT(i8, int8_t)\n"
        "F2C_DEFINE_INTEGER_DOT(i16, int16_t)\n"
        "F2C_DEFINE_INTEGER_DOT(i32, int32_t)\n"
        "F2C_DEFINE_INTEGER_DOT(i64, int64_t)\n"
        "#undef F2C_DEFINE_INTEGER_DOT\n"
        "static inline F2C_UNUSED float f2c_dot_f(const void *a, int at, ptrdiff_t ad, "
        "const void *b, int bt, ptrdiff_t bd, size_t n) { size_t i; float r = 0.0f; "
        "for (i = 0U; i < n; ++i) r += "
        "(float)f2c_reduction_real_at(a, at, (ptrdiff_t)i * ad) * "
        "(float)f2c_reduction_real_at(b, bt, (ptrdiff_t)i * bd); return r; }\n"
        "static inline F2C_UNUSED double f2c_dot_d(const void *a, int at, ptrdiff_t ad, "
        "const void *b, int bt, ptrdiff_t bd, size_t n) { size_t i; double r = 0.0; "
        "for (i = 0U; i < n; ++i) r += "
        "f2c_reduction_real_at(a, at, (ptrdiff_t)i * ad) * "
        "f2c_reduction_real_at(b, bt, (ptrdiff_t)i * bd); return r; }\n");
    if (needs_complex) {
        f2c_buffer_append(
            output,
            "static inline F2C_UNUSED f2c_complex_float f2c_dot_c(const void *a, int at, "
            "ptrdiff_t ad, const void *b, int bt, ptrdiff_t bd, size_t n) { size_t i; "
            "float rr = 0.0f, ri = 0.0f; for (i = 0U; i < n; ++i) { ptrdiff_t ai = "
            "(ptrdiff_t)i * ad, bi = (ptrdiff_t)i * bd; float ar = "
            "(float)f2c_reduction_real_at(a, at, ai), av = "
            "(float)f2c_reduction_imaginary_at(a, at, ai), br = "
            "(float)f2c_reduction_real_at(b, bt, bi), bv = "
            "(float)f2c_reduction_imaginary_at(b, bt, bi); rr += ar * br + av * bv; "
            "ri += ar * bv - av * br; } return f2c_make_c(rr, ri); }\n"
            "static inline F2C_UNUSED f2c_complex_double f2c_dot_z(const void *a, int at, "
            "ptrdiff_t ad, const void *b, int bt, ptrdiff_t bd, size_t n) { size_t i; "
            "double rr = 0.0, ri = 0.0; for (i = 0U; i < n; ++i) { ptrdiff_t ai = "
            "(ptrdiff_t)i * ad, bi = (ptrdiff_t)i * bd; double ar = "
            "f2c_reduction_real_at(a, at, ai), av = "
            "f2c_reduction_imaginary_at(a, at, ai), br = "
            "f2c_reduction_real_at(b, bt, bi), bv = "
            "f2c_reduction_imaginary_at(b, bt, bi); rr += ar * br + av * bv; "
            "ri += ar * bv - av * br; } return f2c_make_z(rr, ri); }\n");
    }
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED bool f2c_reduction_logical_at(const void *v, size_t size, "
        "ptrdiff_t i) { switch (size) { case 1U: return ((const int8_t *)v)[i] != 0; "
        "case 2U: return ((const int16_t *)v)[i] != 0; case 4U: return "
        "((const int32_t *)v)[i] != 0; case 8U: return ((const int64_t *)v)[i] != 0; "
        "default: abort(); } }\n"
        "static inline F2C_UNUSED int64_t f2c_count_l(const void *v, size_t size, size_t n, "
        "ptrdiff_t d) { size_t i; int64_t r = 0; for (i = 0U; i < n; ++i) if "
        "(f2c_reduction_logical_at(v, size, (ptrdiff_t)i * d)) ++r; return r; }\n"
        "static inline F2C_UNUSED bool f2c_any_l(const void *v, size_t size, size_t n, "
        "ptrdiff_t d) { size_t i; for (i = 0U; i < n; ++i) if "
        "(f2c_reduction_logical_at(v, size, (ptrdiff_t)i * d)) return true; return false; }\n"
        "static inline F2C_UNUSED bool f2c_all_l(const void *v, size_t size, size_t n, "
        "ptrdiff_t d) { size_t i; for (i = 0U; i < n; ++i) if "
        "(!f2c_reduction_logical_at(v, size, (ptrdiff_t)i * d)) return false; return true; }\n"
        "static inline F2C_UNUSED bool f2c_dot_l(const void *a, size_t as, ptrdiff_t ad, "
        "const void *b, size_t bs, ptrdiff_t bd, size_t n) { size_t i; "
        "for (i = 0U; i < n; ++i) if (f2c_reduction_logical_at(a, as, (ptrdiff_t)i * ad) && "
        "f2c_reduction_logical_at(b, bs, (ptrdiff_t)i * bd)) return true; return false; }\n");
    f2c_buffer_append(
        output,
        needs_complex
            ? "#define F2C_SUM(v, n, d) _Generic(*(v), int8_t: f2c_sum_i8, int16_t: "
              "f2c_sum_i16, int32_t: f2c_sum_i32, int64_t: f2c_sum_i64, float: f2c_sum_f, "
              "double: f2c_sum_d, f2c_complex_float: f2c_sum_c, f2c_complex_double: "
              "f2c_sum_z)((v), (n), (d))\n"
              "#define F2C_PRODUCT(v, n, d) _Generic(*(v), int8_t: f2c_product_i8, int16_t: "
              "f2c_product_i16, int32_t: f2c_product_i32, int64_t: f2c_product_i64, float: "
              "f2c_product_f, double: f2c_product_d, f2c_complex_float: f2c_product_c, "
              "f2c_complex_double: f2c_product_z)((v), (n), (d))\n"
            : "#define F2C_SUM(v, n, d) _Generic(*(v), int8_t: f2c_sum_i8, int16_t: "
              "f2c_sum_i16, int32_t: f2c_sum_i32, int64_t: f2c_sum_i64, float: f2c_sum_f, "
              "double: f2c_sum_d)((v), (n), (d))\n"
              "#define F2C_PRODUCT(v, n, d) _Generic(*(v), int8_t: f2c_product_i8, int16_t: "
              "f2c_product_i16, int32_t: f2c_product_i32, int64_t: f2c_product_i64, float: "
              "f2c_product_f, double: f2c_product_d)((v), (n), (d))\n");
    f2c_buffer_append(
        output,
        "#define F2C_MAXIMUM(v, n, d) _Generic(*(v), int8_t: f2c_maxval_i8, int16_t: "
        "f2c_maxval_i16, int32_t: f2c_maxval_i32, int64_t: f2c_maxval_i64, float: "
        "f2c_maxval_f, double: f2c_maxval_d)((v), (n), (d))\n"
        "#define F2C_MINIMUM(v, n, d) _Generic(*(v), int8_t: f2c_minval_i8, int16_t: "
        "f2c_minval_i16, int32_t: f2c_minval_i32, int64_t: f2c_minval_i64, float: "
        "f2c_minval_f, double: f2c_minval_d)((v), (n), (d))\n"
        "#define F2C_MAXIMUM_LOCATION(v, n, d) _Generic(*(v), int8_t: f2c_maxloc_i8, "
        "int16_t: f2c_maxloc_i16, int32_t: f2c_maxloc_i32, int64_t: f2c_maxloc_i64, "
        "float: f2c_maxloc_f, double: f2c_maxloc_d)((v), (n), (d))\n"
        "#define F2C_MINIMUM_LOCATION(v, n, d) _Generic(*(v), int8_t: f2c_minloc_i8, "
        "int16_t: f2c_minloc_i16, int32_t: f2c_minloc_i32, int64_t: f2c_minloc_i64, "
        "float: f2c_minloc_f, double: f2c_minloc_d)((v), (n), (d))\n");
    f2c_emit_relation_reduction_support(output, needs_complex);
}
