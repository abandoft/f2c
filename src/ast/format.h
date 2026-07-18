#ifndef F2C_AST_FORMAT_H
#define F2C_AST_FORMAT_H

#include "ir/format.h"

typedef enum F2cFormatErrorCode {
    F2C_FORMAT_ERROR_NONE,
    F2C_FORMAT_ERROR_MEMORY,
    F2C_FORMAT_ERROR_EXPECTED_LEFT_PARENTHESIS,
    F2C_FORMAT_ERROR_EXPECTED_RIGHT_PARENTHESIS,
    F2C_FORMAT_ERROR_EXPECTED_ITEM,
    F2C_FORMAT_ERROR_INVALID_REPEAT,
    F2C_FORMAT_ERROR_INVALID_NUMBER,
    F2C_FORMAT_ERROR_UNTERMINATED_LITERAL,
    F2C_FORMAT_ERROR_INVALID_HOLLERITH,
    F2C_FORMAT_ERROR_INVALID_DESCRIPTOR,
    F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD,
    F2C_FORMAT_ERROR_INVALID_DT_LIST,
    F2C_FORMAT_ERROR_TRAILING_TEXT,
    F2C_FORMAT_ERROR_BUDGET
} F2cFormatErrorCode;

typedef struct F2cFormatError {
    F2cFormatErrorCode code;
    size_t offset;
} F2cFormatError;

F2cFormat *f2c_format_parse(const char *text, size_t length, const F2cSourceSpan *span,
                            F2cFormatError *error);
F2cFormat *f2c_format_parse_character_literal(const char *literal, const F2cSourceSpan *span,
                                              F2cFormatError *error);
const char *f2c_format_error_message(F2cFormatErrorCode code);

#endif
