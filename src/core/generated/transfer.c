#include "core/generated/private.h"

static void emit_value_wrapper(Buffer *output, const char *suffix, const char *type) {
    f2c_buffer_printf(output,
                      "static inline F2C_UNUSED %s f2c_transfer_%s(const void *source, "
                      "size_t source_size) { %s result; f2c_transfer_copy(&result, "
                      "sizeof(result), source, source_size); return result; }\n"
                      "static inline F2C_UNUSED %s f2c_transfer_%s_owned(void *source, "
                      "size_t source_size, void (*release)(void *)) { %s result = "
                      "f2c_transfer_%s(source, source_size); if (release == NULL) abort(); "
                      "release(source); return result; }\n",
                      type, suffix, type, type, suffix, type, suffix);
}

void f2c_emit_transfer_support(Buffer *output, int needs_complex) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED size_t f2c_transfer_extent(int64_t value) { if (value < 0 || "
        "(uint64_t)value > (uint64_t)SIZE_MAX) abort(); return (size_t)value; }\n"
        "static inline F2C_UNUSED void f2c_transfer_copy(void *target, size_t target_size, "
        "const void *source, size_t source_size) { size_t count = target_size < source_size ? "
        "target_size : source_size; if (target_size != 0U) memset(target, 0, target_size); "
        "if (count != 0U) "
        "memmove(target, source, count); }\n"
        "static inline F2C_UNUSED char *f2c_transfer_character(char **storage, "
        "size_t target_size, const void *source, size_t source_size) { if (storage == NULL) "
        "abort(); if (target_size == SIZE_MAX) abort(); { char *replacement = "
        "(char *)realloc(*storage, target_size + 1U); if (replacement == NULL) abort(); "
        "*storage = replacement; } "
        "f2c_transfer_copy(*storage, target_size, source, source_size); "
        "(*storage)[target_size] = '\\0'; return *storage; }\n"
        "static inline F2C_UNUSED char *f2c_transfer_character_owned(char **storage, "
        "size_t target_size, void *source, size_t source_size, "
        "void (*release)(void *)) { char *result = f2c_transfer_character(storage, "
        "target_size, source, source_size); if (release == NULL) abort(); release(source); "
        "return result; }\n");
    emit_value_wrapper(output, "i8", "int8_t");
    emit_value_wrapper(output, "i16", "int16_t");
    emit_value_wrapper(output, "i32", "int32_t");
    emit_value_wrapper(output, "i64", "int64_t");
    emit_value_wrapper(output, "logical", "bool");
    emit_value_wrapper(output, "r4", "float");
    emit_value_wrapper(output, "r8", "double");
    emit_value_wrapper(output, "r16", "long double");
    if (needs_complex) {
        emit_value_wrapper(output, "c4", "f2c_complex_float");
        emit_value_wrapper(output, "c8", "f2c_complex_double");
        emit_value_wrapper(output, "c16", "f2c_complex_long_double");
    }
}
