#include "internal/f2c.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *operator_c(const char *op) {
    if (strcmp(op, ".eq.") == 0)
        return "==";
    if (strcmp(op, ".ne.") == 0)
        return "!=";
    if (strcmp(op, ".lt.") == 0)
        return "<";
    if (strcmp(op, ".le.") == 0)
        return "<=";
    if (strcmp(op, ".gt.") == 0)
        return ">";
    if (strcmp(op, ".ge.") == 0)
        return ">=";
    if (strcmp(op, ".and.") == 0)
        return "&&";
    if (strcmp(op, ".or.") == 0)
        return "||";
    if (strcmp(op, ".eqv.") == 0)
        return "==";
    if (strcmp(op, ".neqv.") == 0)
        return "!=";
    if (strcmp(op, "/=") == 0)
        return "!=";
    return op;
}

static int precedence(const char *op) {
    if (strcmp(op, ".or.") == 0 || strcmp(op, ".eqv.") == 0 || strcmp(op, ".neqv.") == 0)
        return 1;
    if (strcmp(op, ".and.") == 0)
        return 2;
    if (strcmp(op, "==") == 0 || strcmp(op, "/=") == 0 || strcmp(op, "<") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, ".eq.") == 0 || strcmp(op, ".ne.") == 0 || strcmp(op, ".lt.") == 0 ||
        strcmp(op, ".le.") == 0 || strcmp(op, ".gt.") == 0 || strcmp(op, ".ge.") == 0)
        return 3;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "//") == 0)
        return 4;
    if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0)
        return 5;
    if (strcmp(op, "**") == 0)
        return 6;
    return 0;
}

static void append_character_elements(Buffer *output, const char *value, size_t *count) {
    const size_t length = strlen(value);
    if (value[0] == '"' && length >= 2U && value[length - 1U] == '"') {
        size_t i;
        for (i = 0U; i + 2U <= length; ++i) {
            if (i + 2U == length)
                break;
            f2c_buffer_printf(output, "%s(char)((%s)[%zu])", *count == 0U ? "" : ", ", value, i);
            ++*count;
        }
    } else if (strncmp(value, "(char[", 6U) == 0) {
        const size_t array_length = (size_t)strtoul(value + 6, NULL, 10);
        const char *open = strstr(value, "]){");
        const char *terminator = strrchr(value, ',');
        if (array_length > 0U && open != NULL && terminator != NULL && terminator > open + 3) {
            f2c_buffer_printf(output, "%s", *count == 0U ? "" : ", ");
            f2c_buffer_append_n(output, open + 3, (size_t)(terminator - (open + 3)));
            *count += array_length - 1U;
        }
    } else {
        f2c_buffer_printf(output, "%s(char)(%s)", *count == 0U ? "" : ", ", value);
        ++*count;
    }
}

char *f2c_emit_numeric_conversion(const char *operand, Type actual, Type target) {
    Buffer converted = {0};
    if (actual == target || !f2c_type_is_numeric(actual) || !f2c_type_is_numeric(target))
        return f2c_strdup(operand);
    if (target == TYPE_COMPLEX || target == TYPE_DOUBLE_COMPLEX) {
        const int double_precision = target == TYPE_DOUBLE_COMPLEX;
        if (actual == TYPE_COMPLEX || actual == TYPE_DOUBLE_COMPLEX) {
            f2c_buffer_printf(&converted, "%s(%s)", double_precision ? "f2c_c_to_z" : "f2c_z_to_c",
                              operand);
        } else {
            f2c_buffer_printf(
                &converted, "%s((%s)(%s), %s)", double_precision ? "f2c_make_z" : "f2c_make_c",
                double_precision ? "double" : "float", operand, double_precision ? "0.0" : "0.0f");
        }
    } else if (actual == TYPE_COMPLEX || actual == TYPE_DOUBLE_COMPLEX) {
        f2c_buffer_printf(&converted, "((%s)%s(%s))", f2c_c_type(target),
                          actual == TYPE_COMPLEX ? "crealf" : "creal", operand);
    } else {
        f2c_buffer_printf(&converted, "((%s)(%s))", f2c_c_type(target), operand);
    }
    return f2c_buffer_take(&converted);
}

char *f2c_emit_scalar_temporary_address(const char *c_type, Type type, const char *value) {
    Buffer result = {0};
    if (type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX)
        f2c_buffer_printf(&result, "&((%s[]){%s})[0]", c_type, value);
    else
        f2c_buffer_printf(&result, "&(%s){%s}", c_type, value);
    return f2c_buffer_take(&result);
}

char *f2c_emit_binary(Unit *unit, const char *left, Type left_type, const char *op,
                      const char *right, Type right_type, Type *result_type) {
    Buffer result = {0};
    char normalized_operator[16];
    size_t operator_length = 0U;
    const char *operator_cursor;
    for (operator_cursor = op;
         *operator_cursor != '\0' && operator_length + 1U < sizeof(normalized_operator);
         ++operator_cursor) {
        if (!isspace((unsigned char)*operator_cursor))
            normalized_operator[operator_length++] = *operator_cursor;
    }
    normalized_operator[operator_length] = '\0';
    op = normalized_operator;
    const int comparison = precedence(op) == 3;
    (void)unit;
    *result_type = comparison ? TYPE_LOGICAL : f2c_common_numeric_type(left_type, right_type);
    if (strcmp(op, "**") == 0) {
        *result_type = left_type;
        if (right_type == TYPE_INTEGER && strcmp(right, "2") == 0 &&
            (left_type == TYPE_REAL || left_type == TYPE_DOUBLE || left_type == TYPE_COMPLEX ||
             left_type == TYPE_DOUBLE_COMPLEX)) {
            const char *helper = left_type == TYPE_REAL      ? "f2c_square_f"
                                 : left_type == TYPE_DOUBLE  ? "f2c_square_d"
                                 : left_type == TYPE_COMPLEX ? "f2c_square_c"
                                                             : "f2c_square_z";
            f2c_buffer_printf(&result, "%s(%s)", helper, left);
        } else if (left_type == TYPE_INTEGER)
            f2c_buffer_printf(&result, "((int32_t)pow((double)(%s), (double)(%s)))", left, right);
        else if (left_type == TYPE_REAL)
            f2c_buffer_printf(&result, "powf((float)(%s), (float)(%s))", left, right);
        else if (left_type == TYPE_COMPLEX || left_type == TYPE_DOUBLE_COMPLEX) {
            char *converted_right = f2c_emit_numeric_conversion(right, right_type, left_type);
            f2c_buffer_printf(&result, "%s(%s, %s)", left_type == TYPE_COMPLEX ? "cpowf" : "cpow",
                              left, converted_right);
            free(converted_right);
        } else
            f2c_buffer_printf(&result, "pow((double)(%s), (double)(%s))", left, right);
    } else if (left_type == TYPE_COMPLEX || left_type == TYPE_DOUBLE_COMPLEX ||
               right_type == TYPE_COMPLEX || right_type == TYPE_DOUBLE_COMPLEX) {
        const Type common_type = f2c_common_numeric_type(left_type, right_type);
        const int double_precision = common_type == TYPE_DOUBLE_COMPLEX;
        const char *c_operator = operator_c(op);
        char *converted_left = f2c_emit_numeric_conversion(left, left_type, common_type);
        char *converted_right = f2c_emit_numeric_conversion(right, right_type, common_type);
        if (strcmp(c_operator, "==") == 0 || strcmp(c_operator, "!=") == 0) {
            f2c_buffer_printf(&result, "%s%s(%s, %s)", strcmp(c_operator, "!=") == 0 ? "!" : "",
                              double_precision ? "f2c_zeq" : "f2c_ceq", converted_left,
                              converted_right);
            *result_type = TYPE_LOGICAL;
        } else {
            const char *helper =
                strcmp(c_operator, "+") == 0   ? (double_precision ? "f2c_zadd" : "f2c_cadd")
                : strcmp(c_operator, "-") == 0 ? (double_precision ? "f2c_zsub" : "f2c_csub")
                : strcmp(c_operator, "*") == 0 ? (double_precision ? "f2c_zmul" : "f2c_cmul")
                                               : (double_precision ? "f2c_zdiv" : "f2c_cdiv");
            f2c_buffer_printf(&result, "%s(%s, %s)", helper, converted_left, converted_right);
        }
        free(converted_left);
        free(converted_right);
    } else if (strcmp(op, "//") == 0) {
        Buffer elements = {0};
        size_t element_count = 0U;
        append_character_elements(&elements, left, &element_count);
        append_character_elements(&elements, right, &element_count);
        f2c_buffer_printf(&result, "(char[%zu]){%s, '\\0'}", element_count + 1U,
                          elements.data != NULL ? elements.data : "");
        *result_type = TYPE_CHARACTER;
        free(f2c_buffer_take(&elements));
    } else {
        const int logical_operator = strcmp(op, ".and.") == 0 || strcmp(op, ".or.") == 0 ||
                                     strcmp(op, ".eqv.") == 0 || strcmp(op, ".neqv.") == 0;
        const Type common_type =
            logical_operator ? TYPE_UNKNOWN : f2c_common_numeric_type(left_type, right_type);
        if (logical_operator)
            *result_type = TYPE_LOGICAL;
        char *converted_left = f2c_emit_numeric_conversion(left, left_type, common_type);
        char *converted_right = f2c_emit_numeric_conversion(right, right_type, common_type);
        f2c_buffer_printf(&result, "(%s %s %s)", converted_left, operator_c(op), converted_right);
        free(converted_left);
        free(converted_right);
    }
    return f2c_buffer_take(&result);
}

char *f2c_emit_intrinsic(const char *name, char **args, const Type *argument_types, size_t count,
                         Type result_type) {
    Buffer result = {0};
    const char *mapped = name;
    size_t i;
    if (strcmp(name, "abs") == 0 || strcmp(name, "dabs") == 0)
        mapped = count != 0U && argument_types[0] == TYPE_INTEGER ? "abs" : "F2C_ABS";
    else if (strcmp(name, "mod") == 0)
        mapped = "F2C_MOD";
    else if (strcmp(name, "dsqrt") == 0)
        mapped = "sqrt";
    else if (strcmp(name, "dexp") == 0)
        mapped = "exp";
    else if (strcmp(name, "dlog") == 0)
        mapped = "log";
    else if (strcmp(name, "dsin") == 0)
        mapped = "sin";
    else if (strcmp(name, "dcos") == 0)
        mapped = "cos";
    else if (strcmp(name, "conjg") == 0 || strcmp(name, "dconjg") == 0)
        mapped = count != 0U && argument_types[0] == TYPE_COMPLEX ? "conjf" : "conj";
    else if (strcmp(name, "aimag") == 0)
        mapped = count != 0U && argument_types[0] == TYPE_COMPLEX ? "cimagf" : "cimag";
    else if (strcmp(name, "dimag") == 0)
        mapped = "cimag";
    else if (strcmp(name, "dreal") == 0)
        mapped = "creal";
    else if (strcmp(name, "cabs") == 0 || strcmp(name, "cdabs") == 0)
        mapped = count != 0U && argument_types[0] == TYPE_COMPLEX ? "cabsf" : "cabs";
    if (count != 0U && argument_types[0] == TYPE_REAL) {
        if (strcmp(name, "sqrt") == 0)
            mapped = "sqrtf";
        else if (strcmp(name, "sin") == 0)
            mapped = "sinf";
        else if (strcmp(name, "cos") == 0)
            mapped = "cosf";
        else if (strcmp(name, "tan") == 0)
            mapped = "tanf";
        else if (strcmp(name, "exp") == 0)
            mapped = "expf";
        else if (strcmp(name, "log") == 0)
            mapped = "logf";
        else if (strcmp(name, "log10") == 0)
            mapped = "log10f";
        else if (strcmp(name, "atan") == 0)
            mapped = "atanf";
        else if (strcmp(name, "asin") == 0)
            mapped = "asinf";
        else if (strcmp(name, "acos") == 0)
            mapped = "acosf";
        else if (strcmp(name, "atan2") == 0)
            mapped = "atan2f";
    } else if (count != 0U &&
               (argument_types[0] == TYPE_COMPLEX || argument_types[0] == TYPE_DOUBLE_COMPLEX)) {
        const int single = argument_types[0] == TYPE_COMPLEX;
        if (strcmp(name, "sqrt") == 0)
            mapped = single ? "csqrtf" : "csqrt";
        else if (strcmp(name, "exp") == 0)
            mapped = single ? "cexpf" : "cexp";
        else if (strcmp(name, "log") == 0)
            mapped = single ? "clogf" : "clog";
        else if (strcmp(name, "sin") == 0)
            mapped = single ? "csinf" : "csin";
        else if (strcmp(name, "cos") == 0)
            mapped = single ? "ccosf" : "ccos";
    }
    if (strcmp(name, "max") == 0 || strcmp(name, "min") == 0) {
        const char *macro = strcmp(name, "max") == 0 ? "F2C_FORTRAN_MAX" : "F2C_FORTRAN_MIN";
        if (count == 0U) {
            f2c_buffer_append(&result, "0");
        } else {
            f2c_buffer_append(&result, args[0]);
            for (i = 1U; i < count; ++i) {
                char *previous = f2c_buffer_take(&result);
                f2c_buffer_printf(&result, "%s(%s, %s)", macro, previous, args[i]);
                free(previous);
            }
        }
    } else if (strcmp(name, "real") == 0 && count != 0U &&
               (argument_types[0] == TYPE_COMPLEX || argument_types[0] == TYPE_DOUBLE_COMPLEX)) {
        const char *real_type = result_type == TYPE_DOUBLE ? "double" : "float";
        f2c_buffer_printf(&result, "((%s)%s(%s))", real_type,
                          argument_types[0] == TYPE_COMPLEX ? "crealf" : "creal", args[0]);
    } else if (strcmp(name, "dble") == 0 || strcmp(name, "real") == 0 ||
               strcmp(name, "float") == 0 || strcmp(name, "int") == 0) {
        const char *cast =
            strcmp(name, "dble") == 0
                ? "double"
                : (strcmp(name, "real") == 0 && result_type == TYPE_DOUBLE
                       ? "double"
                       : ((strcmp(name, "real") == 0 || strcmp(name, "float") == 0) ? "float"
                                                                                    : "int32_t"));
        f2c_buffer_printf(&result, "((%s)(%s))", cast, count != 0U ? args[0] : "0");
    } else if (strcmp(name, "nint") == 0 || strcmp(name, "idnint") == 0) {
        f2c_buffer_printf(&result, "((int32_t)lrint(%s))", count != 0U ? args[0] : "0");
    } else if (strcmp(name, "cmplx") == 0 || strcmp(name, "dcmplx") == 0) {
        const int double_precision =
            strcmp(name, "dcmplx") == 0 || result_type == TYPE_DOUBLE_COMPLEX;
        if (count >= 2U) {
            /* Construct the components independently.  The algebraic spelling
             * `real + imag * I` contaminates the real component when imag is
             * infinite because IEEE 0*Inf is NaN. */
            f2c_buffer_printf(&result, "%s((%s)(%s), (%s)(%s))",
                              double_precision ? "f2c_make_z" : "f2c_make_c",
                              double_precision ? "double" : "float", args[0],
                              double_precision ? "double" : "float", args[1]);
        } else {
            char *converted = f2c_emit_numeric_conversion(
                count != 0U ? args[0] : "0", count != 0U ? argument_types[0] : TYPE_INTEGER,
                double_precision ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX);
            f2c_buffer_append(&result, converted);
            free(converted);
        }
    } else if (strcmp(name, "ichar") == 0) {
        const char *value = count != 0U ? args[0] : "0";
        if (value[0] == '"')
            f2c_buffer_printf(&result, "((int32_t)(unsigned char)%s[0])", value);
        else
            f2c_buffer_printf(&result, "((int32_t)(unsigned char)(%s))", value);
    } else if (strcmp(name, "char") == 0) {
        f2c_buffer_printf(&result, "((char)(%s))", count != 0U ? args[0] : "0");
    } else if (strcmp(name, "len") == 0 || strcmp(name, "len_trim") == 0) {
        const char *value = count != 0U ? args[0] : "\"\"";
        const size_t length = strlen(value);
        if (length >= 4U && value[0] == '(' && value[1] == '*' && value[length - 1U] == ')') {
            f2c_buffer_append(&result, "((int32_t)strlen(");
            f2c_buffer_append_n(&result, value + 2, length - 3U);
            f2c_buffer_append(&result, "))");
        } else {
            f2c_buffer_printf(&result, "((int32_t)strlen(%s))", value);
        }
    } else if (strcmp(name, "sign") == 0 || strcmp(name, "dsign") == 0) {
        if (strcmp(name, "sign") == 0 && count != 0U && argument_types[0] == TYPE_REAL)
            f2c_buffer_printf(&result, "copysignf(fabsf(%s), %s)", count >= 1U ? args[0] : "0.0f",
                              count >= 2U ? args[1] : "0.0f");
        else
            f2c_buffer_printf(&result, "copysign(fabs((double)(%s)), (double)(%s))",
                              count >= 1U ? args[0] : "0", count >= 2U ? args[1] : "0");
    } else if (strcmp(name, "exponent") == 0) {
        f2c_buffer_printf(&result, "((int32_t)(ilogb(fabs((double)(%s))) + 1))",
                          count != 0U ? args[0] : "0");
    } else if (strcmp(name, "ceiling") == 0) {
        f2c_buffer_printf(&result, "((int32_t)ceil((double)(%s)))", count != 0U ? args[0] : "0");
    } else if (strcmp(name, "floor") == 0) {
        f2c_buffer_printf(&result, "((int32_t)floor((double)(%s)))", count != 0U ? args[0] : "0");
    } else if (strcmp(name, "huge") == 0) {
        f2c_buffer_printf(&result,
                          "_Generic((%s), float: FLT_MAX, double: DBL_MAX, int32_t: "
                          "INT32_MAX, default: DBL_MAX)",
                          count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "tiny") == 0) {
        f2c_buffer_printf(&result,
                          "_Generic((%s), float: FLT_MIN, double: DBL_MIN, default: DBL_MIN)",
                          count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "epsilon") == 0) {
        f2c_buffer_printf(
            &result,
            "_Generic((%s), float: FLT_EPSILON, double: DBL_EPSILON, default: DBL_EPSILON)",
            count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "radix") == 0) {
        f2c_buffer_append(&result, "FLT_RADIX");
    } else if (strcmp(name, "digits") == 0) {
        f2c_buffer_printf(&result,
                          "_Generic((%s), float: FLT_MANT_DIG, double: DBL_MANT_DIG, "
                          "default: DBL_MANT_DIG)",
                          count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "minexponent") == 0) {
        f2c_buffer_printf(&result,
                          "_Generic((%s), float: FLT_MIN_EXP, double: DBL_MIN_EXP, "
                          "default: DBL_MIN_EXP)",
                          count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "maxexponent") == 0) {
        f2c_buffer_printf(&result,
                          "_Generic((%s), float: FLT_MAX_EXP, double: DBL_MAX_EXP, "
                          "default: DBL_MAX_EXP)",
                          count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "kind") == 0) {
        f2c_buffer_printf(&result,
                          "_Generic((%s), float: 4, double: 8, f2c_complex_float: 4, "
                          "f2c_complex_double: 8, default: 4)",
                          count != 0U ? args[0] : "0.0f");
    } else if (strcmp(name, "alog") == 0) {
        f2c_buffer_printf(&result, "logf(%s)", count != 0U ? args[0] : "0");
    } else if (strcmp(name, "log10") == 0) {
        f2c_buffer_printf(&result, "%s(%s)", mapped, count != 0U ? args[0] : "0");
    } else if (strcmp(name, "isnan") == 0 || strcmp(name, "la_isnan") == 0) {
        f2c_buffer_printf(&result, "isnan(%s)", count != 0U ? args[0] : "0");
    } else if (strcmp(name, "cabs1") == 0 || strcmp(name, "abs1") == 0) {
        const char *value = count != 0U ? args[0] : "0";
        const int single = count != 0U && argument_types[0] == TYPE_COMPLEX;
        f2c_buffer_printf(&result, "(F2C_ABS(%s(%s)) + F2C_ABS(%s(%s)))",
                          single ? "crealf" : "creal", value, single ? "cimagf" : "cimag", value);
    } else if (strcmp(name, "cabs2") == 0) {
        const char *value = count != 0U ? args[0] : "0";
        const int single = count != 0U && argument_types[0] == TYPE_COMPLEX;
        f2c_buffer_printf(&result, "(F2C_ABS(%s(%s) / %s) + F2C_ABS(%s(%s) / %s))",
                          single ? "crealf" : "creal", value, single ? "2.0f" : "2.0",
                          single ? "cimagf" : "cimag", value, single ? "2.0f" : "2.0");
    } else if (strcmp(name, "abssq") == 0) {
        const char *value = count != 0U ? args[0] : "0";
        const int single = count != 0U && argument_types[0] == TYPE_COMPLEX;
        f2c_buffer_printf(&result, "((%s(%s) * %s(%s)) + (%s(%s) * %s(%s)))",
                          single ? "crealf" : "creal", value, single ? "crealf" : "creal", value,
                          single ? "cimagf" : "cimag", value, single ? "cimagf" : "cimag", value);
    } else if (strcmp(name, "omp_get_thread_num") == 0) {
        f2c_buffer_append(&result, "0");
    } else if (strcmp(name, "omp_get_num_threads") == 0) {
        f2c_buffer_append(&result, "1");
    } else if (strcmp(name, "iand") == 0) {
        f2c_buffer_printf(&result, "((int32_t)(%s) & (int32_t)(%s))", count >= 1U ? args[0] : "0",
                          count >= 2U ? args[1] : "0");
    } else if (strcmp(name, "transfer") == 0) {
        f2c_buffer_printf(&result, "F2C_TRANSFER(%s, %s)", count >= 1U ? args[0] : "0",
                          count >= 2U ? args[1] : "0");
    } else if (strcmp(name, "maxloc") == 0 || strcmp(name, "maxval") == 0) {
        f2c_buffer_printf(&result, "%s(%s)",
                          strcmp(name, "maxloc") == 0 ? "F2C_MAXLOC" : "F2C_MAXVAL",
                          count >= 1U ? args[0] : "NULL, 0");
    } else {
        f2c_buffer_printf(&result, "%s(", mapped);
        for (i = 0U; i < count; ++i) {
            f2c_buffer_printf(&result, "%s%s", i == 0U ? "" : ", ", args[i]);
        }
        f2c_buffer_append(&result, ")");
    }
    return f2c_buffer_take(&result);
}

char *f2c_symbol_dimension_lower(Unit *unit, const Symbol *symbol, size_t dimension) {
    Buffer result = {0};
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (f2c_symbol_uses_descriptor(symbol)) {
        f2c_buffer_printf(&result, "%s_lower_%zu", f2c_symbol_c_name(unit, symbol), dimension + 1U);
        return f2c_buffer_take(&result);
    }
    return f2c_emit_typed_expression(unit, symbol->dimensions[dimension].lower_expression);
}

char *f2c_symbol_dimension_upper(Unit *unit, const Symbol *symbol, size_t dimension) {
    Buffer result = {0};
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (f2c_symbol_uses_descriptor(symbol)) {
        f2c_buffer_printf(&result, "(%s_lower_%zu + %s_extent_%zu - 1)",
                          f2c_symbol_c_name(unit, symbol), dimension + 1U,
                          f2c_symbol_c_name(unit, symbol), dimension + 1U);
        return f2c_buffer_take(&result);
    }
    return f2c_emit_typed_expression(unit, symbol->dimensions[dimension].upper_expression);
}

char *f2c_symbol_dimension_extent(Unit *unit, const Symbol *symbol, size_t dimension) {
    Buffer result = {0};
    char *lower;
    char *upper;
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (f2c_symbol_uses_descriptor(symbol)) {
        f2c_buffer_printf(&result, "%s_extent_%zu", f2c_symbol_c_name(unit, symbol),
                          dimension + 1U);
        return f2c_buffer_take(&result);
    }
    lower = f2c_symbol_dimension_lower(unit, symbol, dimension);
    upper = f2c_symbol_dimension_upper(unit, symbol, dimension);
    if (lower == NULL || upper == NULL) {
        free(lower);
        free(upper);
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s) >= (%s) ? (size_t)((%s) - (%s) + 1) : 0U)", upper, lower,
                      upper, lower);
    free(lower);
    free(upper);
    return f2c_buffer_take(&result);
}

char *f2c_emit_array_reference(Unit *unit, Symbol *symbol, char **indices, size_t count) {
    Buffer result = {0};
    char *character_length = NULL;
    size_t i;
    f2c_buffer_printf(&result, "%s[", f2c_symbol_c_name(unit, symbol));
    if (symbol->type == TYPE_CHARACTER) {
        character_length = f2c_symbol_character_length(unit, symbol);
        if (character_length == NULL)
            character_length = f2c_strdup("1U");
        f2c_buffer_printf(&result, "(size_t)(%s) * (size_t)(", character_length);
    }
    if (symbol->argument && f2c_symbol_uses_descriptor(symbol)) {
        f2c_buffer_printf(&result, "f2c_array_descriptor_offset(%zuU, (const int64_t[]){", count);
        for (i = 0U; i < count; ++i)
            f2c_buffer_printf(&result, "%s(int64_t)(%s)", i == 0U ? "" : ", ", indices[i]);
        f2c_buffer_append(&result, "}, (const int64_t[]){");
        for (i = 0U; i < count; ++i)
            f2c_buffer_printf(&result, "%s(int64_t)%s_lower_%zu", i == 0U ? "" : ", ",
                              f2c_symbol_c_name(unit, symbol), i + 1U);
        f2c_buffer_append(&result, "}, (const size_t[]){");
        for (i = 0U; i < count; ++i)
            f2c_buffer_printf(&result, "%s(size_t)%s_extent_%zu", i == 0U ? "" : ", ",
                              f2c_symbol_c_name(unit, symbol), i + 1U);
        f2c_buffer_append(&result, "}, (const ptrdiff_t[]){");
        for (i = 0U; i < count; ++i)
            f2c_buffer_printf(&result, "%s%s_stride_%zu", i == 0U ? "" : ", ",
                              f2c_symbol_c_name(unit, symbol), i + 1U);
        f2c_buffer_append(&result, "})");
        if (character_length != NULL)
            f2c_buffer_append(&result, ")");
        f2c_buffer_append(&result, "]");
        free(character_length);
        return f2c_buffer_take(&result);
    }
    for (i = 0U; i < count; ++i) {
        char *lower =
            i < symbol->rank ? f2c_symbol_dimension_lower(unit, symbol, i) : f2c_strdup("1");
        char *extent = i < symbol->rank ? f2c_symbol_dimension_extent(unit, symbol, i) : NULL;
        const int checked =
            !symbol->argument && !symbol->allocatable && !symbol->pointer && extent != NULL;
        if (i != 0U) {
            size_t j;
            f2c_buffer_append(&result, " + (");
            for (j = 0U; j < i; ++j) {
                if (f2c_symbol_uses_descriptor(symbol)) {
                    f2c_buffer_printf(&result, "%s%s_extent_%zu", j == 0U ? "" : " * ",
                                      f2c_symbol_c_name(unit, symbol), j + 1U);
                    continue;
                }
                char *lo_c;
                char *hi_c;
                lo_c = f2c_symbol_dimension_lower(unit, symbol, j);
                hi_c = f2c_symbol_dimension_upper(unit, symbol, j);
                f2c_buffer_printf(&result, "%s((%s) - (%s) + 1)", j == 0U ? "" : " * ", hi_c, lo_c);
                free(lo_c);
                free(hi_c);
            }
            f2c_buffer_append(&result, ") * ");
        }
        if (checked) {
            f2c_buffer_printf(&result,
                              "f2c_array_offset((int64_t)((int32_t)(%s)), "
                              "(int64_t)(%s), (size_t)(%s))",
                              indices[i], lower, extent);
        } else {
            f2c_buffer_printf(&result, "(((int32_t)(%s)) - (%s))", indices[i], lower);
        }
        free(extent);
        free(lower);
    }
    if (character_length != NULL)
        f2c_buffer_append(&result, ")");
    f2c_buffer_append(&result, "]");
    free(character_length);
    return f2c_buffer_take(&result);
}

char *f2c_find_assignment(char *line) {
    F2cTokenStream lexer;
    int parenthesis_depth = 0;
    int bracket_depth = 0;
    f2c_token_stream_init(&lexer, line, 1U, 1U);
    for (;;) {
        f2c_token_stream_next(&lexer);
        if (lexer.token.kind == F2C_TOKEN_END)
            return NULL;
        if (lexer.token.kind == F2C_TOKEN_LEFT_PAREN)
            ++parenthesis_depth;
        else if (lexer.token.kind == F2C_TOKEN_RIGHT_PAREN && parenthesis_depth > 0)
            --parenthesis_depth;
        else if (lexer.token.kind == F2C_TOKEN_LEFT_BRACKET ||
                 lexer.token.kind == F2C_TOKEN_ARRAY_BEGIN)
            ++bracket_depth;
        else if ((lexer.token.kind == F2C_TOKEN_RIGHT_BRACKET ||
                  lexer.token.kind == F2C_TOKEN_ARRAY_END) &&
                 bracket_depth > 0)
            --bracket_depth;
        else if (lexer.token.kind == F2C_TOKEN_OPERATOR && parenthesis_depth == 0 &&
                 bracket_depth == 0 && f2c_token_equals(&lexer.token, "="))
            return (char *)lexer.token.begin;
    }
}
