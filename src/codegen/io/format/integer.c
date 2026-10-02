#include "codegen/io/private.h"

void f2c_io_emit_format_integer_support(Context *context) {
    f2c_buffer_append(
        &context->output,
        "static inline F2C_UNUSED void f2c_format_write_integer(f2c_format_state *state, "
        "int64_t value, int kind) { f2c_format_descriptor descriptor; char digits[64], "
        "*field; size_t length = 0U, minimum, total, offset = 0U; unsigned base = 10U; "
        "uint64_t magnitude; bool negative, plus; if (!f2c_format_next(state, &descriptor)) "
        "return; if (!f2c_format_integer_descriptor(&descriptor)) { state->status = 0; "
        "return; } if (descriptor.code[0] == 'B') base = 2U; else if (descriptor.code[0] "
        "== 'O') base = 8U; else if (descriptor.code[0] == 'Z') base = 16U; negative = "
        "value < 0 && base == 10U; plus = state->sign == F2C_SIGN_PLUS && base == 10U && "
        "!negative; "
        "magnitude = negative ? (uint64_t)(-(value + 1)) + UINT64_C(1) : (uint64_t)value; "
        "if (base != 10U && kind > 0 && kind < 8) magnitude &= (UINT64_C(1) << "
        "((unsigned)kind * CHAR_BIT)) - UINT64_C(1); if (value != 0 || descriptor.digits "
        "!= 0) do { unsigned digit = (unsigned)(magnitude % base); digits[length++] = "
        "(char)(digit < 10U ? '0' + digit : 'A' + digit - 10U); magnitude /= base; } "
        "while (magnitude != 0U); minimum = descriptor.digits > 0 ? (size_t)descriptor.digits "
        ": 0U; total = length > minimum ? length : minimum; if (total != 0U && "
        "(negative || plus)) ++total; if (descriptor.width > 0 && total > "
        "(size_t)descriptor.width) { f2c_format_field(state, \"\", total, descriptor.width); "
        "return; } field = (char *)malloc(total == 0U ? 1U : total); if (field == NULL) "
        "{ state->status = 0; return; } if (total != 0U && (negative || plus)) "
        "field[offset++] = negative ? '-' : '+'; while (offset + length < total) "
        "field[offset++] = '0'; while (length != 0U) field[offset++] = digits[--length]; "
        "f2c_format_field(state, field, total, descriptor.width == 0 && total == 0U ? "
        "1 : descriptor.width); free(field); }\n");
}
