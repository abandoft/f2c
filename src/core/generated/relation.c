#include "core/generated/private.h"

void f2c_emit_relation_reduction_support(Buffer *output, int needs_complex) {
    f2c_buffer_append(
        output, "#define F2C_DEFINE_RELATION_REDUCTION(s, t) "
                "static inline F2C_UNUSED int32_t f2c_relation_reduce_##s(const t *a, "
                "ptrdiff_t ad, size_t an, const t *b, ptrdiff_t bd, size_t bn, int relation, "
                "int reduction) { size_t i, n; int32_t count = 0; if (an == SIZE_MAX && bn == "
                "SIZE_MAX) abort(); if (an != SIZE_MAX && bn != SIZE_MAX && an != bn) abort(); "
                "n = an == SIZE_MAX ? bn : an; for (i = 0U; i < n; ++i) { t av = "
                "a[(ptrdiff_t)i * ad]; t bv = b[(ptrdiff_t)i * bd]; bool value; switch "
                "(relation) { case 0: value = av == bv; break; case 1: value = av != bv; break; "
                "case 2: value = av < bv; break; case 3: value = av <= bv; break; case 4: value "
                "= av > bv; break; case 5: value = av >= bv; break; default: abort(); } if "
                "(reduction == 0 && value) return 1; if (reduction == 1 && !value) return 0; if "
                "(reduction == 2 && value) ++count; } if (reduction == 0) return 0; if "
                "(reduction == 1) return 1; if (reduction == 2) return count; abort(); }\n"
                "F2C_DEFINE_RELATION_REDUCTION(i8, int8_t)\n"
                "F2C_DEFINE_RELATION_REDUCTION(i16, int16_t)\n"
                "F2C_DEFINE_RELATION_REDUCTION(i32, int32_t)\n"
                "F2C_DEFINE_RELATION_REDUCTION(i64, int64_t)\n"
                "F2C_DEFINE_RELATION_REDUCTION(f, float)\n"
                "F2C_DEFINE_RELATION_REDUCTION(d, double)\n"
                "#undef F2C_DEFINE_RELATION_REDUCTION\n");
    if (needs_complex) {
        f2c_buffer_append(
            output, "#define F2C_DEFINE_COMPLEX_RELATION_REDUCTION(s, t) "
                    "static inline F2C_UNUSED int32_t f2c_relation_reduce_##s(const t *a, "
                    "ptrdiff_t ad, size_t an, const t *b, ptrdiff_t bd, size_t bn, int relation, "
                    "int reduction) { size_t i, n; int32_t count = 0; if (an == SIZE_MAX && bn "
                    "== SIZE_MAX) abort(); if (an != SIZE_MAX && bn != SIZE_MAX && an != bn) "
                    "abort(); n = an == SIZE_MAX ? bn : an; for (i = 0U; i < n; ++i) { bool "
                    "equal = a[(ptrdiff_t)i * ad] == b[(ptrdiff_t)i * bd]; bool value; if "
                    "(relation == 0) value = equal; else if (relation == 1) value = !equal; else "
                    "abort(); if (reduction == 0 && value) return 1; if (reduction == 1 && "
                    "!value) return 0; if (reduction == 2 && value) ++count; } if (reduction == "
                    "0) return 0; if (reduction == 1) return 1; if (reduction == 2) return count; "
                    "abort(); }\n"
                    "F2C_DEFINE_COMPLEX_RELATION_REDUCTION(c, f2c_complex_float)\n"
                    "F2C_DEFINE_COMPLEX_RELATION_REDUCTION(z, f2c_complex_double)\n"
                    "#undef F2C_DEFINE_COMPLEX_RELATION_REDUCTION\n");
    }
    f2c_buffer_append(output,
                      "#define F2C_RELATION_REDUCE(a, ad, an, b, bd, bn, relation, reduction) "
                      "_Generic(*(a), int8_t: f2c_relation_reduce_i8, int16_t: "
                      "f2c_relation_reduce_i16, int32_t: f2c_relation_reduce_i32, int64_t: "
                      "f2c_relation_reduce_i64, float: f2c_relation_reduce_f, double: "
                      "f2c_relation_reduce_d");
    if (needs_complex)
        f2c_buffer_append(output, ", f2c_complex_float: f2c_relation_reduce_c, "
                                  "f2c_complex_double: f2c_relation_reduce_z");
    f2c_buffer_append(output, ")((a), (ad), (an), (b), (bd), (bn), (relation), (reduction))\n");
    f2c_buffer_append(
        output, "static inline F2C_UNUSED int32_t f2c_character_relation_reduce(const void *a, "
                "ptrdiff_t ad, size_t an, size_t al, int av, const void *b, ptrdiff_t bd, "
                "size_t bn, size_t bl, int bv, int relation, int reduction) { size_t i, n; "
                "int32_t count = 0; if (an == SIZE_MAX && bn == SIZE_MAX) abort(); if (an != "
                "SIZE_MAX && bn != SIZE_MAX && an != bn) abort(); n = an == SIZE_MAX ? bn : an; "
                "for (i = 0U; i < n; ++i) { const char *ap = av ? ((const char *const *)a)"
                "[(ptrdiff_t)i * ad] : (const char *)a + (ptrdiff_t)i * ad * (ptrdiff_t)al; "
                "const char *bp = bv ? ((const char *const *)b)[(ptrdiff_t)i * bd] : (const char "
                "*)b + (ptrdiff_t)i * bd * (ptrdiff_t)bl; int comparison = "
                "f2c_character_compare(ap, al, bp, bl); bool value; switch (relation) { case 0: "
                "value = comparison == 0; break; case 1: value = comparison != 0; break; case 2: "
                "value = comparison < 0; break; case 3: value = comparison <= 0; break; case 4: "
                "value = comparison > 0; break; case 5: value = comparison >= 0; break; default: "
                "abort(); } if (reduction == 0 && value) return 1; if (reduction == 1 && "
                "!value) return 0; if (reduction == 2 && value) ++count; } if (reduction == 0) "
                "return 0; if (reduction == 1) return 1; if (reduction == 2) return count; "
                "abort(); }\n");
}
