#ifndef F2C_FRONTEND_TOKEN_H
#define F2C_FRONTEND_TOKEN_H

#include "internal/base.h"

typedef struct F2cSourcePosition {
    const char *source_name;
    size_t line;
    size_t column;
} F2cSourcePosition;

typedef struct F2cSourceSpan {
    F2cSourcePosition begin;
    F2cSourcePosition end;
    F2cSourcePosition spelling_begin;
    F2cSourcePosition spelling_end;
    int has_spelling;
} F2cSourceSpan;

/* A compact run mapping normalized text back to its expansion and spelling
 * locations. A zero column step maps an entire generated run to one source
 * token; a step of one represents ordinary contiguous source text. */
typedef struct F2cSourceMapSegment {
    size_t logical_begin;
    size_t length;
    F2cSourcePosition expansion;
    F2cSourcePosition spelling;
    size_t expansion_width;
    size_t spelling_width;
    unsigned char expansion_column_step;
    unsigned char spelling_column_step;
    unsigned char has_spelling;
} F2cSourceMapSegment;

typedef struct F2cSourceMap {
    F2cSourceMapSegment *items;
    size_t count;
    size_t capacity;
} F2cSourceMap;

typedef enum F2cTokenKind {
    F2C_TOKEN_END,
    F2C_TOKEN_IDENTIFIER,
    F2C_TOKEN_NUMBER,
    F2C_TOKEN_STRING,
    F2C_TOKEN_HOLLERITH,
    F2C_TOKEN_BOZ,
    F2C_TOKEN_LEFT_PAREN,
    F2C_TOKEN_RIGHT_PAREN,
    F2C_TOKEN_LEFT_BRACKET,
    F2C_TOKEN_RIGHT_BRACKET,
    F2C_TOKEN_ARRAY_BEGIN,
    F2C_TOKEN_ARRAY_END,
    F2C_TOKEN_COMMA,
    F2C_TOKEN_COLON,
    F2C_TOKEN_DOUBLE_COLON,
    F2C_TOKEN_SEMICOLON,
    F2C_TOKEN_PERCENT,
    F2C_TOKEN_OPERATOR,
    F2C_TOKEN_INVALID
} F2cTokenKind;

typedef struct F2cToken {
    F2cTokenKind kind;
    const char *begin;
    size_t length;
    size_t line;
    size_t column;
    F2cSourceSpan span;
} F2cToken;

typedef struct F2cTokenStream {
    const char *source;
    const char *cursor;
    size_t line;
    size_t base_column;
    const char *error_at;
    F2cToken token;
} F2cTokenStream;

typedef struct F2cTokenCursor {
    const F2cToken *tokens;
    size_t count;
    size_t position;
} F2cTokenCursor;

typedef struct F2cTokenRange {
    const char *source;
    size_t source_length;
    const F2cToken *tokens;
    size_t count;
} F2cTokenRange;

typedef struct Line {
    char *text;
    const char *source_name;
    size_t number;
    size_t interface_depth;
    int emit_source_comments;
    F2cToken *tokens;
    size_t token_count;
    F2cSourceMapSegment *source_map;
    size_t source_map_count;
} Line;

typedef struct Lines {
    Line *items;
    size_t count;
    size_t capacity;
} Lines;

void f2c_token_stream_init(F2cTokenStream *stream, const char *source, size_t line,
                           size_t base_column);
void f2c_token_stream_next(F2cTokenStream *stream);
int f2c_token_equals(const F2cToken *token, const char *text);
char *f2c_token_text(const F2cToken *token);
int f2c_line_token_equals(const Line *line, size_t index, const char *text);
size_t f2c_line_find_token(const Line *line, size_t start, F2cTokenKind kind, const char *text);
F2cTokenRange f2c_line_token_range(const Line *line, size_t begin, size_t end);
void f2c_token_cursor_init(F2cTokenCursor *cursor, const F2cToken *tokens, size_t count);
const F2cToken *f2c_token_cursor_peek(const F2cTokenCursor *cursor, size_t lookahead);
const F2cToken *f2c_token_cursor_take(F2cTokenCursor *cursor);
int f2c_token_cursor_consume(F2cTokenCursor *cursor, F2cTokenKind kind, const char *text);
int f2c_token_range_balanced(const F2cToken *tokens, size_t count);
int f2c_token_matching_delimiter(const F2cToken *tokens, size_t count, size_t open_index,
                                 size_t *close_index);
F2cTokenRange f2c_token_range_slice(F2cTokenRange range, size_t begin, size_t end);
size_t f2c_token_range_find_top_level(F2cTokenRange range, size_t start, F2cTokenKind kind,
                                      const char *text);
int f2c_token_range_split_top_level(F2cTokenRange range, F2cTokenKind separator_kind,
                                    const char *separator_text, F2cTokenRange **items,
                                    size_t *count);
char *f2c_token_range_text(F2cTokenRange range);
int f2c_hollerith_payload(const char *text, const char **payload, size_t *length);
const char *f2c_character_literal_quote(const char *text);
int f2c_tokenize_lines(Context *context);
int f2c_lines_push(Context *context, char *text, size_t number, const F2cOptions *options);
int f2c_lines_push_mapped(Context *context, char *text, size_t number,
                          F2cSourceMapSegment *source_map, size_t source_map_count,
                          const F2cOptions *options);
F2cSourcePosition f2c_line_source_position(const Line *line, size_t logical_offset);
F2cSourceSpan f2c_line_source_span(const Line *line, size_t logical_begin, size_t logical_length);
F2cSourceSpan f2c_source_span_cover(const F2cSourceSpan *first, const F2cSourceSpan *last);
int f2c_source_map_append(F2cSourceMap *map, size_t logical_begin, size_t length,
                          F2cSourcePosition expansion, size_t expansion_width,
                          unsigned char expansion_column_step, F2cSourcePosition spelling,
                          size_t spelling_width, unsigned char spelling_column_step,
                          int has_spelling);
int f2c_source_map_append_slice(F2cSourceMap *destination, size_t logical_begin,
                                const F2cSourceMapSegment *source, size_t source_count,
                                size_t source_begin, size_t length);
F2cSourceMapSegment *f2c_source_map_slice(const F2cSourceMapSegment *source, size_t source_count,
                                          size_t begin, size_t length, size_t *result_count);
void f2c_source_map_discard(F2cSourceMap *map);
void f2c_diagnostic_span_code(Context *context, F2cDiagnosticCode code, const F2cSourceSpan *span,
                              int error, const char *format, ...);
void f2c_diagnostic_token_code(Context *context, F2cDiagnosticCode code, const Line *line,
                               const F2cToken *token, int error, const char *format, ...);

#endif
