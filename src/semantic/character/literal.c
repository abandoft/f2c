#include "internal/f2c.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

size_t f2c_character_literal_length(const char *text) {
    const char *quote_begin;
    size_t source_length;
    char quote;
    const char *payload;
    size_t payload_length;
    size_t length = 0U;
    size_t i;
    if (f2c_hollerith_payload(text, &payload, &payload_length))
        return payload_length;
    quote_begin = f2c_character_literal_quote(text);
    if (quote_begin == NULL)
        return 0U;
    source_length = strlen(quote_begin);
    quote = *quote_begin;
    if ((quote != '\'' && quote != '"') || source_length < 2U)
        return 0U;
    for (i = 1U; i + 1U < source_length; ++i) {
        if (quote_begin[i] == quote && i + 1U < source_length - 1U && quote_begin[i + 1U] == quote)
            ++i;
        ++length;
    }
    return length;
}

char *f2c_character_literal_bytes(const char *text, size_t *length) {
    const char *cursor = text;
    const char *quote_begin;
    const char *payload;
    size_t payload_length;
    char quote;
    Buffer bytes = {0};
    if (text == NULL || length == NULL)
        return NULL;
    if (f2c_hollerith_payload(text, &payload, &payload_length)) {
        *length = payload_length;
        return f2c_strdup_n(payload, payload_length);
    }
    while (isspace((unsigned char)*cursor))
        ++cursor;
    quote_begin = f2c_character_literal_quote(cursor);
    if (quote_begin == NULL)
        return NULL;
    cursor = quote_begin;
    quote = *cursor;
    if (quote != '\'' && quote != '"')
        return NULL;
    ++cursor;
    while (*cursor != '\0') {
        if (*cursor == quote) {
            if (cursor[1] == quote) {
                f2c_buffer_printf(&bytes, "%c", quote);
                cursor += 2;
                continue;
            }
            ++cursor;
            while (isspace((unsigned char)*cursor))
                ++cursor;
            if (*cursor != '\0') {
                free(f2c_buffer_take(&bytes));
                return NULL;
            }
            *length = bytes.length;
            return f2c_buffer_take(&bytes);
        }
        f2c_buffer_printf(&bytes, "%c", *cursor++);
    }
    free(f2c_buffer_take(&bytes));
    return NULL;
}
