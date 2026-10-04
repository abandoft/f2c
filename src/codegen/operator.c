#include "codegen/operator.h"

#include "internal/f2c.h"

#include <stdlib.h>

static int is_complex(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static const char *complex_suffix(int kind) { return kind == 4 ? "c" : kind == 8 ? "z" : "q"; }

static const char *real_component(int kind) {
    return kind == 4 ? "crealf" : kind == 8 ? "creal" : "creall";
}

static int supported_scalar_type(F2cScalarType type) {
    if (type.type == TYPE_INTEGER || type.type == TYPE_LOGICAL)
        return type.kind == 1 || type.kind == 2 || type.kind == 4 || type.kind == 8;
    if (f2c_type_is_numeric(type.type))
        return type.kind == 4 || type.kind == 8 || type.kind == 16;
    return 0;
}

static char *convert_operand(F2cScalarOperand operand, F2cScalarType target) {
    Buffer output = {0};
    if (operand.type.type == target.type && operand.type.kind == target.kind)
        return f2c_strdup(operand.code);
    if (is_complex(target.type)) {
        if (is_complex(operand.type.type)) {
            f2c_buffer_printf(&output, "f2c_%s_to_%s(%s)", complex_suffix(operand.type.kind),
                              complex_suffix(target.kind), operand.code);
        } else {
            f2c_buffer_printf(&output, "f2c_make_%s((%s)(%s), %s)", complex_suffix(target.kind),
                              f2c_c_type_kind(TYPE_REAL, target.kind), operand.code,
                              target.kind == 4   ? "0.0f"
                              : target.kind == 8 ? "0.0"
                                                 : "0.0L");
        }
    } else if (is_complex(operand.type.type)) {
        f2c_buffer_printf(&output, "((%s)%s(%s))", f2c_c_type_kind(target.type, target.kind),
                          real_component(operand.type.kind), operand.code);
    } else {
        f2c_buffer_printf(&output, "((%s)(%s))", f2c_c_type_kind(target.type, target.kind),
                          operand.code);
    }
    return f2c_buffer_take(&output);
}

static void emit_power(Buffer *output, F2cScalarType result, F2cScalarType exponent,
                       const char *left, const char *right) {
    if (exponent.type == TYPE_INTEGER) {
        if (result.type == TYPE_INTEGER) {
            f2c_buffer_printf(output, "f2c_pow_i%d(%s, (int64_t)(%s))", result.kind * 8, left,
                              right);
        } else if (is_complex(result.type)) {
            f2c_buffer_printf(output, "f2c_pow_%si(%s, (int64_t)(%s))", complex_suffix(result.kind),
                              left, right);
        } else {
            f2c_buffer_printf(output, "f2c_pow_%si(%s, (int64_t)(%s))",
                              result.kind == 4   ? "f"
                              : result.kind == 8 ? "d"
                                                 : "l",
                              left, right);
        }
    } else {
        f2c_buffer_printf(output, "%s(%s, %s)",
                          is_complex(result.type) ? result.kind == 4   ? "cpowf"
                                                    : result.kind == 8 ? "cpow"
                                                                       : "cpowl"
                          : result.kind == 4      ? "powf"
                          : result.kind == 8      ? "pow"
                                                  : "powl",
                          left, right);
    }
}

char *f2c_emit_scalar_operator(F2cOperator operator_kind, int unary, F2cScalarOperand left,
                               F2cScalarOperand right, F2cScalarType expected) {
    F2cOperatorTyping typing;
    Buffer output = {0};
    Buffer narrowed = {0};
    char *converted_left;
    char *converted_right;
    const char *spelling = f2c_operator_c_spelling(operator_kind);
    left.type = f2c_scalar_type(left.type.type, left.type.kind);
    right.type = f2c_scalar_type(right.type.type, right.type.kind);
    expected = f2c_scalar_type(expected.type, expected.kind);
    if (left.code == NULL || (!unary && right.code == NULL) ||
        f2c_operator_typing(operator_kind, unary, left.type, right.type, &typing) !=
            F2C_OPERATOR_VALID ||
        typing.result.type != expected.type || typing.result.kind != expected.kind ||
        !supported_scalar_type(typing.left) || (!unary && !supported_scalar_type(typing.right)))
        return NULL;
    converted_left = convert_operand(left, typing.left);
    converted_right = unary ? NULL : convert_operand(right, typing.right);
    if (converted_left == NULL || (!unary && converted_right == NULL)) {
        free(converted_left);
        free(converted_right);
        return NULL;
    }
    if (unary) {
        if (is_complex(expected.type) && operator_kind == F2C_OPERATOR_SUBTRACT)
            f2c_buffer_printf(&output, "f2c_%sneg(%s)", complex_suffix(expected.kind),
                              converted_left);
        else if (operator_kind == F2C_OPERATOR_ADD && is_complex(expected.type))
            f2c_buffer_printf(&output, "(%s)", converted_left);
        else
            f2c_buffer_printf(&output, "(%s(%s))", spelling, converted_left);
    } else if (operator_kind == F2C_OPERATOR_POWER) {
        /* Preserve the square fast path and evaluate its base only once. */
        if (right.type.type == TYPE_INTEGER && right.code[0] == '2' && right.code[1] == '\0' &&
            expected.type != TYPE_INTEGER) {
            f2c_buffer_printf(&output, "f2c_square_%s(%s)",
                              is_complex(expected.type) ? complex_suffix(expected.kind)
                              : expected.kind == 4      ? "f"
                              : expected.kind == 8      ? "d"
                                                        : "l",
                              converted_left);
        } else {
            emit_power(&output, expected, typing.right, converted_left, converted_right);
        }
    } else if (is_complex(typing.left.type)) {
        const char *suffix = complex_suffix(typing.left.kind);
        if (f2c_operator_is_comparison(operator_kind))
            f2c_buffer_printf(&output, "%sf2c_%seq(%s, %s)",
                              operator_kind == F2C_OPERATOR_NOT_EQUAL ? "!" : "", suffix,
                              converted_left, converted_right);
        else {
            const char *operation = operator_kind == F2C_OPERATOR_ADD        ? "add"
                                    : operator_kind == F2C_OPERATOR_SUBTRACT ? "sub"
                                    : operator_kind == F2C_OPERATOR_MULTIPLY ? "mul"
                                                                             : "div";
            f2c_buffer_printf(&output, "f2c_%s%s(%s, %s)", suffix, operation, converted_left,
                              converted_right);
        }
    } else if (operator_kind == F2C_OPERATOR_EQUIVALENT ||
               operator_kind == F2C_OPERATOR_NOT_EQUIVALENT) {
        f2c_buffer_printf(&output, "(!!(%s) %s !!(%s))", converted_left, spelling, converted_right);
    } else {
        f2c_buffer_printf(&output, "(%s %s %s)", converted_left, spelling, converted_right);
    }
    free(converted_left);
    free(converted_right);
    if (output.failed)
        return f2c_buffer_take(&output);
    if (output.data != NULL &&
        (expected.type == TYPE_LOGICAL || (expected.type == TYPE_INTEGER && expected.kind < 4))) {
        f2c_buffer_printf(&narrowed, "((%s)(%s))", f2c_c_type_kind(expected.type, expected.kind),
                          output.data);
        free(output.data);
        return f2c_buffer_take(&narrowed);
    }
    return f2c_buffer_take(&output);
}
