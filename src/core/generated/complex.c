#include "core/generated/private.h"

/* MSVC represents complex values as structs: use component operations rather
 * than C arithmetic operators, without reducing long-double precision. */
void f2c_emit_extended_complex_support(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED f2c_complex_long_double f2c_qadd(f2c_complex_long_double a, "
        "f2c_complex_long_double b) { return f2c_make_q(creall(a) + creall(b), cimagl(a) + "
        "cimagl(b)); }\n"
        "static inline F2C_UNUSED f2c_complex_long_double f2c_qsub(f2c_complex_long_double a, "
        "f2c_complex_long_double b) { return f2c_make_q(creall(a) - creall(b), cimagl(a) - "
        "cimagl(b)); }\n"
        "static inline F2C_UNUSED f2c_complex_long_double f2c_qneg(f2c_complex_long_double a) "
        "{ return f2c_make_q(-creall(a), -cimagl(a)); }\n"
        "static inline F2C_UNUSED f2c_complex_long_double f2c_qmul(f2c_complex_long_double a, "
        "f2c_complex_long_double b) { long double ar = creall(a), ai = cimagl(a), br = creall(b), "
        "bi = cimagl(b); return f2c_make_q(ar * br - ai * bi, ar * bi + ai * br); }\n"
        "static inline F2C_UNUSED bool f2c_qeq(f2c_complex_long_double a, f2c_complex_long_double "
        "b) "
        "{ return creall(a) == creall(b) && cimagl(a) == cimagl(b); }\n"
        "static inline F2C_UNUSED f2c_complex_long_double f2c_square_q(f2c_complex_long_double a) "
        "{ return f2c_qmul(a, a); }\n"
        "static inline F2C_UNUSED f2c_complex_long_double f2c_qdiv(f2c_complex_long_double a, "
        "f2c_complex_long_double b) { long double ar = creall(a), ai = cimagl(a), br = creall(b), "
        "bi = cimagl(b), scale = fmaxl(fabsl(br), fabsl(bi)); "
        "if (isnan(br) || isnan(bi)) return f2c_make_q(NAN, NAN); "
        "if (isfinite(scale) && scale > 0.0L) { long double ars = ar / scale, ais = ai / scale, "
        "brs = br / scale, bis = bi / scale, denominator = brs * brs + bis * bis, "
        "real_part = (ars * brs + ais * bis) / denominator, "
        "imag_part = (ais * brs - ars * bis) / denominator; "
        "if (isnan(real_part) && isnan(imag_part) && (isinf(ar) || isinf(ai))) { "
        "ar = copysignl(isinf(ar) ? 1.0L : 0.0L, ar); ai = copysignl(isinf(ai) ? 1.0L : 0.0L, ai); "
        "real_part = INFINITY * (ar * brs + ai * bis); imag_part = INFINITY * (ai * brs - ar * "
        "bis); } "
        "return f2c_make_q(real_part, imag_part); } "
        "if (scale == 0.0L && (!isnan(ar) || !isnan(ai))) { long double infinity = "
        "copysignl(INFINITY, br); "
        "return f2c_make_q(infinity * ar, infinity * ai); } "
        "if (isinf(scale) && isfinite(ar) && isfinite(ai)) { "
        "br = copysignl(isinf(br) ? 1.0L : 0.0L, br); bi = copysignl(isinf(bi) ? 1.0L : 0.0L, bi); "
        "return f2c_make_q(0.0L * (ar * br + ai * bi), 0.0L * (ai * br - ar * bi)); } "
        "return f2c_make_q(NAN, NAN); }\n");
}
