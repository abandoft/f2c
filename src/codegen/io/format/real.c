#include "codegen/io/private.h"

static void emit_fixed_renderer(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_fixed_real(f2c_format_state *state, double "
        "value, int precision, size_t *length) { char *field; int needed; if (precision < 0 || "
        "precision > 1000000) { state->status = 0; return NULL; } needed = snprintf(NULL, 0U, "
        "state->sign_plus ? \"%+.*f\" : \"%.*f\", precision, value); if (needed < 0 || "
        "(size_t)needed == SIZE_MAX) { state->status = 0; return NULL; } field = (char "
        "*)malloc((size_t)needed + 1U); if (field == NULL) { state->status = 0; return NULL; } "
        "(void)snprintf(field, (size_t)needed + 1U, state->sign_plus ? \"%+.*f\" : \"%.*f\", "
        "precision, value); *length = (size_t)needed; return field; }\n"
        "static inline F2C_UNUSED void f2c_format_decimal_comma(f2c_format_state *state, char "
        "*field, size_t length) { size_t index; if (!state->decimal_comma) return; for (index = "
        "0U; index < length; ++index) if (field[index] == '.') field[index] = ','; }\n");
}

static void emit_exponential_renderer(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_exponential_real(f2c_format_state *state, "
        "const f2c_format_descriptor *descriptor, double value, size_t *length) { char "
        "*mantissa; char *field; size_t mantissa_length; int scale = state->scale; int "
        "fraction; int exponent = 0; int exponent_digits; int exponent_length; int absolute; "
        "bool omit_marker; size_t total; char exponent_buffer[32]; char marker = "
        "descriptor->code[0] == 'D' ? 'D' : 'E'; if (!isfinite(value)) { int needed = "
        "snprintf(NULL, 0U, state->sign_plus ? "
        "\"%+.*E\" : \"%.*E\", descriptor->digits, value); if (needed < 0) { state->status = "
        "0; return NULL; } field = (char *)malloc((size_t)needed + 1U); if (field == NULL) { "
        "state->status = 0; return NULL; } (void)snprintf(field, (size_t)needed + 1U, "
        "state->sign_plus ? \"%+.*E\" : \"%.*E\", descriptor->digits, value); *length = "
        "(size_t)needed; return field; } if (descriptor->code[1] == 'S') scale = 1; if (value != "
        "0.0) { int base = (int)floor(log10(fabs(value))); if (descriptor->code[1] == 'N') { "
        "int remainder = base % 3; if (remainder < 0) remainder += 3; exponent = base - "
        "remainder; scale = base - exponent + 1; } else exponent = base - scale + 1; value = "
        "copysign(pow(10.0, log10(fabs(value)) - (double)exponent), value); } fraction = "
        "descriptor->digits; if (scale > 1) fraction "
        "-= scale - 1; if (fraction < 0) fraction = 0; mantissa = "
        "f2c_format_fixed_real(state, value, fraction, &mantissa_length); if (mantissa == NULL) "
        "return NULL; absolute = exponent < 0 ? -exponent : exponent; exponent_digits = "
        "descriptor->exponent > 0 ? descriptor->exponent : 2; exponent_length = "
        "snprintf(exponent_buffer, sizeof(exponent_buffer), \"%0*d\", exponent_digits, "
        "absolute); if (exponent_length < 0 || (size_t)exponent_length >= "
        "sizeof(exponent_buffer) || (descriptor->exponent > 0 && exponent_length > "
        "descriptor->exponent)) { free(mantissa); state->status = 0; return NULL; } omit_marker "
        "= descriptor->exponent == 0 && absolute >= 100; total = mantissa_length + "
        "(size_t)exponent_length + (omit_marker ? 1U : 2U); if (total < mantissa_length) { "
        "free(mantissa); state->status = 0; return NULL; } field = (char *)malloc(total + 1U); "
        "if (field == NULL) { free(mantissa); state->status = 0; return NULL; } "
        "memcpy(field, mantissa, mantissa_length); if (omit_marker) { field[mantissa_length] = "
        "exponent < 0 ? '-' : '+'; memcpy(field + mantissa_length + 1U, exponent_buffer, "
        "(size_t)exponent_length); } else { field[mantissa_length] = marker; "
        "field[mantissa_length + 1U] = exponent < 0 ? '-' : '+'; memcpy(field + "
        "mantissa_length + 2U, exponent_buffer, (size_t)exponent_length); } field[total] = '\\0'; "
        "free(mantissa); *length = total; return field; }\n");
}

static void emit_general_renderer(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_general_real(f2c_format_state *state, const "
        "f2c_format_descriptor *descriptor, double value, size_t *length) { char *field; int "
        "exponent = value != 0.0 && isfinite(value) ? (int)floor(log10(fabs(value))) : 0; if "
        "(descriptor->digits == 0) { int needed = snprintf(NULL, 0U, state->sign_plus ? "
        "\"%+.17G\" : \"%.17G\", value); if (needed < 0) { state->status = 0; return NULL; } "
        "field = (char *)malloc((size_t)needed + 1U); if (field == NULL) { state->status = 0; "
        "return NULL; } (void)snprintf(field, (size_t)needed + 1U, state->sign_plus ? "
        "\"%+.17G\" : \"%.17G\", value); *length = (size_t)needed; return field; } if "
        "(isfinite(value) && (value == 0.0 || (exponent >= -1 && exponent < "
        "descriptor->digits))) { int precision = descriptor->digits - exponent - 1; size_t "
        "trailing = descriptor->exponent > 0 ? (size_t)descriptor->exponent + 2U : 4U; size_t "
        "base_length; char *replacement; field = f2c_format_fixed_real(state, value, precision, "
        "&base_length); if (field == NULL || base_length > SIZE_MAX - trailing - 1U) { "
        "free(field); state->status = 0; return NULL; } replacement = (char *)realloc(field, "
        "base_length + trailing + 1U); if (replacement == NULL) { free(field); state->status = "
        "0; return NULL; } field = replacement; memset(field + base_length, ' ', trailing); "
        "*length = base_length + trailing; field[*length] = '\\0'; return field; } { "
        "f2c_format_descriptor exponential = *descriptor; exponential.code[0] = 'E'; "
        "exponential.code[1] = '\\0'; return f2c_format_exponential_real(state, &exponential, "
        "value, length); } }\n");
}

void f2c_io_emit_format_real_support(Context *context) {
    emit_fixed_renderer(&context->output);
    emit_exponential_renderer(&context->output);
    emit_general_renderer(&context->output);
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED char *f2c_format_render_real(f2c_format_state *state, const "
        "f2c_format_descriptor *descriptor, double value, size_t *length) { char *field; if "
        "(descriptor->code[0] == 'F') field = f2c_format_fixed_real(state, value * pow(10.0, "
        "(double)state->scale), descriptor->digits, length); else if (descriptor->code[0] == "
        "'G') field = f2c_format_general_real(state, descriptor, value, length); else field = "
        "f2c_format_exponential_real(state, descriptor, value, length); if (field != NULL) "
        "f2c_format_decimal_comma(state, field, *length); return field; }\n");
}
