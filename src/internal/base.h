#ifndef F2C_INTERNAL_BASE_H
#define F2C_INTERNAL_BASE_H

#include "f2c/f2c.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Buffer {
    char *data;
    size_t length;
    size_t capacity;
    size_t limit;
    int limit_exceeded;
    int failed;
} Buffer;

typedef struct Context Context;

void f2c_buffer_append_n(Buffer *buffer, const char *text, size_t length);
void f2c_buffer_append(Buffer *buffer, const char *text);
void f2c_buffer_printf(Buffer *buffer, const char *format, ...);
char *f2c_buffer_take(Buffer *buffer);
char *f2c_strdup_n(const char *text, size_t length);
char *f2c_strdup(const char *text);
char *f2c_trim(char *text);
void f2c_diagnostic(Context *context, size_t line, int error, const char *format, ...);
void f2c_diagnostic_at(Context *context, size_t line, size_t column, int error, const char *format,
                       ...);
void f2c_diagnostic_code(Context *context, F2cDiagnosticCode code, size_t line, int error,
                         const char *format, ...);
void f2c_diagnostic_at_code(Context *context, F2cDiagnosticCode code, size_t line, size_t column,
                            int error, const char *format, ...);

#endif
