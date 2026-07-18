#include "codegen/io/private.h"

static void emit_text_parser_helpers(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_format_second(const f2c_format_state *state) { "
        "return state->position + 1U < state->length ? (unsigned char)state->text["
        "state->position + 1U] : 0; }\n"
        "static inline F2C_UNUSED void f2c_format_skip_spaces(f2c_format_state *state) { while "
        "(f2c_format_peek(state) == ' ' || f2c_format_peek(state) == '\\t') "
        "++state->position; }\n"
        "static inline F2C_UNUSED int f2c_format_quoted_literal(f2c_format_state *state, int "
        "quote) { ++state->position; for (;;) { size_t begin = state->position; while "
        "(state->position < state->length && (unsigned char)state->text[state->position] != "
        "(unsigned)quote) ++state->position; f2c_format_literal(state, state->text + begin, "
        "state->position - begin); if (state->position >= state->length) { state->status = 0; "
        "return 0; } ++state->position; if (f2c_format_peek(state) != quote) return 1; "
        "f2c_format_literal(state, state->text + state->position, 1U); ++state->position; } }\n"
        "static inline F2C_UNUSED int f2c_format_descriptor_boundary(int value) { return value "
        "== 0 || value == ')' || value == ',' || value == '/' || value == ':' || "
        "isspace(value); }\n");
}

static void emit_dynamic_dt_parser(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_format_dynamic_dt(f2c_format_state *state, "
        "f2c_format_descriptor *descriptor) { int quote; if (!f2c_format_reset_iotype(state)) "
        "return 0; state->scratch_v_list_count = 0U; f2c_format_skip_spaces(state); quote = "
        "f2c_format_peek(state); if (quote == '\\'' || quote == '\"') { bool closed = false; "
        "++state->position; while (state->position < state->length) { int item = (unsigned "
        "char)state->text[state->position++]; if (item == quote) { if (f2c_format_peek(state) "
        "== quote) ++state->position; else { closed = true; break; } } if "
        "(!f2c_format_append_iotype(state, (char)item)) return 0; } if (!closed) { "
        "state->status = 0; return 0; } } f2c_format_skip_spaces(state); if "
        "(f2c_format_peek(state) == '(') { bool have_value = false; ++state->position; for (;;) "
        "{ bool negative = false; int present = 0; uint32_t magnitude; f2c_format_skip_spaces("
        "state); if (f2c_format_peek(state) == '+' || f2c_format_peek(state) == '-') { negative "
        "= f2c_format_peek(state) == '-'; ++state->position; } magnitude = "
        "f2c_format_unsigned(state, &present); if (!present || state->status == 0 || magnitude "
        "> UINT32_C(2147483647) + (negative ? 1U : 0U)) { state->status = 0; return 0; } if "
        "(!f2c_format_append_v_list(state, negative ? (int32_t)(-(int64_t)magnitude) : "
        "(int32_t)magnitude)) return 0; have_value = true; f2c_format_skip_spaces(state); if "
        "(f2c_format_peek(state) == ')') { ++state->position; break; } if "
        "(f2c_format_peek(state) != ',') { state->status = 0; return 0; } "
        "++state->position; } if (!have_value) { state->status = 0; return 0; } } "
        "descriptor->iotype = state->scratch_iotype; descriptor->iotype_length = "
        "state->scratch_iotype_length; descriptor->v_list = state->scratch_v_list; "
        "descriptor->v_list_count = state->scratch_v_list_count; return 1; }\n");
}

static void emit_text_parser_prefix(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int f2c_format_next_text(f2c_format_state *state, "
        "f2c_format_descriptor *descriptor) { bool reverted = false; for (;;) { int c; int has "
        "= 0; bool negative = false; bool unlimited = false; uint32_t prefix, repeats; while "
        "((c = f2c_format_peek(state)) == ' ' || c == '\\t' || c == ',') "
        "++state->position; c = f2c_format_peek(state); if (c == 0) { if (state->finishing) "
        "return 0; if (reverted) { state->status = 0; return 0; } reverted = true; "
        "f2c_format_record(state); state->position = state->reversion; state->depth = "
        "state->depth != 0U ? 1U : 0U; if (state->depth != 0U) "
        "state->frames[0].yielded = false; continue; } if (c == '+' || c == '-') { negative = c "
        "== '-'; ++state->position; } prefix = f2c_format_unsigned(state, &has); if "
        "(state->status == 0) return 0; c = toupper(f2c_format_peek(state)); if (c == '*') { if "
        "(has || negative) { state->status = 0; return 0; } unlimited = true; "
        "++state->position; f2c_format_skip_spaces(state); c = f2c_format_peek(state); if (c "
        "!= '(') { state->status = 0; return 0; } } if (c == '(') { if (negative || (has && "
        "prefix == 0U) || !f2c_format_push_frame(state, state->position + 1U, has ? prefix : "
        "1U, unlimited)) { state->status = 0; return 0; } ++state->position; continue; } if (c "
        "== ')') { if (state->depth > 1U) { f2c_format_frame *frame = "
        "&state->frames[state->depth - 1U]; if (frame->unlimited) { if (!frame->yielded) { "
        "state->status = 0; return 0; } if (state->finishing) return 0; frame->yielded = false; "
        "state->position = frame->start; } else if (frame->remaining > 1U) { "
        "--frame->remaining; frame->yielded = false; state->position = frame->start; } else { "
        "--state->depth; ++state->position; } } else { if (state->finishing) return 0; if "
        "(reverted) { state->status = 0; return 0; } reverted = true; f2c_format_record(state); "
        "state->position = state->reversion; if (state->depth != 0U) "
        "state->frames[0].yielded = false; } continue; } if (c == '\\'' || c == '\"') { if "
        "(negative || has || !f2c_format_quoted_literal(state, c)) { state->status = 0; return "
        "0; } continue; } if (has && !negative && c == 'H') { size_t available; "
        "++state->position; available = state->length - state->position; if (prefix == 0U || "
        "(size_t)prefix > available) { state->status = 0; return 0; } "
        "f2c_format_literal(state, state->text + state->position, prefix); state->position += "
        "prefix; continue; } if (c == '/') { uint32_t count; if (negative || (has && prefix == "
        "0U)) { state->status = 0; return 0; } count = has ? prefix : 1U; "
        "++state->position; while (count-- != 0U) f2c_format_record(state); continue; } if (c "
        "== ':') { if (negative || has) { state->status = 0; return 0; } ++state->position; if "
        "(state->finishing) return 0; continue; } if (c == 'X') { if (negative || (has && "
        "prefix == 0U)) { state->status = 0; return 0; } ++state->position; "
        "f2c_format_spaces(state, has ? prefix : 1U); continue; } ");
}

static void emit_text_parser_controls(Buffer *output) {
    f2c_buffer_append(
        output,
        "if (c == 'T') { int mode = 0; if (negative || has) { state->status = 0; return 0; } "
        "++state->position; c = toupper(f2c_format_peek(state)); if (c == 'L' || c == 'R') { "
        "mode = c == 'L' ? -1 : 1; ++state->position; } prefix = f2c_format_unsigned(state, "
        "&has); if (!has || prefix == 0U || state->status == 0) { state->status = 0; return 0; "
        "} f2c_format_position(state, mode, prefix); continue; } if (c == 'P') { if (!has) { "
        "state->status = 0; return 0; } state->scale = negative ? -(int)prefix : (int)prefix; "
        "++state->position; continue; } if (negative) { state->status = 0; return 0; } if (c == "
        "'S' && !has) { ++state->position; c = toupper(f2c_format_peek(state)); if (c == 'P') "
        "{ state->sign_plus = 1; ++state->position; } else { state->sign_plus = 0; if (c == "
        "'S') ++state->position; } continue; } if (c == 'B' && !has && "
        "(toupper(f2c_format_second(state)) == 'N' || toupper(f2c_format_second(state)) == "
        "'Z')) { state->blank_zero = toupper(f2c_format_second(state)) == 'Z'; "
        "state->position += 2U; continue; } if (c == 'R' && !has && "
        "strchr(\"UDZNCP\", toupper(f2c_format_second(state))) != NULL) { const char *rounds "
        "= \"UDZNCP\"; const char *found = strchr(rounds, toupper(f2c_format_second(state))); "
        "state->rounding = (int)(found - rounds); state->position += 2U; continue; } if (c == "
        "'D' && !has && (toupper(f2c_format_second(state)) == 'C' || "
        "toupper(f2c_format_second(state)) == 'P')) { state->decimal_comma = "
        "toupper(f2c_format_second(state)) == 'C'; state->position += 2U; continue; } ");
}

static void emit_text_parser_descriptor(Buffer *output) {
    f2c_buffer_append(
        output,
        "if (strchr(\"IBOZFEDGLA\", c) == NULL) { state->status = 0; return 0; } repeats = "
        "has && prefix != 0U ? prefix : 1U; memset(descriptor, 0, sizeof(*descriptor)); "
        "descriptor->code[0] = (char)c; ++state->position; if (c == 'E' && "
        "(toupper(f2c_format_peek(state)) == 'N' || toupper(f2c_format_peek(state)) == 'S')) "
        "descriptor->code[1] = (char)toupper(state->text[state->position++]); else if (c == 'D' "
        "&& toupper(f2c_format_peek(state)) == 'T') descriptor->code[1] = "
        "(char)toupper(state->text[state->position++]); if (descriptor->code[0] == 'D' && "
        "descriptor->code[1] == 'T') { if (!f2c_format_dynamic_dt(state, descriptor)) return 0; "
        "} else { f2c_format_skip_spaces(state); prefix = f2c_format_unsigned(state, &has); if "
        "(state->status == 0 || (c != 'A' && !has)) { state->status = 0; return 0; } "
        "descriptor->width = has ? (int)prefix : 0; if (c != 'L') { f2c_format_skip_spaces("
        "state); if (f2c_format_peek(state) == '.') { ++state->position; "
        "f2c_format_skip_spaces(state); prefix = f2c_format_unsigned(state, &has); if (!has || "
        "state->status == 0) { state->status = 0; return 0; } descriptor->digits = (int)prefix; "
        "} else if (c == 'F' || c == 'E' || c == 'D' || (c == 'G' && descriptor->width != "
        "0)) { state->status = 0; return 0; } f2c_format_skip_spaces(state); if ((c == 'E' || "
        "c == 'D' || c == 'G') && toupper(f2c_format_peek(state)) == 'E') { "
        "++state->position; f2c_format_skip_spaces(state); prefix = f2c_format_unsigned(state, "
        "&has); if (!has || state->status == 0) { state->status = 0; return 0; } "
        "descriptor->exponent = (int)prefix; } } } if (!f2c_format_descriptor_boundary("
        "f2c_format_peek(state))) { state->status = 0; return 0; } "
        "f2c_format_mark_yielded(state); state->repeated = *descriptor; "
        "state->repeat_remaining = repeats > 1U ? repeats - 1U : 0U; return 1; } }\n"
        "static inline F2C_UNUSED int f2c_format_next(f2c_format_state *state, "
        "f2c_format_descriptor *descriptor) { if (state == NULL || descriptor == NULL || "
        "state->status == 0 || state->status == EOF) return 0; if (state->repeat_remaining != "
        "0U) { *descriptor = state->repeated; --state->repeat_remaining; return 1; } return "
        "state->program != NULL ? f2c_format_next_program(state, descriptor) : "
        "f2c_format_next_text(state, descriptor); }\n");
}

void f2c_io_emit_format_text_parser_support(Context *context) {
    emit_text_parser_helpers(&context->output);
    emit_dynamic_dt_parser(&context->output);
    emit_text_parser_prefix(&context->output);
    emit_text_parser_controls(&context->output);
    emit_text_parser_descriptor(&context->output);
}
