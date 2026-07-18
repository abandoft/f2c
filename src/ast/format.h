#ifndef F2C_AST_FORMAT_H
#define F2C_AST_FORMAT_H

#include "ir/format.h"

F2cFormat *f2c_format_parse(const char *text, size_t length, const F2cSourceSpan *span,
                            F2cFormatError *error);
F2cFormat *f2c_format_parse_character_literal(const char *literal, const F2cSourceSpan *span,
                                              F2cFormatError *error);
const char *f2c_format_error_message(F2cFormatErrorCode code);

#endif
