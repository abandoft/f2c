#ifndef F2C_IR_FORMAT_H
#define F2C_IR_FORMAT_H

#include "frontend/token.h"

typedef enum F2cFormatNodeKind {
    F2C_FORMAT_GROUP,
    F2C_FORMAT_DATA,
    F2C_FORMAT_LITERAL,
    F2C_FORMAT_RECORD,
    F2C_FORMAT_COLON,
    F2C_FORMAT_SPACE,
    F2C_FORMAT_POSITION,
    F2C_FORMAT_SCALE,
    F2C_FORMAT_SIGN,
    F2C_FORMAT_BLANK,
    F2C_FORMAT_DECIMAL,
    F2C_FORMAT_ROUND
} F2cFormatNodeKind;

typedef enum F2cFormatPositionKind {
    F2C_FORMAT_POSITION_ABSOLUTE,
    F2C_FORMAT_POSITION_LEFT,
    F2C_FORMAT_POSITION_RIGHT
} F2cFormatPositionKind;

typedef enum F2cFormatSignKind {
    F2C_FORMAT_SIGN_PROCESSOR,
    F2C_FORMAT_SIGN_PLUS,
    F2C_FORMAT_SIGN_SUPPRESS
} F2cFormatSignKind;

typedef enum F2cFormatBlankKind { F2C_FORMAT_BLANK_NULL, F2C_FORMAT_BLANK_ZERO } F2cFormatBlankKind;

typedef enum F2cFormatDecimalKind {
    F2C_FORMAT_DECIMAL_POINT,
    F2C_FORMAT_DECIMAL_COMMA
} F2cFormatDecimalKind;

typedef enum F2cFormatRoundKind {
    F2C_FORMAT_ROUND_UP,
    F2C_FORMAT_ROUND_DOWN,
    F2C_FORMAT_ROUND_ZERO,
    F2C_FORMAT_ROUND_NEAREST,
    F2C_FORMAT_ROUND_COMPATIBLE,
    F2C_FORMAT_ROUND_PROCESSOR
} F2cFormatRoundKind;

typedef struct F2cFormatNode {
    F2cFormatNodeKind kind;
    F2cSourceSpan span;
    uint32_t repeat;
    int unlimited;
    char code[3];
    int width;
    int digits;
    int exponent;
    int has_width;
    int has_digits;
    int has_exponent;
    int control;
    char *text;
    size_t text_length;
    int32_t *v_list;
    size_t v_list_count;
    struct F2cFormatNode *children;
    size_t child_count;
    size_t child_capacity;
} F2cFormatNode;

typedef struct F2cFormat {
    F2cFormatNode root;
    F2cSourceSpan span;
    char *source;
    size_t source_length;
    int validated;
} F2cFormat;

F2cFormat *f2c_format_clone(const F2cFormat *format);
void f2c_format_free(F2cFormat *format);

#endif
