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

char *f2c_emit_intrinsic(const char *name, F2cIntrinsicId intrinsic, char **args,
                         const Type *argument_types, size_t count, Type result_type) {
    Buffer result = {0};
    const F2cIntrinsicDescriptor *descriptor = f2c_intrinsic_descriptor(intrinsic);
    const char *callee = descriptor != NULL ? descriptor->canonical_name : name;
    size_t argument;
    (void)result_type;
    if (intrinsic == F2C_INTRINSIC_ISNAN) {
        f2c_buffer_printf(&result, "isnan(%s)", count != 0U ? args[0] : "0");
    } else if (intrinsic == F2C_INTRINSIC_TRANSFER) {
        f2c_buffer_printf(&result, "F2C_TRANSFER(%s, %s)", count >= 1U ? args[0] : "0",
                          count >= 2U ? args[1] : "0");
    } else if (intrinsic == F2C_INTRINSIC_NULL) {
        f2c_buffer_append(&result, "NULL");
    } else if (intrinsic != F2C_INTRINSIC_NONE) {
        f2c_buffer_printf(&result, "%s(", callee != NULL ? callee : "");
        for (argument = 0U; argument < count; ++argument)
            f2c_buffer_printf(&result, "%s%s", argument == 0U ? "" : ", ", args[argument]);
        f2c_buffer_append(&result, ")");
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
    } else {
        f2c_buffer_printf(&result, "%s(", callee != NULL ? callee : "");
        for (argument = 0U; argument < count; ++argument)
            f2c_buffer_printf(&result, "%s%s", argument == 0U ? "" : ", ", args[argument]);
        f2c_buffer_append(&result, ")");
    }
    return f2c_buffer_take(&result);
}

char *f2c_symbol_dimension_lower(Unit *unit, const Symbol *symbol, size_t dimension) {
    Buffer result = {0};
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (f2c_symbol_is_automatic_array(unit, symbol)) {
        f2c_buffer_printf(&result, "f2c_auto_lower_%s_%zu", f2c_symbol_c_name(unit, symbol),
                          dimension + 1U);
        return f2c_buffer_take(&result);
    }
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
    if (f2c_symbol_is_automatic_array(unit, symbol)) {
        f2c_buffer_printf(&result,
                          "(f2c_auto_lower_%s_%zu + (int64_t)f2c_auto_extent_%s_%zu - "
                          "INT64_C(1))",
                          f2c_symbol_c_name(unit, symbol), dimension + 1U,
                          f2c_symbol_c_name(unit, symbol), dimension + 1U);
        return f2c_buffer_take(&result);
    }
    if (f2c_symbol_uses_descriptor(symbol)) {
        f2c_buffer_printf(&result, "(%s_lower_%zu + %s_extent_%zu - 1)",
                          f2c_symbol_c_name(unit, symbol), dimension + 1U,
                          f2c_symbol_c_name(unit, symbol), dimension + 1U);
        return f2c_buffer_take(&result);
    }
    return f2c_emit_typed_expression(unit, symbol->dimensions[dimension].upper_expression);
}

static int dimension_lower_constant(Unit *unit, const Symbol *symbol, size_t dimension,
                                    int64_t *value) {
    const F2cExpr *expression;
    if (symbol == NULL || dimension >= symbol->rank || value == NULL ||
        f2c_symbol_uses_descriptor(symbol))
        return 0;
    expression = symbol->dimensions[dimension].lower_expression;
    return expression != NULL && f2c_evaluate_integer_constant(unit, expression, value);
}

char *f2c_symbol_dimension_extent(Unit *unit, const Symbol *symbol, size_t dimension) {
    Buffer result = {0};
    char *lower;
    char *upper;
    int64_t lower_value;
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (f2c_symbol_is_automatic_array(unit, symbol)) {
        f2c_buffer_printf(&result, "f2c_auto_extent_%s_%zu", f2c_symbol_c_name(unit, symbol),
                          dimension + 1U);
        return f2c_buffer_take(&result);
    }
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
    if (dimension_lower_constant(unit, symbol, dimension, &lower_value) && lower_value == 1) {
        f2c_buffer_printf(&result, "((%s) >= 1 ? (size_t)(%s) : 0U)", upper, upper);
    } else {
        f2c_buffer_printf(&result, "((%s) >= (%s) ? (size_t)((%s) - (%s) + 1) : 0U)", upper, lower,
                          upper, lower);
    }
    free(lower);
    free(upper);
    return f2c_buffer_take(&result);
}

static char *emit_contiguous_array_offset(Unit *unit, Symbol *symbol, char **indices,
                                          size_t count) {
    Buffer result = {0};
    const int checked_array = !symbol->argument && !symbol->allocatable && !symbol->pointer;
    size_t i;
    for (i = 0U; i < count; ++i) {
        char *lower =
            i < symbol->rank ? f2c_symbol_dimension_lower(unit, symbol, i) : f2c_strdup("1");
        char *extent = i < symbol->rank ? f2c_symbol_dimension_extent(unit, symbol, i) : NULL;
        const int checked = checked_array && extent != NULL;
        if (i != 0U) {
            size_t j;
            f2c_buffer_append(&result, " + (");
            for (j = 0U; j < i; ++j) {
                int64_t lower_value;
                if (f2c_symbol_uses_descriptor(symbol)) {
                    f2c_buffer_printf(&result, "%s%s_extent_%zu", j == 0U ? "" : " * ",
                                      f2c_symbol_c_name(unit, symbol), j + 1U);
                    continue;
                }
                char *lo_c;
                char *hi_c;
                lo_c = f2c_symbol_dimension_lower(unit, symbol, j);
                hi_c = f2c_symbol_dimension_upper(unit, symbol, j);
                if (dimension_lower_constant(unit, symbol, j, &lower_value) && lower_value == 1) {
                    f2c_buffer_printf(&result, "%s(%s)(%s)", j == 0U ? "" : " * ",
                                      checked_array ? "size_t" : "ptrdiff_t", hi_c);
                } else {
                    f2c_buffer_printf(&result, "%s(%s)((%s) - (%s) + 1)", j == 0U ? "" : " * ",
                                      checked_array ? "size_t" : "ptrdiff_t", hi_c, lo_c);
                }
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
            int64_t lower_value;
            const int lower_known = dimension_lower_constant(unit, symbol, i, &lower_value);
            if (lower_known && lower_value == 0) {
                f2c_buffer_printf(&result, "((ptrdiff_t)(%s))", indices[i]);
            } else if (lower_known && lower_value == 1) {
                f2c_buffer_printf(&result, "((ptrdiff_t)(%s) - 1)", indices[i]);
            } else {
                f2c_buffer_printf(&result, "((ptrdiff_t)(%s) - (ptrdiff_t)(%s))", indices[i],
                                  lower);
            }
        }
        free(extent);
        free(lower);
    }
    return f2c_buffer_take(&result);
}

const char *f2c_unaligned_access_suffix(const Symbol *symbol) {
    const int kind = symbol != NULL && symbol->kind > 0
                         ? symbol->kind
                         : f2c_default_kind(symbol != NULL ? symbol->type : TYPE_UNKNOWN);
    if (symbol == NULL)
        return NULL;
    switch (symbol->type) {
    case TYPE_LOGICAL:
        /* Non-default LOGICAL storage has the corresponding integer representation. */
        /* fall through */
    case TYPE_INTEGER:
        if (kind == 1)
            return "i8";
        if (kind == 2)
            return "i16";
        if (kind == 4)
            return "i32";
        if (kind == 8)
            return "i64";
        return NULL;
    case TYPE_REAL:
    case TYPE_DOUBLE:
        if (kind == 4)
            return "r4";
        if (kind == 8)
            return "r8";
        if (kind == 16)
            return "r16";
        return NULL;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        if (kind == 4)
            return "c4";
        if (kind == 8)
            return "c8";
        if (kind == 16)
            return "c16";
        return NULL;
    case TYPE_CHARACTER:
    case TYPE_DERIVED:
    case TYPE_UNKNOWN:
    default:
        return NULL;
    }
}

char *f2c_emit_unaligned_linear_address(Unit *unit, Symbol *symbol, const char *offset) {
    Buffer result = {0};
    if (unit == NULL || symbol == NULL || !symbol->equivalence_unaligned ||
        f2c_unaligned_access_suffix(symbol) == NULL)
        return NULL;
    f2c_buffer_printf(&result, "((unsigned char *)(%s)", f2c_symbol_c_name(unit, symbol));
    if (offset != NULL)
        f2c_buffer_printf(&result, " + sizeof(%s) * (size_t)(%s)", f2c_symbol_c_type(symbol),
                          offset);
    f2c_buffer_append(&result, ")");
    return f2c_buffer_take(&result);
}

char *f2c_emit_unaligned_linear_load(Unit *unit, Symbol *symbol, const char *offset) {
    Buffer result = {0};
    const char *suffix = f2c_unaligned_access_suffix(symbol);
    char *address = f2c_emit_unaligned_linear_address(unit, symbol, offset);
    if (suffix == NULL || address == NULL) {
        free(address);
        return NULL;
    }
    f2c_buffer_printf(&result, "f2c_unaligned_load_%s(%s)", suffix, address);
    free(address);
    return f2c_buffer_take(&result);
}

char *f2c_emit_unaligned_address(Unit *unit, Symbol *symbol, char **indices, size_t count) {
    char *offset = NULL;
    char *result;
    if (count != 0U) {
        offset = emit_contiguous_array_offset(unit, symbol, indices, count);
        if (offset == NULL)
            return NULL;
    }
    result = f2c_emit_unaligned_linear_address(unit, symbol, offset);
    free(offset);
    return result;
}

char *f2c_emit_unaligned_load(Unit *unit, Symbol *symbol, char **indices, size_t count) {
    Buffer result = {0};
    const char *suffix = f2c_unaligned_access_suffix(symbol);
    char *address = f2c_emit_unaligned_address(unit, symbol, indices, count);
    if (suffix == NULL || address == NULL) {
        free(address);
        return NULL;
    }
    f2c_buffer_printf(&result, "f2c_unaligned_load_%s(%s)", suffix, address);
    free(address);
    return f2c_buffer_take(&result);
}

char *f2c_emit_array_reference(Unit *unit, Symbol *symbol, char **indices, size_t count) {
    Buffer result = {0};
    char *character_length = NULL;
    char *offset;
    size_t i;
    if (symbol->equivalence_unaligned)
        return f2c_emit_unaligned_load(unit, symbol, indices, count);
    f2c_buffer_printf(&result, "%s[", f2c_symbol_c_name(unit, symbol));
    if (symbol->type == TYPE_CHARACTER) {
        character_length = f2c_symbol_character_length(unit, symbol);
        if (character_length == NULL)
            character_length = f2c_strdup("1U");
        f2c_buffer_printf(&result, "(size_t)(%s) * (size_t)(", character_length);
    }
    if (symbol->pointer || (symbol->argument && f2c_symbol_uses_descriptor(symbol))) {
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
    offset = emit_contiguous_array_offset(unit, symbol, indices, count);
    if (offset == NULL) {
        free(character_length);
        free(result.data);
        return NULL;
    }
    f2c_buffer_append(&result, offset);
    if (character_length != NULL)
        f2c_buffer_append(&result, ")");
    f2c_buffer_append(&result, "]");
    free(offset);
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
