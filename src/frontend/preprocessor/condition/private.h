#ifndef F2C_FRONTEND_PREPROCESSOR_CONDITION_PRIVATE_H
#define F2C_FRONTEND_PREPROCESSOR_CONDITION_PRIVATE_H

#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <string.h>

static inline int condition_identifier_start(char value) {
    return isalpha((unsigned char)value) != 0 || value == '_';
}

static inline int condition_identifier_continue(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_';
}

static inline int condition_word_equal(const char *begin, size_t length, const char *word) {
    return strlen(word) == length && strncmp(begin, word, length) == 0;
}

void f2c_preprocessor_condition_diagnose(Preprocessor *preprocessor, F2cDiagnosticCode code,
                                         size_t line, size_t column, const char *message);
int f2c_preprocessor_expand_condition(Preprocessor *preprocessor, const char *text, size_t length,
                                      size_t line, size_t column, Buffer *output);

#endif
