#include "core/generated/private.h"

void f2c_emit_unaligned_storage_support(Buffer *output, int needs_complex) {
    f2c_buffer_append(output,
                      "#define F2C_DEFINE_UNALIGNED_ACCESS(suffix, type) \\\n"
                      "static inline F2C_UNUSED type f2c_unaligned_load_##suffix("
                      "const unsigned char *address) { \\\n"
                      "    type value; memcpy(&value, address, sizeof(value)); return value; \\\n"
                      "} \\\n"
                      "static inline F2C_UNUSED void f2c_unaligned_store_##suffix("
                      "unsigned char *address, type value) { \\\n"
                      "    memcpy(address, &value, sizeof(value)); \\\n"
                      "} \\\n"
                      "static inline F2C_UNUSED type f2c_unaligned_load_##suffix##_volatile("
                      "const volatile unsigned char *address) { \\\n"
                      "    type value; unsigned char *bytes = (unsigned char *)&value; \\\n"
                      "    for (size_t i = 0U; i < sizeof(value); ++i) bytes[i] = address[i]; \\\n"
                      "    return value; \\\n"
                      "} \\\n"
                      "static inline F2C_UNUSED void f2c_unaligned_store_##suffix##_volatile("
                      "volatile unsigned char *address, type value) { \\\n"
                      "    const unsigned char *bytes = (const unsigned char *)&value; \\\n"
                      "    for (size_t i = 0U; i < sizeof(value); ++i) address[i] = bytes[i]; \\\n"
                      "}\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(i8, int8_t)\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(i16, int16_t)\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(i32, int32_t)\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(i64, int64_t)\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(r4, float)\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(r8, double)\n"
                      "F2C_DEFINE_UNALIGNED_ACCESS(r16, long double)\n");
    if (needs_complex)
        f2c_buffer_append(output, "F2C_DEFINE_UNALIGNED_ACCESS(c4, f2c_complex_float)\n"
                                  "F2C_DEFINE_UNALIGNED_ACCESS(c8, f2c_complex_double)\n"
                                  "F2C_DEFINE_UNALIGNED_ACCESS(c16, f2c_complex_long_double)\n");
    f2c_buffer_append(output, "#undef F2C_DEFINE_UNALIGNED_ACCESS\n");
}
