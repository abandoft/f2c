#include "core/generated/private.h"

void f2c_emit_list_io_support(Buffer *output, int needs_complex) {
    f2c_emit_list_write_support(output);
    f2c_emit_list_token_support(output);
    f2c_emit_list_read_support(output);
    if (needs_complex)
        f2c_emit_list_complex_support(output);
    f2c_buffer_append(
        output,
        needs_complex
            ? "#define F2C_WRITE(f, v) _Generic((v), int8_t: f2c_write_i8, int16_t: "
              "f2c_write_i16, int32_t: f2c_write_i32, int64_t: f2c_write_i64, float: "
              "f2c_write_float, double: f2c_write_double, bool: f2c_write_bool, char: "
              "f2c_write_char, char *: f2c_write_string, const char *: f2c_write_string, "
              "f2c_complex_float: f2c_write_c, f2c_complex_double: f2c_write_z)((f), (v))\n"
              "#define F2C_READ(f, v) _Generic((v), int8_t *: f2c_read_i8, int16_t *: "
              "f2c_read_i16, int32_t *: f2c_read_i32, int64_t *: f2c_read_i64, float *: "
              "f2c_read_float, double *: f2c_read_double, bool *: f2c_read_bool, char *: "
              "f2c_read_char, f2c_complex_float *: f2c_read_c, f2c_complex_double *: "
              "f2c_read_z)((f), (v))\n"
            : "#define F2C_WRITE(f, v) _Generic((v), int8_t: f2c_write_i8, int16_t: "
              "f2c_write_i16, int32_t: f2c_write_i32, int64_t: f2c_write_i64, float: "
              "f2c_write_float, double: f2c_write_double, bool: f2c_write_bool, char: "
              "f2c_write_char, char *: f2c_write_string, const char *: f2c_write_string)((f), "
              "(v))\n"
              "#define F2C_READ(f, v) _Generic((v), int8_t *: f2c_read_i8, int16_t *: "
              "f2c_read_i16, int32_t *: f2c_read_i32, int64_t *: f2c_read_i64, float *: "
              "f2c_read_float, double *: f2c_read_double, bool *: f2c_read_bool, char *: "
              "f2c_read_char)((f), (v))\n");
}
