#include "core/generated/private.h"

static void emit_byte_access(Buffer *output, int needs_complex) {
    f2c_buffer_append(output, "#define F2C_RELATION_AT(value, offset) _Generic(*(value), int8_t: "
                              "F2C_RELATION_NAME(f2c_reduction_load_i8), int16_t: "
                              "F2C_RELATION_NAME(f2c_reduction_load_i16), int32_t: "
                              "F2C_RELATION_NAME(f2c_reduction_load_i32), int64_t: "
                              "F2C_RELATION_NAME(f2c_reduction_load_i64), bool: "
                              "F2C_RELATION_NAME(f2c_reduction_load_l), float: "
                              "F2C_RELATION_NAME(f2c_reduction_load_f), double: "
                              "F2C_RELATION_NAME(f2c_reduction_load_d)");
    if (needs_complex)
        f2c_buffer_append(
            output,
            ", f2c_complex_float: F2C_RELATION_NAME(f2c_reduction_load_c), f2c_complex_double: "
            "F2C_RELATION_NAME(f2c_reduction_load_z)");
    f2c_buffer_append(
        output,
        ")((value), (offset))\n"
        "static inline F2C_UNUSED const F2C_RELATION_ACCESS char "
        "*F2C_RELATION_NAME(f2c_character_relation_pointer)(const F2C_RELATION_ACCESS void "
        "*data, ptrdiff_t offset, int vector) { const F2C_RELATION_ACCESS unsigned char "
        "*address = (const F2C_RELATION_ACCESS unsigned char *)data + offset; if (vector == 2) "
        "return *(const F2C_RELATION_ACCESS char *const F2C_RELATION_ACCESS *)address; if "
        "(vector == 1) return *(const char *const F2C_RELATION_ACCESS *)address; return (const "
        "F2C_RELATION_ACCESS char *)address; }\n");
}

static void emit_relation_kernels(Buffer *output, int needs_complex, int qualified) {
    f2c_buffer_append(output, qualified
                                  ? "#define F2C_RELATION_ACCESS volatile\n"
                                    "#define F2C_RELATION_NAME(name) name##_volatile\n"
                                    "#define F2C_RELATION_COMPARE f2c_character_compare_volatile\n"
                                  : "#define F2C_RELATION_ACCESS\n"
                                    "#define F2C_RELATION_NAME(name) name\n"
                                    "#define F2C_RELATION_COMPARE f2c_character_compare\n");
    emit_byte_access(output, needs_complex);
    f2c_buffer_append(
        output,
        "#define F2C_DEFINE_RELATION_REDUCTION(s, t) static inline F2C_UNUSED int32_t "
        "F2C_RELATION_NAME(f2c_relation_reduce_##s)(const F2C_RELATION_ACCESS t *a, ptrdiff_t "
        "ad, size_t an, const F2C_RELATION_ACCESS t *b, ptrdiff_t bd, size_t bn, int relation, "
        "int reduction) { size_t i, n; int32_t count = 0; if (an == SIZE_MAX && bn == "
        "SIZE_MAX) abort(); if (an != SIZE_MAX && bn != SIZE_MAX && an != bn) abort(); n = an "
        "== SIZE_MAX ? bn : an; for (i = 0U; i < n; ++i) { t av = F2C_RELATION_AT(a, "
        "f2c_descriptor_stride_extent(ad, i)); t bv = F2C_RELATION_AT(b, "
        "f2c_descriptor_stride_extent(bd, i)); bool value; switch (relation) { case 0: value = "
        "av == bv; break; case 1: value = av != bv; break; case 2: value = av < bv; break; "
        "case 3: value = av <= bv; break; case 4: value = av > bv; break; case 5: value = av "
        ">= bv; break; case 6: value = (av != 0) == (bv != 0); break; case 7: value = (av != "
        "0) != (bv != 0); break; default: abort(); } if (reduction == 0 && value) return 1; if "
        "(reduction == 1 && !value) return 0; if (reduction == 2 && value) ++count; } if "
        "(reduction == 0) return 0; if (reduction == 1) return 1; if (reduction == 2) return "
        "count; abort(); }\n"
        "F2C_DEFINE_RELATION_REDUCTION(i8, int8_t)\n"
        "F2C_DEFINE_RELATION_REDUCTION(i16, int16_t)\n"
        "F2C_DEFINE_RELATION_REDUCTION(i32, int32_t)\n"
        "F2C_DEFINE_RELATION_REDUCTION(i64, int64_t)\n"
        "F2C_DEFINE_RELATION_REDUCTION(l, bool)\n"
        "F2C_DEFINE_RELATION_REDUCTION(f, float)\n"
        "F2C_DEFINE_RELATION_REDUCTION(d, double)\n"
        "#undef F2C_DEFINE_RELATION_REDUCTION\n");
    if (needs_complex) {
        f2c_buffer_append(
            output,
            "#define F2C_DEFINE_COMPLEX_RELATION_REDUCTION(s, t, equal_fn) static inline "
            "F2C_UNUSED int32_t F2C_RELATION_NAME(f2c_relation_reduce_##s)(const "
            "F2C_RELATION_ACCESS t *a, ptrdiff_t ad, size_t an, const F2C_RELATION_ACCESS t *b, "
            "ptrdiff_t bd, size_t bn, int relation, int reduction) { size_t i, n; int32_t count = "
            "0; if (an == SIZE_MAX && bn == SIZE_MAX) abort(); if (an != SIZE_MAX && bn != "
            "SIZE_MAX && an != bn) abort(); n = an == SIZE_MAX ? bn : an; for (i = 0U; i < n; ++i) "
            "{ bool equal = equal_fn(F2C_RELATION_AT(a, f2c_descriptor_stride_extent(ad, i)), "
            "F2C_RELATION_AT(b, f2c_descriptor_stride_extent(bd, i))); bool value; if (relation == "
            "0) value = equal; else if (relation == 1) value = !equal; else abort(); if (reduction "
            "== 0 && value) return 1; if (reduction == 1 && !value) return 0; if (reduction == 2 "
            "&& value) ++count; } if (reduction == 0) return 0; if (reduction == 1) return 1; if "
            "(reduction == 2) return count; abort(); }\n"
            "F2C_DEFINE_COMPLEX_RELATION_REDUCTION(c, f2c_complex_float, f2c_ceq)\n"
            "F2C_DEFINE_COMPLEX_RELATION_REDUCTION(z, f2c_complex_double, f2c_zeq)\n"
            "#undef F2C_DEFINE_COMPLEX_RELATION_REDUCTION\n");
    }
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int32_t "
        "F2C_RELATION_NAME(f2c_character_relation_reduce)(const F2C_RELATION_ACCESS void *a, "
        "ptrdiff_t ad, size_t an, size_t al, int av, const F2C_RELATION_ACCESS void *b, "
        "ptrdiff_t bd, size_t bn, size_t bl, int bv, int relation, int reduction) { size_t i, "
        "n; int32_t count = 0; if (an == SIZE_MAX && bn == SIZE_MAX) abort(); if (an != "
        "SIZE_MAX && bn != SIZE_MAX && an != bn) abort(); n = an == SIZE_MAX ? bn : an; for (i "
        "= 0U; i < n; ++i) { const F2C_RELATION_ACCESS char *ap = "
        "F2C_RELATION_NAME(f2c_character_relation_pointer)(a, f2c_descriptor_stride_extent(ad, "
        "i), av); const F2C_RELATION_ACCESS char *bp = "
        "F2C_RELATION_NAME(f2c_character_relation_pointer)(b, f2c_descriptor_stride_extent(bd, "
        "i), bv); int comparison = F2C_RELATION_COMPARE(ap, al, bp, bl); bool value; switch "
        "(relation) { case 0: value = comparison == 0; break; case 1: value = comparison != 0; "
        "break; case 2: value = comparison < 0; break; case 3: value = comparison <= 0; break; "
        "case 4: value = comparison > 0; break; case 5: value = comparison >= 0; break; "
        "default: abort(); } if (reduction == 0 && value) return 1; if (reduction == 1 && "
        "!value) return 0; if (reduction == 2 && value) ++count; } if (reduction == 0) return "
        "0; if (reduction == 1) return 1; if (reduction == 2) return count; abort(); }\n");
    f2c_buffer_append(output, "#undef F2C_RELATION_AT\n"
                              "#undef F2C_RELATION_COMPARE\n"
                              "#undef F2C_RELATION_NAME\n"
                              "#undef F2C_RELATION_ACCESS\n");
}

void f2c_emit_relation_reduction_support(Buffer *output, int needs_complex, int needs_qualified) {
    emit_relation_kernels(output, needs_complex, 0);
    if (needs_qualified) {
        emit_relation_kernels(output, needs_complex, 1);
    }
    f2c_buffer_append(
        output,
        "#define F2C_RELATION_REDUCE(a, ad, an, b, bd, bn, relation, reduction) _Generic(*(a), "
        "bool: f2c_relation_reduce_l, int8_t: f2c_relation_reduce_i8, int16_t: "
        "f2c_relation_reduce_i16, int32_t: f2c_relation_reduce_i32, int64_t: "
        "f2c_relation_reduce_i64, float: f2c_relation_reduce_f, double: f2c_relation_reduce_d");
    if (needs_complex)
        f2c_buffer_append(output, ", f2c_complex_float: f2c_relation_reduce_c, f2c_complex_double: "
                                  "f2c_relation_reduce_z");
    f2c_buffer_append(output, ")((a), (ad), (an), (b), (bd), (bn), (relation), (reduction))\n");
}
