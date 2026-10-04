#include "codegen/storage/private.h"
#include "internal/f2c.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *f2c_emit_numeric_conversion_as(const char *operand, Type actual, Type target,
                                     const char *target_c_type) {
    Buffer converted = {0};
    if (operand == NULL || target_c_type == NULL)
        return NULL;
    if (actual == TYPE_LOGICAL && target == TYPE_LOGICAL) {
        f2c_buffer_printf(&converted, "((%s)(%s))", target_c_type, operand);
        return f2c_buffer_take(&converted);
    }
    if (!f2c_type_is_numeric(actual) || !f2c_type_is_numeric(target))
        return f2c_strdup(operand);
    if (target == TYPE_COMPLEX || target == TYPE_DOUBLE_COMPLEX) {
        const int double_precision = target == TYPE_DOUBLE_COMPLEX;
        if (actual == target)
            return f2c_strdup(operand);
        if (actual == TYPE_COMPLEX || actual == TYPE_DOUBLE_COMPLEX) {
            f2c_buffer_printf(&converted, "%s(%s)", double_precision ? "f2c_c_to_z" : "f2c_z_to_c",
                              operand);
        } else {
            f2c_buffer_printf(
                &converted, "%s((%s)(%s), %s)", double_precision ? "f2c_make_z" : "f2c_make_c",
                double_precision ? "double" : "float", operand, double_precision ? "0.0" : "0.0f");
        }
    } else if (actual == TYPE_COMPLEX || actual == TYPE_DOUBLE_COMPLEX) {
        f2c_buffer_printf(&converted, "((%s)%s(%s))", target_c_type,
                          actual == TYPE_COMPLEX ? "crealf" : "creal", operand);
    } else {
        f2c_buffer_printf(&converted, "((%s)(%s))", target_c_type, operand);
    }
    return f2c_buffer_take(&converted);
}

char *f2c_emit_numeric_conversion(const char *operand, Type actual, Type target) {
    return actual == target
               ? f2c_strdup(operand)
               : f2c_emit_numeric_conversion_as(operand, actual, target, f2c_c_type(target));
}

char *f2c_emit_scalar_temporary_address(const char *c_type, Type type, const char *value) {
    Buffer result = {0};
    if (type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX)
        f2c_buffer_printf(&result, "&((%s[]){%s})[0]", c_type, value);
    else
        f2c_buffer_printf(&result, "&(%s){%s}", c_type, value);
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
        return f2c_storage_symbol_property(unit, symbol, F2C_OBJECT_LOWER, dimension);
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
        char *lower = f2c_symbol_dimension_lower(unit, symbol, dimension);
        char *extent = f2c_symbol_dimension_extent(unit, symbol, dimension);
        if (lower == NULL || extent == NULL) {
            free(lower);
            free(extent);
            return NULL;
        }
        f2c_buffer_printf(&result, "((int64_t)(%s) + (int64_t)(%s) - INT64_C(1))", lower, extent);
        free(lower);
        free(extent);
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
        return f2c_storage_symbol_property(unit, symbol, F2C_OBJECT_EXTENT, dimension);
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
                    char *prior_extent = f2c_symbol_dimension_extent(unit, symbol, j);
                    if (prior_extent == NULL) {
                        free(lower);
                        free(extent);
                        free(result.data);
                        return NULL;
                    }
                    f2c_buffer_printf(&result, "%s(%s)", j == 0U ? "" : " * ", prior_extent);
                    free(prior_extent);
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
            return symbol->volatile_entity ? "i8_volatile" : "i8";
        if (kind == 2)
            return symbol->volatile_entity ? "i16_volatile" : "i16";
        if (kind == 4)
            return symbol->volatile_entity ? "i32_volatile" : "i32";
        if (kind == 8)
            return symbol->volatile_entity ? "i64_volatile" : "i64";
        return NULL;
    case TYPE_REAL:
    case TYPE_DOUBLE:
        if (kind == 4)
            return symbol->volatile_entity ? "r4_volatile" : "r4";
        if (kind == 8)
            return symbol->volatile_entity ? "r8_volatile" : "r8";
        if (kind == 16)
            return symbol->volatile_entity ? "r16_volatile" : "r16";
        return NULL;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        if (kind == 4)
            return symbol->volatile_entity ? "c4_volatile" : "c4";
        if (kind == 8)
            return symbol->volatile_entity ? "c8_volatile" : "c8";
        if (kind == 16)
            return symbol->volatile_entity ? "c16_volatile" : "c16";
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

static char *emit_array_reference(Unit *unit, Symbol *symbol, char **indices, size_t count,
                                  int physical_storage) {
    Buffer result = {0};
    char *character_length = NULL;
    char *offset;
    size_t i;
    if (symbol->equivalence_unaligned)
        return f2c_emit_unaligned_load(unit, symbol, indices, count);
    {
        char *data = f2c_storage_symbol_data(unit, symbol);
        if (data == NULL)
            return NULL;
        if (!physical_storage && symbol->volatile_entity && symbol->type != TYPE_CHARACTER)
            f2c_buffer_printf(&result, "((volatile %s *)%s)[", f2c_symbol_c_type(symbol), data);
        else
            f2c_buffer_printf(&result, "%s[", data);
        free(data);
    }
    if (symbol->type == TYPE_CHARACTER) {
        character_length = f2c_symbol_character_length(unit, symbol);
        if (character_length == NULL)
            character_length = f2c_strdup("1U");
        f2c_buffer_append(&result, "f2c_descriptor_stride_extent((ptrdiff_t)(");
    }
    if (symbol->pointer || (symbol->argument && f2c_symbol_uses_descriptor(symbol))) {
        f2c_buffer_printf(&result, "f2c_array_descriptor_offset(%zuU, (const int64_t[]){", count);
        for (i = 0U; i < count; ++i)
            f2c_buffer_printf(&result, "%s(int64_t)(%s)", i == 0U ? "" : ", ", indices[i]);
        f2c_buffer_append(&result, "}, (const int64_t[]){");
        for (i = 0U; i < count; ++i) {
            char *lower = f2c_symbol_dimension_lower(unit, symbol, i);
            if (lower == NULL) {
                free(f2c_buffer_take(&result));
                free(character_length);
                return NULL;
            }
            f2c_buffer_printf(&result, "%s(int64_t)(%s)", i == 0U ? "" : ", ", lower);
            free(lower);
        }
        f2c_buffer_append(&result, "}, (const size_t[]){");
        for (i = 0U; i < count; ++i) {
            char *extent = f2c_symbol_dimension_extent(unit, symbol, i);
            if (extent == NULL) {
                free(f2c_buffer_take(&result));
                free(character_length);
                return NULL;
            }
            f2c_buffer_printf(&result, "%s(size_t)(%s)", i == 0U ? "" : ", ", extent);
            free(extent);
        }
        f2c_buffer_append(&result, "}, (const ptrdiff_t[]){");
        for (i = 0U; i < count; ++i) {
            char *stride = f2c_storage_symbol_property(unit, symbol, F2C_OBJECT_STRIDE, i);
            if (stride == NULL) {
                free(f2c_buffer_take(&result));
                free(character_length);
                return NULL;
            }
            f2c_buffer_printf(&result, "%s(%s)", i == 0U ? "" : ", ", stride);
            free(stride);
        }
        f2c_buffer_append(&result, "})");
        if (character_length != NULL)
            f2c_buffer_printf(&result, "), (size_t)(%s))", character_length);
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
        f2c_buffer_printf(&result, "), (size_t)(%s))", character_length);
    f2c_buffer_append(&result, "]");
    free(offset);
    free(character_length);
    return f2c_buffer_take(&result);
}

char *f2c_emit_array_reference(Unit *unit, Symbol *symbol, char **indices, size_t count) {
    return emit_array_reference(unit, symbol, indices, count, 0);
}

char *f2c_emit_array_storage_reference(Unit *unit, Symbol *symbol, char **indices, size_t count) {
    return emit_array_reference(unit, symbol, indices, count, 1);
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
