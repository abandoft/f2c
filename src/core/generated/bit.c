#include "core/generated/private.h"

static void emit_integer_kind(Buffer *output, const char *suffix, const char *signed_type,
                              const char *unsigned_type, unsigned width, const char *width_mask) {
    f2c_buffer_printf(output,
                      "_Static_assert(sizeof(%s) == sizeof(%s), "
                      "\"f2c integer bit storage mismatch\");\n",
                      signed_type, unsigned_type);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED uint64_t f2c_bits_%s(%s value) { %s object; "
                      "memcpy(&object, &value, sizeof(object)); return (uint64_t)object; }\n",
                      suffix, signed_type, unsigned_type);
    f2c_buffer_printf(
        output,
        "static inline F2C_UNUSED %s f2c_value_%s(uint64_t bits) { %s object = "
        "(%s)(bits & %s); %s value; memcpy(&value, &object, sizeof(value)); return value; }\n",
        signed_type, suffix, unsigned_type, unsigned_type, width_mask, signed_type);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED %s f2c_iand_%s(%s left, %s right) { return "
                      "f2c_value_%s(f2c_bits_%s(left) & f2c_bits_%s(right)); }\n",
                      signed_type, suffix, signed_type, signed_type, suffix, suffix, suffix);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED %s f2c_ior_%s(%s left, %s right) { return "
                      "f2c_value_%s(f2c_bits_%s(left) | f2c_bits_%s(right)); }\n",
                      signed_type, suffix, signed_type, signed_type, suffix, suffix, suffix);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED %s f2c_ieor_%s(%s left, %s right) { return "
                      "f2c_value_%s(f2c_bits_%s(left) ^ f2c_bits_%s(right)); }\n",
                      signed_type, suffix, signed_type, signed_type, suffix, suffix, suffix);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED %s f2c_not_%s(%s value) { return "
                      "f2c_value_%s((~f2c_bits_%s(value)) & %s); }\n",
                      signed_type, suffix, signed_type, suffix, suffix, width_mask);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED bool f2c_btest_%s(%s value, int64_t position) { "
                      "if (position < 0 || position >= (int64_t)%u) abort(); return "
                      "(f2c_bits_%s(value) & (UINT64_C(1) << (unsigned)position)) != 0U; }\n",
                      suffix, signed_type, width, suffix);
    f2c_buffer_printf(
        output,
        "static inline F2C_UNUSED %s f2c_ibclr_%s(%s value, int64_t position) { "
        "if (position < 0 || position >= (int64_t)%u) abort(); return "
        "f2c_value_%s(f2c_bits_%s(value) & ~(UINT64_C(1) << (unsigned)position)); }\n",
        signed_type, suffix, signed_type, width, suffix, suffix);
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED %s f2c_ibset_%s(%s value, int64_t position) { "
                      "if (position < 0 || position >= (int64_t)%u) abort(); return "
                      "f2c_value_%s(f2c_bits_%s(value) | (UINT64_C(1) << (unsigned)position)); }\n",
                      signed_type, suffix, signed_type, width, suffix, suffix);
    f2c_buffer_printf(
        output,
        "static inline F2C_UNUSED %s f2c_ibits_%s(%s value, int64_t position, "
        "int64_t length) { uint64_t mask; if (position < 0 || length < 0 || "
        "(uint64_t)position > UINT64_C(%u) || (uint64_t)length > UINT64_C(%u) - "
        "(uint64_t)position) abort(); if (length == 0) return (%s)0; mask = "
        "length == 64 ? UINT64_MAX : (UINT64_C(1) << (unsigned)length) - UINT64_C(1); "
        "return f2c_value_%s((f2c_bits_%s(value) >> (unsigned)position) & mask); }\n",
        signed_type, suffix, signed_type, width, width, signed_type, suffix, suffix);
    f2c_buffer_printf(
        output,
        "static inline F2C_UNUSED %s f2c_ishft_%s(%s value, int64_t shift) { "
        "uint64_t bits = f2c_bits_%s(value); unsigned amount; if (shift < -INT64_C(%u) || "
        "shift > INT64_C(%u)) abort(); if (shift == -INT64_C(%u) || shift == INT64_C(%u)) "
        "return (%s)0; amount = (unsigned)(shift < 0 ? -shift : shift); return shift < 0 ? "
        "f2c_value_%s(bits >> amount) : f2c_value_%s((bits << amount) & %s); }\n",
        signed_type, suffix, signed_type, suffix, width, width, width, width, signed_type, suffix,
        suffix, width_mask);
    f2c_buffer_printf(
        output,
        "static inline F2C_UNUSED %s f2c_ishftc_%s(%s value, int64_t shift, int64_t size) { "
        "uint64_t bits = f2c_bits_%s(value); uint64_t mask; uint64_t field; "
        "int64_t normalized; unsigned amount; if (size <= 0 || size > INT64_C(%u) || "
        "shift < -size || shift > size) abort(); mask = size == 64 ? UINT64_MAX : "
        "(UINT64_C(1) << (unsigned)size) - UINT64_C(1); field = bits & mask; "
        "normalized = shift %% size; if (normalized < 0) normalized += size; "
        "amount = (unsigned)normalized; if (amount != 0U) field = "
        "((field << amount) | (field >> ((unsigned)size - amount))) & mask; "
        "return f2c_value_%s((bits & ~mask) | field); }\n",
        signed_type, suffix, signed_type, suffix, width, suffix);
    f2c_buffer_printf(
        output,
        "static inline F2C_UNUSED void f2c_mvbits_%s(%s source, int64_t source_position, "
        "int64_t length, %s *target, int64_t target_position) { uint64_t mask; "
        "uint64_t source_bits; uint64_t target_bits; if (target == NULL || "
        "source_position < 0 || length < 0 || target_position < 0 || "
        "(uint64_t)source_position > UINT64_C(%u) || "
        "(uint64_t)length > UINT64_C(%u) - (uint64_t)source_position || "
        "(uint64_t)target_position > UINT64_C(%u) || "
        "(uint64_t)length > UINT64_C(%u) - (uint64_t)target_position) abort(); "
        "if (length == 0) return; mask = length == 64 ? UINT64_MAX : "
        "(UINT64_C(1) << (unsigned)length) - UINT64_C(1); "
        "source_bits = f2c_bits_%s(source); target_bits = f2c_bits_%s(*target); "
        "target_bits = (target_bits & ~(mask << (unsigned)target_position)) | "
        "(((source_bits >> (unsigned)source_position) & mask) << "
        "(unsigned)target_position); *target = f2c_value_%s(target_bits); }\n",
        suffix, signed_type, signed_type, width, width, width, width, suffix, suffix, suffix);
}

void f2c_emit_bit_intrinsic_support(Buffer *output) {
    emit_integer_kind(output, "i8", "int8_t", "uint8_t", 8U, "UINT64_C(0xff)");
    emit_integer_kind(output, "i16", "int16_t", "uint16_t", 16U, "UINT64_C(0xffff)");
    emit_integer_kind(output, "i32", "int32_t", "uint32_t", 32U, "UINT64_C(0xffffffff)");
    emit_integer_kind(output, "i64", "int64_t", "uint64_t", 64U, "UINT64_MAX");
}
