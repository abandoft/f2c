#include "internal/f2c.h"

void f2c_emit_namelist_support(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_name_equal(const char *left, const char "
        "*right) { while (*left != '\\0' && *right != '\\0') { if "
        "(tolower((unsigned char)*left) != tolower((unsigned char)*right)) return false; "
        "++left; ++right; } return *left == '\\0' && *right == '\\0'; }\n"
        "static inline F2C_UNUSED long f2c_namelist_group(f2c_io_stream *file, long start, "
        "const char "
        "*group) { int c; if (start < 0L || f2c_stream_seek(file, start, SEEK_SET) != 0) "
        "return -1L; while ((c = f2c_stream_getc(file)) != EOF) { if (c == '&' || c == '$') "
        "{ char name[128]; size_t length = 0U; do { c = f2c_stream_getc(file); } while (c != "
        "EOF && "
        "isspace((unsigned char)c)); while (c != EOF && (isalnum((unsigned char)c) || c == "
        "'_')) { if (length + 1U < sizeof(name)) name[length++] = (char)c; c = "
        "f2c_stream_getc(file); } name[length] = '\\0'; if (c != EOF) "
        "(void)f2c_stream_ungetc(c, file); if (f2c_namelist_name_equal(name, group)) return "
        "f2c_stream_tell(file); } } return -1L; }\n");
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_root_equal(const char *designator, "
        "const char *member) { while (*member != '\\0' && *designator != '\\0' && "
        "tolower((unsigned char)*designator) == tolower((unsigned char)*member)) { "
        "++designator; ++member; } return *member == '\\0' && (*designator == '\\0' || "
        "*designator == '%' || *designator == '('); }\n");
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED void f2c_namelist_write_character(f2c_io_stream *file, "
        "const char "
        "*value, size_t length) { file->list_character = false; "
        "f2c_write_character(file, value, length); file->list_character = false; }\n");
    f2c_emit_namelist_parser_support(context);
}
