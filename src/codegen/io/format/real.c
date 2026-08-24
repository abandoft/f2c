#include "codegen/io/private.h"

static void emit_real_utilities(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_real_special(f2c_format_state *state, double "
        "value, size_t *length) { const char *word; size_t prefix = 0U; char *field; if "
        "(isnan(value)) word = \"NaN\"; else if (isinf(value)) { word = \"Infinity\"; prefix = "
        "signbit(value) || state->sign_plus ? 1U : 0U; } else return NULL; *length = "
        "strlen(word) + prefix; field = (char *)malloc(*length + 1U); if (field == NULL) { "
        "state->status = 0; return NULL; } if (prefix != 0U) field[0] = signbit(value) ? '-' : "
        "'+'; memcpy(field + prefix, word, strlen(word) + 1U); return field; }\n"
        "static inline F2C_UNUSED char *f2c_format_real_overflow(f2c_format_state *state, const "
        "f2c_format_descriptor *descriptor, size_t *length) { char *field; if "
        "(descriptor->width <= 0) { state->status = 0; return NULL; } field = (char *)malloc(1U); "
        "if (field == NULL) { state->status = 0; return NULL; } field[0] = '\\0'; *length = "
        "(size_t)descriptor->width + 1U; return field; }\n"
        "static inline F2C_UNUSED double f2c_format_scale_power10(double value, int exponent) { "
        "if (value == 0.0 || !isfinite(value) || exponent == 0) return value; if (exponent > "
        "400) return copysign(0.0, value); if (exponent < -400) return copysign(INFINITY, value); "
        "while (exponent > 300) { value /= 1.0e300; exponent -= 300; } while (exponent < -300) "
        "{ value *= 1.0e300; exponent += 300; } return value / pow(10.0, (double)exponent); }\n"
        "static inline F2C_UNUSED int f2c_format_decimal_exponent(double value) { int exponent; "
        "double magnitude; if (value == 0.0 || !isfinite(value)) return 0; magnitude = "
        "fabs(value); exponent = (int)floor(log10(magnitude)); if (exponent > -308 && exponent "
        "< 308) { double power = pow(10.0, (double)exponent); if (magnitude < power) "
        "--exponent; else if (magnitude >= power * 10.0) ++exponent; } return exponent; }\n"
        "static inline F2C_UNUSED void f2c_format_compact_real(char *field, size_t *length, int "
        "width) { size_t zero = field[0] == '+' || field[0] == '-' ? 1U : 0U; if (width > 0 && "
        "*length > (size_t)width && zero + 1U < *length && field[zero] == '0' && field[zero + "
        "1U] == '.') { memmove(field + zero, field + zero + 1U, *length - zero); --*length; } }\n"
        "static inline F2C_UNUSED void f2c_format_decimal_comma(f2c_format_state *state, char "
        "*field, size_t length) { size_t index; if (!state->decimal_comma) return; for (index = "
        "0U; index < length; ++index) if (field[index] == '.') field[index] = ','; }\n");
}

static void emit_fixed_renderer(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_fixed_real(f2c_format_state *state, double "
        "value, int precision, size_t *length) { char *special = "
        "f2c_format_real_special(state, value, length); if (special != NULL || !isfinite(value)) "
        "return special; return f2c_format_printf_real(state, value, precision, 'f', length); "
        "}\n");
}

static void emit_exponential_renderer(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_exponential_real(f2c_format_state *state, "
        "const f2c_format_descriptor *descriptor, double value, size_t *length) { char "
        "*mantissa; char *field; char exponent_buffer[32]; size_t mantissa_length; size_t "
        "exponent_width; size_t exponent_length; size_t total; int scale = state->scale; int "
        "fraction = descriptor->digits; int exponent = 0; int exponent_step = 1; int base; int "
        "absolute; int rendered; double scaled = value; double upper; double rounded; bool "
        "engineering = descriptor->code[0] == 'E' && descriptor->code[1] == 'N'; bool "
        "scientific = descriptor->code[0] == 'E' && descriptor->code[1] == 'S'; bool "
        "omit_marker; char marker = descriptor->code[0] == 'D' ? 'D' : 'E'; if "
        "(!isfinite(value)) return f2c_format_real_special(state, value, length); if "
        "(descriptor->digits < 0 || descriptor->digits > 1000000) { state->status = 0; return "
        "NULL; } base = f2c_format_decimal_exponent(value); if (engineering) { int remainder = "
        "base % 3; if (remainder < 0) remainder += 3; exponent = base - remainder; exponent_step "
        "= 3; scale = remainder + 1; upper = 1000.0; } else { if (scientific) scale = 1; "
        "exponent = value != 0.0 ? base - scale + 1 : 0; upper = pow(10.0, (double)scale); } if "
        "(value != 0.0) scaled = f2c_format_scale_power10(value, exponent); if (!engineering && "
        "scale > 1) fraction -= scale - 1; if (fraction < 0) return "
        "f2c_format_real_overflow(state, descriptor, length); mantissa = "
        "f2c_format_fixed_real(state, scaled, fraction, &mantissa_length); if (mantissa == NULL) "
        "return NULL; rounded = strtod(mantissa, NULL); if (isfinite(rounded) && fabs(rounded) >= "
        "upper) { free(mantissa); scaled = f2c_format_scale_power10(scaled, exponent_step); "
        "exponent += exponent_step; mantissa = f2c_format_fixed_real(state, scaled, fraction, "
        "&mantissa_length); if (mantissa == NULL) return NULL; } absolute = exponent < 0 ? "
        "-exponent : exponent; rendered = snprintf(exponent_buffer, sizeof(exponent_buffer), "
        "\"%d\", absolute); if (rendered < 0 || (size_t)rendered >= sizeof(exponent_buffer)) { "
        "free(mantissa); state->status = 0; return NULL; } exponent_length = (size_t)rendered; if "
        "(descriptor->has_exponent && descriptor->exponent > 0 && exponent_length > "
        "(size_t)descriptor->exponent) { "
        "free(mantissa); return f2c_format_real_overflow(state, descriptor, length); } "
        "exponent_width = descriptor->has_exponent && descriptor->exponent > 0 ? "
        "(size_t)descriptor->exponent : (descriptor->has_exponent && descriptor->exponent == "
        "0 ? exponent_length : (exponent_length < 2U ? 2U : exponent_length)); "
        "omit_marker = !descriptor->has_exponent && exponent_length > 2U; if (mantissa_length > "
        "SIZE_MAX - exponent_width - "
        "(omit_marker ? 1U : 2U)) { free(mantissa); state->status = 0; return NULL; } total = "
        "mantissa_length + exponent_width + (omit_marker ? 1U : 2U); field = (char "
        "*)malloc(total + 1U); if (field == NULL) { free(mantissa); state->status = 0; return "
        "NULL; } memcpy(field, mantissa, mantissa_length); if (!omit_marker) "
        "field[mantissa_length++] = marker; field[mantissa_length++] = exponent < 0 ? '-' : "
        "'+'; memset(field + mantissa_length, '0', exponent_width - exponent_length); memcpy(field "
        "+ mantissa_length + exponent_width - exponent_length, exponent_buffer, "
        "exponent_length); field[total] = '\\0'; free(mantissa); *length = total; return field; "
        "}\n");
}

static void emit_general_renderer(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED char *f2c_format_general_real(f2c_format_state *state, const "
        "f2c_format_descriptor *descriptor, double value, int significant_digits, size_t "
        "*length) { f2c_format_descriptor effective = *descriptor; char *field; int exponent; "
        "int precision; int rounded_exponent; int digits = descriptor->digits; size_t trailing; "
        "size_t base_length; char *replacement; if (!isfinite(value)) return "
        "f2c_format_real_special(state, value, length); if (descriptor->width == 0 && digits == "
        "0) digits = significant_digits; if (digits <= 0) digits = 1; exponent = "
        "f2c_format_decimal_exponent(value); if (value == 0.0 || (exponent >= -1 && exponent < "
        "digits)) { precision = digits - exponent - 1; field = f2c_format_fixed_real(state, "
        "value, precision, &base_length); if (field == NULL) return NULL; rounded_exponent = "
        "f2c_format_decimal_exponent(strtod(field, NULL)); if (value != 0.0 && rounded_exponent "
        "> exponent && rounded_exponent < digits) { free(field); precision = digits - "
        "rounded_exponent - 1; field = f2c_format_fixed_real(state, value, precision, "
        "&base_length); if (field == NULL) return NULL; } if (precision == 0 && strchr(field, "
        "'.') == NULL) { replacement = (char *)realloc(field, base_length + 2U); if "
        "(replacement == NULL) { free(field); state->status = 0; return NULL; } field = "
        "replacement; field[base_length++] = '.'; field[base_length] = '\\0'; } trailing = "
        "descriptor->width == 0 ? 0U : (descriptor->has_exponent ? "
        "(size_t)descriptor->exponent + 2U : 4U); if (base_length > SIZE_MAX - trailing - 1U) "
        "{ free(field); state->status = 0; return NULL; } replacement = (char *)realloc(field, "
        "base_length + trailing + 1U); if (replacement == NULL) { free(field); state->status = "
        "0; return NULL; } field = replacement; memset(field + base_length, ' ', trailing); "
        "*length = base_length + trailing; field[*length] = '\\0'; return field; } "
        "effective.code[0] = 'E'; effective.code[1] = '\\0'; effective.digits = digits; if "
        "(descriptor->width == 0) { effective.exponent = -1; effective.has_exponent = true; } "
        "return "
        "f2c_format_exponential_real(state, &effective, value, length); }\n");
}

void f2c_io_emit_format_real_support(Context *context) {
    emit_real_utilities(&context->output);
    emit_fixed_renderer(&context->output);
    emit_exponential_renderer(&context->output);
    emit_general_renderer(&context->output);
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED char *f2c_format_render_real(f2c_format_state *state, const "
        "f2c_format_descriptor *descriptor, double value, int significant_digits, size_t "
        "*length) { char *field; if (descriptor->code[0] == 'F') { double scaled = "
        "f2c_format_scale_power10(value, -state->scale); field = isfinite(value) && "
        "!isfinite(scaled) ? f2c_format_real_overflow(state, descriptor, length) : "
        "f2c_format_fixed_real(state, scaled, descriptor->digits, length); } else if "
        "(descriptor->code[0] == 'G') field = f2c_format_general_real(state, descriptor, value, "
        "significant_digits, length); else field = f2c_format_exponential_real(state, "
        "descriptor, value, length); if (field != NULL) { "
        "f2c_format_compact_real(field, length, descriptor->width); "
        "f2c_format_decimal_comma(state, field, *length); } return field; }\n");
}
