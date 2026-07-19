#include "core/generated/private.h"

static void emit_scalar_list_io(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED void f2c_write_i8(f2c_io_stream *f, int8_t v) { "
        "f2c_stream_printf(f, \" %d\", (int)v); }\n"
        "static inline F2C_UNUSED void f2c_write_i16(f2c_io_stream *f, int16_t v) { "
        "f2c_stream_printf(f, \" %d\", (int)v); }\n"
        "static inline F2C_UNUSED void f2c_write_i32(f2c_io_stream *f, int32_t v) { "
        "f2c_stream_printf(f, \" %d\", (int)v); }\n"
        "static inline F2C_UNUSED void f2c_write_i64(f2c_io_stream *f, int64_t v) { "
        "f2c_stream_printf(f, \" %lld\", (long long)v); }\n"
        "static inline F2C_UNUSED void f2c_write_float(f2c_io_stream *f, float v) { "
        "f2c_stream_printf(f, \" %.9g\", (double)v); }\n"
        "static inline F2C_UNUSED void f2c_write_double(f2c_io_stream *f, double v) { "
        "f2c_stream_printf(f, \" %.17g\", v); }\n"
        "static inline F2C_UNUSED void f2c_write_bool(f2c_io_stream *f, bool v) { "
        "f2c_stream_write_string(f, v ? \" T\" : \" F\"); }\n"
        "static inline F2C_UNUSED void f2c_write_char(f2c_io_stream *f, char v) { "
        "(void)f2c_stream_putc(' ', f); (void)f2c_stream_putc((unsigned char)v, f); }\n"
        "static inline F2C_UNUSED void f2c_write_string(f2c_io_stream *f, const char *v) { "
        "(void)f2c_stream_putc(' ', f); f2c_stream_write_string(f, v); }\n"
        "static inline F2C_UNUSED void f2c_write_character(f2c_io_stream *f, const char *v, "
        "size_t length) { (void)f2c_stream_putc(' ', f); (void)f2c_stream_write(v, length, f); "
        "}\n"
        "static inline F2C_UNUSED int f2c_read_number_token(f2c_io_stream *f, char *v, size_t "
        "n) { int c; size_t i = 0U; if (n == 0U) return 0; do { c = f2c_stream_getc(f); } "
        "while (c != EOF && (isspace((unsigned char)c) || c == ',')); if (c == EOF) return "
        "EOF; do { if (i + 1U < n) v[i++] = (char)c; c = f2c_stream_getc(f); } while (c != EOF "
        "&& !isspace((unsigned char)c) && c != ',' && c != ')'); if (c != EOF) "
        "(void)f2c_stream_ungetc(c, f); v[i] = '\\0'; for (i = 0U; v[i] != '\\0'; ++i) if "
        "(v[i] == 'd' || v[i] == 'D') v[i] = 'e'; return 1; }\n"
        "static inline F2C_UNUSED int f2c_read_i32(f2c_io_stream *f, int32_t *v) { char "
        "t[128]; int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
        "(int32_t)strtol(t, NULL, 10); return r; }\n"
        "static inline F2C_UNUSED int f2c_read_i8(f2c_io_stream *f, int8_t *v) { char t[128]; "
        "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
        "(int8_t)strtol(t, NULL, 10); return r; }\n"
        "static inline F2C_UNUSED int f2c_read_i16(f2c_io_stream *f, int16_t *v) { char "
        "t[128]; int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
        "(int16_t)strtol(t, NULL, 10); return r; }\n"
        "static inline F2C_UNUSED int f2c_read_i64(f2c_io_stream *f, int64_t *v) { char "
        "t[128]; int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
        "(int64_t)strtoll(t, NULL, 10); return r; }\n"
        "static inline F2C_UNUSED int f2c_read_float(f2c_io_stream *f, float *v) { char "
        "t[128]; int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = strtof(t, "
        "NULL); return r; }\n"
        "static inline F2C_UNUSED int f2c_read_double(f2c_io_stream *f, double *v) { char "
        "t[128]; int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = strtod(t, "
        "NULL); return r; }\n"
        "static inline F2C_UNUSED int f2c_read_bool(f2c_io_stream *f, bool *v) { char t[16]; "
        "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = t[0] == 'T' || t[0] "
        "== 't' || t[0] == '1'; return r; }\n"
        "static inline F2C_UNUSED int f2c_read_char(f2c_io_stream *f, char *v) { int c; do { c "
        "= f2c_stream_getc(f); } while (c != EOF && isspace((unsigned char)c)); if (c == EOF) "
        "return EOF; *v = (char)c; return 1; }\n");
}

static void emit_character_list_io(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_read_character(f2c_io_stream *f, char *v, size_t "
        "length) { int c, quote = 0; size_t i = 0U; do { c = f2c_stream_getc(f); } while (c "
        "!= EOF && (isspace((unsigned char)c) || c == ',')); if (c == EOF) return EOF; if (c "
        "== '\\'' || c == '\"') quote = c; else if (i < length) v[i++] = (char)c; while ((c "
        "= f2c_stream_getc(f)) != EOF) { if (quote != 0) { if (c == quote) { int next = "
        "f2c_stream_getc(f); if (next == quote) c = next; else { if (next != EOF) "
        "(void)f2c_stream_ungetc(next, f); break; } } } else if (isspace((unsigned char)c) || c "
        "== ',') { (void)f2c_stream_ungetc(c, f); break; } if (i < length) v[i++] = (char)c; "
        "} if (i < length) memset(v + i, ' ', length - i); return 1; }\n"
        "static inline F2C_UNUSED int f2c_read_record(f2c_io_stream *f, char *v, size_t "
        "capacity) { size_t length = 0U; int c; if (capacity == 0U) return 0; while (length + "
        "1U < capacity && (c = f2c_stream_getc(f)) != EOF && c != '\\n' && c != '\\r') "
        "v[length++] = (char)c; if (length == 0U && c == EOF) return EOF; if (c == '\\r') { "
        "int next = f2c_stream_getc(f); if (next != '\\n' && next != EOF) "
        "(void)f2c_stream_ungetc(next, f); } else if (c != '\\n' && c != EOF) { do { c = "
        "f2c_stream_getc(f); } while (c != '\\n' && c != EOF); } memset(v + length, ' ', "
        "capacity - length - 1U); v[capacity - 1U] = '\\0'; return 1; }\n"
        "static inline F2C_UNUSED void f2c_finish_read(f2c_io_stream *f) { int c; do { c = "
        "f2c_stream_getc(f); } while (c != '\\n' && c != EOF); }\n");
}

static void emit_complex_list_io(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED void f2c_write_c(f2c_io_stream *f, f2c_complex_float v) { "
        "f2c_stream_printf(f, \" (%.9g,%.9g)\", (double)crealf(v), (double)cimagf(v)); }\n"
        "static inline F2C_UNUSED void f2c_write_z(f2c_io_stream *f, f2c_complex_double v) { "
        "f2c_stream_printf(f, \" (%.17g,%.17g)\", creal(v), cimag(v)); }\n"
        "static inline F2C_UNUSED int f2c_read_complex_parts(f2c_io_stream *f, double *r, "
        "double *i) { char a[128], b[128]; size_t p = 0U; int c; do { c = "
        "f2c_stream_getc(f); } while (c != EOF && (isspace((unsigned char)c) || c == ',')); if "
        "(c != '(') return c == EOF ? EOF : 0; while ((c = f2c_stream_getc(f)) != EOF && c != "
        "',') if (p + 1U < sizeof(a)) a[p++] = (char)c; if (c == EOF) return EOF; a[p] = "
        "'\\0'; p = 0U; while ((c = f2c_stream_getc(f)) != EOF && c != ')') if (p + 1U < "
        "sizeof(b)) b[p++] = (char)c; if (c == EOF) return EOF; b[p] = '\\0'; for (p = 0U; "
        "a[p] != '\\0'; ++p) if (a[p] == 'd' || a[p] == 'D') a[p] = 'e'; for (p = 0U; b[p] "
        "!= '\\0'; ++p) if (b[p] == 'd' || b[p] == 'D') b[p] = 'e'; *r = strtod(a, NULL); "
        "*i = strtod(b, NULL); return 2; }\n"
        "static inline F2C_UNUSED int f2c_read_c(f2c_io_stream *f, f2c_complex_float *v) { "
        "double r, i; int n = f2c_read_complex_parts(f, &r, &i); if (n == 2) *v = "
        "f2c_make_c((float)r, (float)i); return n; }\n"
        "static inline F2C_UNUSED int f2c_read_z(f2c_io_stream *f, f2c_complex_double *v) { "
        "double r, i; int n = f2c_read_complex_parts(f, &r, &i); if (n == 2) *v = "
        "f2c_make_z(r, i); return n; }\n");
}

void f2c_emit_list_io_support(Buffer *output, int needs_complex) {
    emit_scalar_list_io(output);
    emit_character_list_io(output);
    if (needs_complex)
        emit_complex_list_io(output);
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
