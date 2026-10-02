#include "core/generated/private.h"

void f2c_emit_list_token_support(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_io_read_token(f2c_io_stream *stream, int terminator, "
        "bool whitespace_ends, bool skip_separator, char **token) { size_t length = 0U, "
        "capacity = 128U; char *buffer; int c; *token = NULL; do { c = f2c_stream_getc(stream); "
        "} while (c != EOF && (isspace((unsigned char)c) || (skip_separator && c == "
        "f2c_io_value_separator(stream)))); if (c == EOF) return EOF; buffer = "
        "(char *)malloc(capacity); if (buffer == NULL) return 0; for (;;) { if (c == EOF || "
        "(terminator != 0 && c == terminator) || (whitespace_ends && "
        "(isspace((unsigned char)c) || c == f2c_io_value_separator(stream) || c == ')'))) "
        "break; if (length + 1U == capacity) { size_t next = capacity * 2U; char *grown; "
        "if (capacity >= 16U * 1024U * 1024U || next <= capacity) { free(buffer); return 0; } "
        "grown = (char *)realloc(buffer, next); if (grown == NULL) { free(buffer); return "
        "0; } buffer = grown; capacity = next; } buffer[length++] = (char)c; "
        "c = f2c_stream_getc(stream); } if (c != EOF) (void)f2c_stream_ungetc(c, stream); "
        "if (length == 0U || (terminator != 0 && c == EOF)) { free(buffer); return c == EOF "
        "? EOF : 0; } buffer[length] = '\\0'; *token = buffer; return 1; }\n"
        "static inline F2C_UNUSED int f2c_read_number_token(f2c_io_stream *stream, char "
        "*token, size_t capacity) { char *allocated; int status = "
        "f2c_io_read_token(stream, 0, true, true, &allocated); if (status != 1) return status; "
        "if (strlen(allocated) >= capacity) { free(allocated); return 0; } strcpy(token, "
        "allocated); free(allocated); for (size_t index = 0U; token[index] != '\\0'; ++index) "
        "if (token[index] == 'd' || token[index] == 'D') token[index] = 'e'; return 1; }\n");
}
