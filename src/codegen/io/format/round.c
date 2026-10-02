#include "codegen/io/private.h"

void f2c_io_emit_format_round_support(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED int f2c_format_begin_rounding(f2c_format_state *state, double "
        "value, int precision, char conversion, int *saved) { if (f2c_io_begin_rounding("
        "state->rounding, value, conversion == 'E' ? precision + 1 : precision, "
        "conversion == 'E', saved)) return 1; state->status = 0; return 0; }\n"
        "static inline F2C_UNUSED void f2c_format_end_rounding(int saved) { "
        "f2c_io_end_rounding(saved); }\n"
        "static inline F2C_UNUSED char *f2c_format_printf_real(f2c_format_state *state, double "
        "value, int precision, char conversion, size_t *length) { char format[8]; char *field; "
        "int needed; int saved; struct lconv *locale; const char *point; char *decimal; size_t "
        "point_length; if (precision < 0 || precision > 1000000 || (conversion != 'f' && "
        "conversion != 'E')) { state->status = 0; return NULL; } (void)snprintf(format, "
        "sizeof(format), state->sign == F2C_SIGN_PLUS ? \"%%+.*%c\" : \"%%.*%c\", conversion); if "
        "(!f2c_format_begin_rounding(state, value, precision, conversion, &saved)) return NULL; "
        "needed = "
        "snprintf(NULL, 0U, format, precision, value); f2c_format_end_rounding(saved); if "
        "(needed < 0 || (size_t)needed == SIZE_MAX) { state->status = 0; return NULL; } field = "
        "(char *)malloc((size_t)needed + 1U); if (field == NULL) { state->status = 0; return "
        "NULL; } if (!f2c_format_begin_rounding(state, value, precision, conversion, &saved)) { "
        "free(field); return NULL; } (void)snprintf(field, (size_t)needed + 1U, format, "
        "precision, value); f2c_format_end_rounding(saved); *length = (size_t)needed; locale = "
        "localeconv(); point = locale != NULL ? locale->decimal_point : NULL; point_length = "
        "point != NULL ? strlen(point) : 0U; if (point_length != 0U && !(point_length == 1U && "
        "point[0] == '.')) { decimal = strstr(field, point); if (decimal != NULL) { size_t "
        "offset = (size_t)(decimal - field); decimal[0] = '.'; if (point_length > 1U) { "
        "memmove(decimal + 1U, decimal + point_length, *length - offset - point_length + 1U); "
        "*length -= point_length - 1U; } } } return field; }\n");
}
