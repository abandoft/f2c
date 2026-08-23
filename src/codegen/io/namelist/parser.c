#include "internal/f2c.h"

static void emit_input_model(Context *context) {
    f2c_buffer_append(
        &context->output,
        "typedef struct f2c_namelist_assignment { char *designator; char *value; size_t "
        "value_length; } f2c_namelist_assignment;\n"
        "typedef struct f2c_namelist_input { f2c_namelist_assignment *assignments; size_t "
        "count; size_t capacity; } f2c_namelist_input;\n"
        "static inline F2C_UNUSED void f2c_namelist_input_destroy(f2c_namelist_input *input) "
        "{ size_t index; if (input == NULL) return; for (index = 0U; index < input->count; "
        "++index) { free(input->assignments[index].designator); "
        "free(input->assignments[index].value); } free(input->assignments); memset(input, 0, "
        "sizeof(*input)); }\n"
        "static inline F2C_UNUSED bool f2c_namelist_grow(char **buffer, size_t *capacity, "
        "size_t required) { size_t next; char *grown; if (required <= *capacity) return true; "
        "next = *capacity == 0U ? 256U : *capacity; while (next < required) { if (next > "
        "SIZE_MAX / 2U) { next = required; break; } next *= 2U; } grown = (char *)realloc(*buffer, "
        "next); if (grown == NULL) return false; *buffer = grown; *capacity = next; return true; "
        "}\n"
        "static inline F2C_UNUSED char *f2c_namelist_copy_slice(const char *begin, const char "
        "*end, bool compact) { size_t length = (size_t)(end - begin), index, output = 0U; char "
        "*copy; if (length == SIZE_MAX) return NULL; copy = (char *)malloc(length + 1U); if "
        "(copy == NULL) return NULL; for (index = 0U; index < length; ++index) if (!compact || "
        "!isspace((unsigned char)begin[index])) copy[output++] = begin[index]; copy[output] = "
        "'\\0'; return copy; }\n");
}

static void emit_designator_parser(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED const char *f2c_namelist_parse_designator(const char *cursor, "
        "const char *end, const char **equals) { int parentheses = 0; bool identifier = true; "
        "if (cursor == end || (!isalpha((unsigned char)*cursor) && *cursor != '_')) return "
        "NULL; while (cursor < end) { const unsigned char c = (unsigned char)*cursor; if "
        "(isspace(c)) { const char *next = cursor; while (next < end && "
        "isspace((unsigned char)*next)) ++next; if (parentheses == 0 && next < end && *next != "
        "'=') return NULL; cursor = next; continue; } if (c == '=' && parentheses == 0) { "
        "*equals = cursor; return cursor + 1; } if (c == '(') { ++parentheses; identifier = "
        "false; } else if (c == ')') { if (parentheses == 0) return NULL; --parentheses; "
        "identifier = false; } else if (c == '%') { if (parentheses != 0) return NULL; "
        "identifier = true; } else if (identifier) { if (!isalnum(c) && c != '_') return NULL; "
        "} else if (parentheses == 0 || (!isalnum(c) && c != '_' && c != ':' && c != ',' && "
        "c != '+' && c != '-')) return NULL; ++cursor; } return NULL; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_assignment_is(const f2c_namelist_input "
        "*input, size_t index, const char *designator) { return input != NULL && index < "
        "input->count && f2c_namelist_name_equal(input->assignments[index].designator, "
        "designator); }\n"
        "static inline F2C_UNUSED bool f2c_namelist_assignment_root_is(const "
        "f2c_namelist_input *input, size_t index, const char *root) { return input != NULL && "
        "index < input->count && f2c_namelist_root_equal(input->assignments[index].designator, "
        "root); }\n");
}

static void emit_assignment_storage(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_append_assignment(f2c_namelist_input "
        "*input, const char *designator_begin, const char *designator_end, const char "
        "*value_begin, const char *value_end) { f2c_namelist_assignment *grown, *entry; size_t "
        "next; while (value_end > value_begin && isspace((unsigned char)value_end[-1])) "
        "--value_end; if (input->count == input->capacity) { next = input->capacity == 0U ? 8U "
        ": input->capacity * 2U; if (next < input->capacity || next > SIZE_MAX / "
        "sizeof(*grown)) return false; grown = (f2c_namelist_assignment *)realloc("
        "input->assignments, next * sizeof(*grown)); if (grown == NULL) return false; "
        "input->assignments = grown; input->capacity = next; } entry = "
        "&input->assignments[input->count]; memset(entry, 0, sizeof(*entry)); entry->designator "
        "= f2c_namelist_copy_slice(designator_begin, designator_end, true); entry->value = "
        "f2c_namelist_copy_slice(value_begin, value_end, false); if (entry->designator == NULL "
        "|| entry->value == NULL) { free(entry->designator); free(entry->value); "
        "memset(entry, 0, sizeof(*entry)); return false; } entry->value_length = "
        "(size_t)(value_end - value_begin); ++input->count; return true; }\n"
        "static inline F2C_UNUSED const char *f2c_namelist_next_assignment(const char *cursor, "
        "const char *end) { int quote = 0, parentheses = 0; while (cursor < end) { const int c "
        "= (unsigned char)*cursor; if (quote != 0) { if (c == quote) { if (cursor + 1 < end && "
        "cursor[1] == quote) { cursor += 2; continue; } quote = 0; } ++cursor; continue; } if "
        "(c == '\\'' || c == '\"') { quote = c; ++cursor; continue; } if (c == '(') "
        "++parentheses; else if (c == ')' && parentheses > 0) --parentheses; else if "
        "(parentheses == 0 && (c == ',' || isspace((unsigned char)c))) { const char *candidate "
        "= cursor + 1, *equals = NULL, *boundary = cursor; while (candidate < end && "
        "(*candidate == ',' || isspace((unsigned char)*candidate))) { if (*candidate == ',') "
        "boundary = candidate; ++candidate; } if (candidate < end && "
        "f2c_namelist_parse_designator(candidate, end, &equals) != NULL) return boundary; } "
        "++cursor; } return end; }\n");
}

static void emit_input_parser(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_parse_body(f2c_namelist_input *input, "
        "const char *body, size_t length) { const char *cursor = body, *end = body + length; "
        "while (cursor < end) { const char *equals = NULL, *value, *next, *designator_end; while "
        "(cursor < end && (*cursor == ',' || isspace((unsigned char)*cursor))) ++cursor; if "
        "(cursor == end) return true; value = f2c_namelist_parse_designator(cursor, end, "
        "&equals); if (value == NULL || equals == NULL) return false; designator_end = equals; "
        "while (designator_end > cursor && isspace((unsigned char)designator_end[-1])) "
        "--designator_end; next = f2c_namelist_next_assignment(value, end); if "
        "(!f2c_namelist_append_assignment(input, cursor, designator_end, value, next)) return "
        "false; cursor = next; } return true; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_input_parse(f2c_io_stream *file, long "
        "group_start, f2c_namelist_input *input) { char *body = NULL; size_t length = 0U, "
        "capacity = 0U; int c, quote = 0; bool terminated = false, valid; memset(input, 0, "
        "sizeof(*input)); if (group_start < 0L || f2c_stream_seek(file, group_start, SEEK_SET) "
        "!= 0) return false; while ((c = f2c_stream_getc(file)) != EOF) { if (quote != 0) { if "
        "(c == quote) { int next = f2c_stream_getc(file); if (next == quote) { if "
        "(!f2c_namelist_grow(&body, &capacity, length + 3U)) goto fail; body[length++] = "
        "(char)c; body[length++] = (char)next; continue; } quote = 0; if (next != EOF) "
        "(void)f2c_stream_ungetc(next, file); } } else if (c == '\\'' || c == '\"') quote "
        "= c; else if (c == '/') { terminated = true; break; } if "
        "(!f2c_namelist_grow(&body, &capacity, length + 2U)) goto fail; body[length++] = "
        "(char)c; } if (!terminated || quote != 0) goto fail; valid = "
        "f2c_namelist_parse_body(input, body != NULL ? body : \"\", length); free(body); if "
        "(!valid) f2c_namelist_input_destroy(input); return valid; fail: free(body); "
        "f2c_namelist_input_destroy(input); return false; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_value_stream(const f2c_namelist_input "
        "*input, size_t index, f2c_io_stream *stream) { if (input == NULL || index >= "
        "input->count) return false; return f2c_stream_initialize_internal(stream, "
        "input->assignments[index].value, input->assignments[index].value_length, 1U, true); "
        "}\n"
        "static inline F2C_UNUSED bool f2c_namelist_value_consumed(f2c_io_stream *stream) { int "
        "c; do { c = f2c_stream_getc(stream); } while (c == ',' || (c != EOF && "
        "isspace((unsigned char)c))); return c == EOF; }\n");
}

static void emit_value_cursor(Context *context) {
    f2c_buffer_append(
        &context->output,
        "typedef enum f2c_namelist_item_status { F2C_NAMELIST_ITEM_ERROR = -1, "
        "F2C_NAMELIST_ITEM_END = 0, F2C_NAMELIST_ITEM_VALUE = 1, F2C_NAMELIST_ITEM_NULL = 2 "
        "} f2c_namelist_item_status;\n"
        "typedef struct f2c_namelist_value_cursor { char *text; size_t length; size_t position; "
        "size_t repeated_offset; size_t repeated_length; uint64_t repeat_remaining; bool "
        "at_field_start; bool terminal_null; bool error; } f2c_namelist_value_cursor;\n"
        "static inline F2C_UNUSED bool f2c_namelist_cursor_initialize(const f2c_namelist_input "
        "*input, size_t index, f2c_namelist_value_cursor *cursor) { if (input == NULL || index "
        ">= input->count || cursor == NULL) return false; memset(cursor, 0, sizeof(*cursor)); "
        "cursor->text = input->assignments[index].value; cursor->length = "
        "input->assignments[index].value_length; cursor->at_field_start = true; return true; "
        "}\n"
        "static inline F2C_UNUSED bool f2c_namelist_item_stream(f2c_namelist_value_cursor "
        "*cursor, size_t offset, size_t length, f2c_io_stream *stream) { if (offset > "
        "cursor->length || length > cursor->length - offset) { cursor->error = true; return "
        "false; } return f2c_stream_initialize_internal(stream, cursor->text + offset, length, "
        "1U, true); }\n");
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_repeat_prefix(const char *text, size_t "
        "begin, size_t end, uint64_t *repeat, size_t *value_begin) { size_t cursor = begin; "
        "uint64_t count = 0U; if (cursor == end || !isdigit((unsigned char)text[cursor])) "
        "return false; while (cursor < end && isdigit((unsigned char)text[cursor])) { const "
        "unsigned digit = (unsigned)(text[cursor] - '0'); if (count > (UINT64_MAX - digit) / "
        "UINT64_C(10)) return false; count = count * UINT64_C(10) + digit; ++cursor; } if "
        "(cursor == end || text[cursor] != '*') return false; if (count == 0U) return false; "
        "*repeat = count; *value_begin = cursor + 1U; return true; }\n"
        "static inline F2C_UNUSED size_t f2c_namelist_item_end(const char *text, size_t "
        "position, size_t length) { int quote = 0, parentheses = 0; while (position < length) "
        "{ const int c = (unsigned char)text[position]; if (quote != 0) { if (c == quote) { if "
        "(position + 1U < length && text[position + 1U] == quote) { position += 2U; continue; } "
        "quote = 0; } ++position; continue; } if (c == '\\'' || c == '\"') quote = c; else "
        "if (c == '(') ++parentheses; else if (c == ')' && parentheses > 0) --parentheses; else "
        "if (parentheses == 0 && (c == ',' || isspace((unsigned char)c))) break; ++position; } "
        "return position; }\n");
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED f2c_namelist_item_status f2c_namelist_cursor_next("
        "f2c_namelist_value_cursor *cursor, f2c_io_stream *stream) { size_t begin, end, "
        "value_begin; uint64_t repeat = 1U; if (cursor == NULL || stream == NULL || "
        "cursor->error) return F2C_NAMELIST_ITEM_ERROR; if (cursor->repeat_remaining != 0U) { "
        "--cursor->repeat_remaining; if (cursor->repeated_length == 0U) return "
        "F2C_NAMELIST_ITEM_NULL; return f2c_namelist_item_stream(cursor, "
        "cursor->repeated_offset, cursor->repeated_length, stream) ? F2C_NAMELIST_ITEM_VALUE "
        ": F2C_NAMELIST_ITEM_ERROR; } if (cursor->terminal_null) { cursor->terminal_null = "
        "false; return F2C_NAMELIST_ITEM_NULL; } while (cursor->position < cursor->length && "
        "isspace((unsigned char)cursor->text[cursor->position])) ++cursor->position; if "
        "(cursor->position < cursor->length && cursor->text[cursor->position] == ',') { "
        "++cursor->position; if (cursor->at_field_start) return F2C_NAMELIST_ITEM_NULL; "
        "cursor->at_field_start = true; while (cursor->position < cursor->length && "
        "isspace((unsigned char)cursor->text[cursor->position])) ++cursor->position; if "
        "(cursor->position == cursor->length) { cursor->terminal_null = true; return "
        "f2c_namelist_cursor_next(cursor, stream); } if (cursor->text[cursor->position] == ',') "
        "{ ++cursor->position; return F2C_NAMELIST_ITEM_NULL; } } while (cursor->position < "
        "cursor->length "
        "&& isspace((unsigned char)cursor->text[cursor->position])) ++cursor->position; if "
        "(cursor->position == cursor->length) return F2C_NAMELIST_ITEM_END; begin = "
        "cursor->position; end = f2c_namelist_item_end(cursor->text, begin, cursor->length); if "
        "(end == begin) { cursor->error = true; return F2C_NAMELIST_ITEM_ERROR; } "
        "cursor->position = end; cursor->at_field_start = false; value_begin = begin; if "
        "(f2c_namelist_repeat_prefix(cursor->text, begin, end, &repeat, &value_begin)) { "
        "cursor->repeat_remaining = repeat - 1U; cursor->repeated_offset = value_begin; "
        "cursor->repeated_length = end - value_begin; if (cursor->repeated_length == 0U) "
        "return F2C_NAMELIST_ITEM_NULL; } return f2c_namelist_item_stream(cursor, value_begin, "
        "end - value_begin, stream) ? F2C_NAMELIST_ITEM_VALUE : F2C_NAMELIST_ITEM_ERROR; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_cursor_consumed("
        "f2c_namelist_value_cursor *cursor) { f2c_io_stream ignored; "
        "f2c_namelist_item_status item = f2c_namelist_cursor_next(cursor, &ignored); return item "
        "== F2C_NAMELIST_ITEM_END; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_assignment_value_count(const "
        "f2c_namelist_input *input, size_t index, size_t *count) { f2c_namelist_value_cursor "
        "cursor; f2c_io_stream ignored; f2c_namelist_item_status item; size_t total = 0U; if "
        "(count == NULL || !f2c_namelist_cursor_initialize(input, index, &cursor)) return false; "
        "for (;;) { item = f2c_namelist_cursor_next(&cursor, &ignored); if (item == "
        "F2C_NAMELIST_ITEM_END) { *count = total; return true; } if (item == "
        "F2C_NAMELIST_ITEM_ERROR) return false; if (total == SIZE_MAX || "
        "cursor.repeat_remaining > SIZE_MAX - total - 1U) return false; total += "
        "(size_t)cursor.repeat_remaining + 1U; "
        "cursor.repeat_remaining = 0U; } }\n");
}

static void emit_selector_parser(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_parse_index(const char *begin, const char "
        "*end, int64_t *value) { char *parsed_end = NULL; long long parsed; if (begin == end) "
        "return false; errno = 0; parsed = strtoll(begin, &parsed_end, 10); if (errno == ERANGE "
        "|| parsed_end != end) return false; *value = (int64_t)parsed; return true; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_selector_accepts(const char *begin, const "
        "char *end, int64_t index) { const char *first = NULL, *second = NULL, *cursor; "
        "int64_t lower, upper, stride = 1; uint64_t distance, magnitude; for (cursor = begin; "
        "cursor < end; ++cursor) if (*cursor == ':') { if (first == NULL) first = cursor; else "
        "if (second == NULL) second = cursor; else return false; } if (first == NULL) return "
        "f2c_namelist_parse_index(begin, end, &lower) && index == lower; if (second != NULL && "
        "!f2c_namelist_parse_index(second + 1, end, &stride)) return false; if (stride == 0) "
        "return false; lower = stride > 0 ? INT64_MIN : INT64_MAX; upper = stride > 0 ? "
        "INT64_MAX : INT64_MIN; if (first != begin && !f2c_namelist_parse_index(begin, first, "
        "&lower)) return false; if ((second != NULL ? second : end) != first + 1 && "
        "!f2c_namelist_parse_index(first + 1, second != NULL ? second : end, &upper)) return "
        "false; if (stride > 0) { if (index < lower || index > upper) return false; if (lower == "
        "INT64_MIN) return true; distance = (uint64_t)index - (uint64_t)lower; magnitude = "
        "(uint64_t)stride; } else { if (index > lower || index < upper) return false; if (lower "
        "== INT64_MAX) return true; distance = (uint64_t)lower - (uint64_t)index; magnitude = "
        "(uint64_t)(-(stride + 1)) + UINT64_C(1); } return distance % magnitude == 0U; }\n");
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_designator_selects_text(const char "
        "*selector, const char *element) { while (*selector != '\\0' && *element != '\\0') { "
        "if (*selector != '(') { if (*element == '(' || tolower((unsigned char)*selector) != "
        "tolower((unsigned char)*element)) return false; ++selector; ++element; continue; } { "
        "const char *selector_end, *element_end; int64_t index; if (*element != '(') return "
        "false; ++selector; ++element; for (;;) { selector_end = selector; while "
        "(*selector_end != '\\0' && *selector_end != ',' && *selector_end != ')') "
        "++selector_end; element_end = element; while (*element_end != '\\0' && *element_end "
        "!= ',' && *element_end != ')') ++element_end; if (!f2c_namelist_parse_index(element, "
        "element_end, &index) || !f2c_namelist_selector_accepts(selector, selector_end, index)) "
        "return false; if (*selector_end != *element_end) return false; if (*selector_end == "
        "')') { selector = selector_end + 1; element = element_end + 1; break; } if "
        "(*selector_end != ',') return false; selector = selector_end + 1; element = "
        "element_end + 1; } } } return *selector == '\\0' && *element == '\\0'; }\n"
        "static inline F2C_UNUSED bool f2c_namelist_assignment_selects(const "
        "f2c_namelist_input *input, size_t index, const char *element) { return input != NULL "
        "&& index < input->count && f2c_namelist_designator_selects_text("
        "input->assignments[index].designator, element); }\n");
}

static void emit_substring_parser(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_assignment_substring(const "
        "f2c_namelist_input *input, size_t index, const char *base, size_t character_length, "
        "size_t *offset, size_t *length) { const char *designator, *cursor, *colon, *close; "
        "size_t base_length; int64_t lower = 1, upper; if (input == NULL || index >= "
        "input->count || base == NULL || offset == NULL || length == NULL || character_length "
        "> (size_t)INT64_MAX) return false; designator = input->assignments[index].designator; "
        "base_length = strlen(base); for (cursor = designator; (size_t)(cursor - designator) < "
        "base_length; ++cursor) if (*cursor == '\\0' || tolower((unsigned char)*cursor) != "
        "tolower((unsigned char)base[(size_t)(cursor - designator)])) return false; if "
        "(*cursor != '(') return false; colon = strchr(cursor + 1, ':'); close = "
        "strchr(cursor + 1, ')'); if (colon == NULL || close == NULL || colon > close || "
        "close[1] != '\\0' || strchr(colon + 1, ':') != NULL) return false; upper = "
        "(int64_t)character_length; if (colon != cursor + 1 && "
        "!f2c_namelist_parse_index(cursor + 1, colon, &lower)) return false; if (close != "
        "colon + 1 && !f2c_namelist_parse_index(colon + 1, close, &upper)) return false; if "
        "(lower < 1 || upper < 0 || lower > (int64_t)character_length + 1 || upper > "
        "(int64_t)character_length || lower > upper + 1) return false; *offset = "
        "(size_t)(lower - 1); *length = upper >= lower ? (size_t)(upper - lower + 1) : 0U; "
        "return true; }\n");
}

static void emit_section_direction(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED bool f2c_namelist_dimension_descends(const "
        "f2c_namelist_input *input, size_t index, const char *base, size_t dimension) { const "
        "char *cursor, *end, *first, *second; size_t current = 0U, base_length; int64_t stride; "
        "if (input == NULL || index >= input->count || base == NULL) return false; cursor = "
        "input->assignments[index].designator; base_length = strlen(base); for (size_t i = 0U; "
        "i < base_length; ++i) if (cursor[i] == '\\0' || "
        "tolower((unsigned char)cursor[i]) != tolower((unsigned char)base[i])) return false; "
        "cursor += base_length; if (*cursor != '(') return false; ++cursor; while (*cursor != "
        "'\\0' && current < dimension) { while (*cursor != '\\0' && *cursor != ',' && "
        "*cursor != ')') ++cursor; if (*cursor != ',') return false; ++cursor; ++current; } if "
        "(current != dimension) return false; end = cursor; while (*end != '\\0' && *end != "
        "',' && *end != ')') ++end; first = memchr(cursor, ':', (size_t)(end - cursor)); if "
        "(first == NULL) return false; second = memchr(first + 1, ':', (size_t)(end - first - "
        "1)); return second != NULL && f2c_namelist_parse_index(second + 1, end, &stride) && "
        "stride < 0; }\n");
}

void f2c_emit_namelist_parser_support(Context *context) {
    emit_input_model(context);
    emit_designator_parser(context);
    emit_assignment_storage(context);
    emit_input_parser(context);
    emit_value_cursor(context);
    emit_selector_parser(context);
    emit_substring_parser(context);
    emit_section_direction(context);
}
