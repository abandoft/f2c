#include "codegen/expression/private.h"

#include "codegen/array/private.h"
#include "codegen/descriptor/private.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_emit_array_reference(Unit *unit, const F2cExpr *expression, int *supported);

void f2c_expression_append_component(Buffer *output, const char *base,
                                     const F2cDerivedType *dynamic_type, const Symbol *component) {
    const F2cDerivedType *owner = dynamic_type;
    f2c_buffer_printf(output, "(%s)", base);
    while (owner != NULL && owner != component->derived_owner) {
        f2c_buffer_append(output, ".parent");
        owner = owner->parent;
    }
    f2c_buffer_printf(output, ".%s",
                      component->c_name != NULL ? component->c_name : component->name);
}

static const F2cExpr *intrinsic_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static const F2cExpr *call_argument(const F2cExpr *call, const char *keyword, size_t position) {
    size_t positional = 0U;
    size_t argument;
    for (argument = 0U; call != NULL && argument < call->child_count; ++argument) {
        const F2cExpr *actual = call->children[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return intrinsic_argument_value(actual);
        } else if (positional++ == position) {
            return actual;
        }
    }
    return NULL;
}

int f2c_expression_array_view(Unit *unit, const F2cExpr *array, char **pointer, char **count,
                              char **stride, int *supported) {
    Buffer extent = {0};
    Buffer storage_stride = {0};
    const F2cExpr *selector;
    char *reference;
    char *lower;
    char *upper;
    char *step;
    size_t dimension;
    if (array == NULL || array->rank == 0U)
        return 0;
    if (array->kind == F2C_EXPR_NAME && array->symbol != NULL) {
        *pointer = f2c_strdup(f2c_symbol_c_name(unit, array->symbol));
        *count = f2c_symbol_element_count(unit, array->symbol);
        *stride = f2c_strdup("1");
        return *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if (array->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        char *constructor = f2c_expression_emit(unit, array, supported);
        Buffer grouped = {0};
        if (constructor != NULL)
            f2c_buffer_printf(&grouped, "(%s)", constructor);
        free(constructor);
        *pointer = f2c_buffer_take(&grouped);
        f2c_buffer_printf(&extent, "%zuU", array->child_count);
        *count = f2c_buffer_take(&extent);
        *stride = f2c_strdup("1");
        return *supported && *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if (array->kind == F2C_EXPR_CALL && array->text != NULL &&
        strcmp(array->text, "reshape") == 0) {
        const F2cExpr *source = call_argument(array, "source", 0U);
        const F2cExpr *pad = call_argument(array, "pad", 2U);
        const F2cExpr *order = call_argument(array, "order", 3U);
        char *source_count = NULL;
        Buffer result_count = {0};
        size_t index;
        if (source == NULL || pad != NULL || order != NULL || array->rank == 0U ||
            !f2c_expression_array_view(unit, source, pointer, &source_count, stride, supported))
            return 0;
        for (index = 0U; index < array->rank; ++index) {
            char *dimension_extent = f2c_array_expression_extent(unit, array, index);
            if (dimension_extent == NULL) {
                free(*pointer);
                free(source_count);
                free(*stride);
                *pointer = NULL;
                *stride = NULL;
                return 0;
            }
            f2c_buffer_printf(&result_count, "%s(size_t)(%s)", index == 0U ? "" : " * ",
                              dimension_extent);
            free(dimension_extent);
        }
        f2c_buffer_printf(&extent, "((%s) <= (size_t)(%s) ? (%s) : (abort(), 0U))",
                          result_count.data, source_count, result_count.data);
        free(result_count.data);
        free(source_count);
        *count = f2c_buffer_take(&extent);
        return *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if (array->kind != F2C_EXPR_ARRAY_REFERENCE || array->symbol == NULL || array->rank != 1U)
        return 0;
    selector = NULL;
    dimension = 0U;
    for (size_t i = 0U; i < array->child_count; ++i) {
        if (array->children[i]->kind == F2C_EXPR_ARRAY_SECTION) {
            if (selector != NULL)
                return 0;
            selector = array->children[i];
            dimension = i;
        } else if (array->children[i]->rank != 0U) {
            return 0;
        }
    }
    if (selector == NULL || selector->child_count != 3U)
        return 0;
    lower = selector->children[0]->kind == F2C_EXPR_INVALID
                ? f2c_symbol_dimension_lower(unit, array->symbol, dimension)
                : f2c_expression_emit(unit, selector->children[0], supported);
    upper = selector->children[1]->kind == F2C_EXPR_INVALID
                ? f2c_symbol_dimension_upper(unit, array->symbol, dimension)
                : f2c_expression_emit(unit, selector->children[1], supported);
    step = selector->children[2]->kind == F2C_EXPR_INVALID
               ? f2c_strdup("1")
               : f2c_expression_emit(unit, selector->children[2], supported);
    reference = f2c_expression_emit_array_reference(unit, array, supported);
    if (!*supported || lower == NULL || upper == NULL || step == NULL || reference == NULL) {
        free(lower);
        free(upper);
        free(step);
        free(reference);
        return 0;
    }
    *pointer = NULL;
    {
        Buffer address = {0};
        f2c_buffer_printf(&address, "(&%s)", reference);
        *pointer = f2c_buffer_take(&address);
    }
    f2c_buffer_printf(&extent,
                      "((%s) > 0 ? ((%s) >= (%s) ? (size_t)(((%s) - (%s)) / (%s) + 1) "
                      ": 0U) : ((%s) < 0 ? ((%s) <= (%s) ? "
                      "(size_t)(((%s) - (%s)) / (-(%s)) + 1) : 0U) : (abort(), 0U)))",
                      step, upper, lower, upper, lower, step, step, upper, lower, lower, upper,
                      step);
    *count = f2c_buffer_take(&extent);
    for (size_t i = 0U; i < dimension; ++i) {
        char *dimension_extent = f2c_symbol_dimension_extent(unit, array->symbol, i);
        if (dimension_extent == NULL) {
            free(reference);
            free(lower);
            free(upper);
            free(step);
            return 0;
        }
        f2c_buffer_printf(&storage_stride, "%s(ptrdiff_t)(%s)", i == 0U ? "" : " * ",
                          dimension_extent);
        free(dimension_extent);
    }
    if (dimension != 0U)
        f2c_buffer_append(&storage_stride, " * ");
    f2c_buffer_printf(&storage_stride, "(ptrdiff_t)(%s)", step);
    *stride = f2c_buffer_take(&storage_stride);
    free(reference);
    free(lower);
    free(upper);
    free(step);
    return *pointer != NULL && *count != NULL && *stride != NULL;
}

char *f2c_expression_real_literal(const F2cExpr *expression) {
    char *result = f2c_strdup(expression->text != NULL ? expression->text : "0");
    char *kind;
    char *cursor;
    Buffer typed = {0};
    if (result == NULL)
        return NULL;
    kind = strchr(result, '_');
    if (kind != NULL)
        *kind = '\0';
    for (cursor = result; *cursor != '\0'; ++cursor) {
        if (*cursor == 'd' || *cursor == 'D')
            *cursor = 'e';
    }
    if (expression->type_kind == 4) {
        const size_t length = strlen(result);
        if (length == 0U || (result[length - 1U] != 'f' && result[length - 1U] != 'F')) {
            f2c_buffer_printf(&typed, "%sf", result);
            free(result);
            result = f2c_buffer_take(&typed);
        }
    }
    return result;
}

char *f2c_expression_integer_literal(const F2cExpr *expression) {
    const char *text = expression->text != NULL ? expression->text : "0";
    const char *suffix = strchr(text, '_');
    char *digits = suffix != NULL ? f2c_strdup_n(text, (size_t)(suffix - text)) : f2c_strdup(text);
    Buffer result = {0};
    if (digits == NULL)
        return NULL;
    if (expression->type_kind != 0 && expression->type_kind != 4) {
        f2c_buffer_printf(&result, "((%s)(%s))", f2c_expression_c_type(expression), digits);
        free(digits);
        return f2c_buffer_take(&result);
    }
    return digits;
}

char *f2c_expression_string_literal(const char *text) {
    Buffer result = {0};
    const char *quote_begin = f2c_character_literal_quote(text);
    char quote = quote_begin != NULL ? *quote_begin : '\'';
    size_t length = quote_begin != NULL ? strlen(quote_begin) : 0U;
    const char *hollerith = text;
    unsigned long long hollerith_length = 0ULL;
    size_t i;
    f2c_buffer_append(&result, "\"");
    while (hollerith != NULL && isdigit((unsigned char)*hollerith)) {
        hollerith_length = hollerith_length * 10ULL + (unsigned long long)(*hollerith - '0');
        ++hollerith;
    }
    if (hollerith != NULL && (*hollerith == 'h' || *hollerith == 'H') &&
        hollerith_length <= (unsigned long long)SIZE_MAX &&
        (size_t)hollerith_length <= strlen(hollerith + 1)) {
        const unsigned char *payload = (const unsigned char *)(hollerith + 1);
        for (i = 0U; i < (size_t)hollerith_length; ++i) {
            const unsigned char value = payload[i];
            if (value == '\\' || value == '"') {
                const char escaped = (char)value;
                f2c_buffer_append(&result, "\\");
                f2c_buffer_append_n(&result, &escaped, 1U);
            } else if (value >= 32U && value <= 126U) {
                const char printable = (char)value;
                f2c_buffer_append_n(&result, &printable, 1U);
            } else {
                f2c_buffer_printf(&result, "\\%03o", (unsigned int)value);
            }
        }
        f2c_buffer_append(&result, "\"");
        return f2c_buffer_take(&result);
    }
    for (i = 1U; i + 1U < length; ++i) {
        char c = quote_begin[i];
        if (c == quote && i + 1U < length - 1U && quote_begin[i + 1U] == quote)
            ++i;
        if (c == '\\' || c == '"')
            f2c_buffer_append(&result, "\\");
        f2c_buffer_append_n(&result, &c, 1U);
    }
    f2c_buffer_append(&result, "\"");
    return f2c_buffer_take(&result);
}

char *f2c_expression_boz_literal(const char *text) {
    const char prefix = text != NULL ? (char)tolower((unsigned char)text[0]) : '\0';
    const int base = prefix == 'b' ? 2 : (prefix == 'o' ? 8 : 16);
    const char *digits = text != NULL ? text + 2 : "";
    char *copy;
    char *end = NULL;
    unsigned long long value;
    Buffer result = {0};
    size_t length = text != NULL ? strlen(text) : 0U;
    if (length < 3U || (prefix != 'b' && prefix != 'o' && prefix != 'z' && prefix != 'x'))
        return NULL;
    copy = f2c_strdup_n(digits, length - 3U);
    if (copy == NULL)
        return NULL;
    value = strtoull(copy, &end, base);
    if (end == copy || *end != '\0' || value > (unsigned long long)UINT32_MAX) {
        free(copy);
        return NULL;
    }
    f2c_buffer_printf(&result, "((int32_t)UINT32_C(0x%08X))", (unsigned int)value);
    free(copy);
    return f2c_buffer_take(&result);
}

char *f2c_expression_name(Unit *unit, const F2cExpr *expression, int *supported) {
    Symbol *symbol = expression->symbol;
    if (symbol != NULL && symbol->parameter && symbol->initializer != NULL) {
        char *value = f2c_emit_typed_expression(unit, symbol->initializer_expression);
        Buffer constant = {0};
        if (value == NULL) {
            *supported = 0;
            return NULL;
        }
        if (symbol->type == TYPE_CHARACTER || symbol->type == TYPE_COMPLEX ||
            symbol->type == TYPE_DOUBLE_COMPLEX)
            f2c_buffer_printf(&constant, "(%s)", value);
        else
            f2c_buffer_printf(&constant, "((%s)(%s))", f2c_symbol_c_type(symbol), value);
        free(value);
        return f2c_buffer_take(&constant);
    }
    if (symbol != NULL && symbol->pointer && symbol->rank == 0U && !symbol->external) {
        Buffer dereference = {0};
        f2c_buffer_printf(&dereference, "(*%s)", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&dereference);
    }
    if (symbol != NULL && symbol->argument && symbol->rank == 0U && !symbol->external) {
        Buffer dereference = {0};
        f2c_buffer_printf(&dereference, "(*%s)", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&dereference);
    }
    if (unit->kind == UNIT_FUNCTION && unit->result_name != NULL && expression->text != NULL &&
        strcmp(expression->text, unit->result_name) == 0)
        return f2c_strdup("f2c_result");
    if (strcmp(expression->text != NULL ? expression->text : "", "real64") == 0)
        return f2c_strdup("8");
    if (strcmp(expression->text != NULL ? expression->text : "", "real32") == 0 ||
        strcmp(expression->text != NULL ? expression->text : "", "int32") == 0)
        return f2c_strdup("4");
    return f2c_strdup(symbol != NULL ? f2c_symbol_c_name(unit, symbol)
                                     : (expression->text != NULL ? expression->text : "0"));
}

void f2c_expression_free_arguments(char **arguments, Type *types, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i)
        free(arguments[i]);
    free(arguments);
    free(types);
}

int f2c_expression_children(Unit *unit, const F2cExpr *expression, char ***arguments_out,
                            Type **types_out) {
    char **arguments = expression->child_count != 0U
                           ? (char **)calloc(expression->child_count, sizeof(*arguments))
                           : NULL;
    Type *types = expression->child_count != 0U
                      ? (Type *)malloc(expression->child_count * sizeof(*types))
                      : NULL;
    size_t i;
    if (expression->child_count != 0U && (arguments == NULL || types == NULL)) {
        free(arguments);
        free(types);
        return 0;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        int supported = 1;
        arguments[i] = f2c_expression_emit(unit, expression->children[i], &supported);
        types[i] = expression->children[i]->type;
        if (!supported || arguments[i] == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            return 0;
        }
    }
    *arguments_out = arguments;
    *types_out = types;
    return 1;
}

static char *emit_external_actual(Unit *unit, const F2cExpr *actual, const char *code) {
    Buffer result = {0};
    Symbol *symbol;
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        actual = actual->children[0];
    if (actual == NULL)
        return NULL;
    if (actual->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return f2c_strdup("NULL");
    symbol = actual->symbol;
    if (actual->lowered_c != NULL && actual->kind == F2C_EXPR_NAME && actual->symbol == NULL &&
        actual->value_category == F2C_VALUE_VARIABLE) {
        f2c_buffer_printf(&result, "&(%s)", code);
        return f2c_buffer_take(&result);
    }
    if (actual->lowered_c != NULL)
        return f2c_strdup(code);
    if (actual->kind == F2C_EXPR_NAME && symbol != NULL) {
        if (symbol->parameter) {
            if (symbol->type == TYPE_CHARACTER)
                return f2c_strdup(code);
            return f2c_emit_scalar_temporary_address(f2c_symbol_c_type(symbol), symbol->type, code);
        }
        if (symbol->external && symbol->external_declared)
            return f2c_strdup(f2c_symbol_c_name(unit, symbol));
        if (symbol->argument || symbol->rank != 0U ||
            (symbol->type == TYPE_CHARACTER && symbol->character_length != NULL))
            return f2c_strdup(f2c_symbol_c_name(unit, symbol));
        f2c_buffer_printf(&result, "&%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (actual->kind == F2C_EXPR_ARRAY_REFERENCE) {
        f2c_buffer_printf(&result, "&%s", code);
        return f2c_buffer_take(&result);
    }
    if (actual->kind == F2C_EXPR_SUBSTRING) {
        if (code[0] == '(' && code[1] == '&')
            return f2c_strdup(code);
        f2c_buffer_printf(&result, "&%s", code);
        return f2c_buffer_take(&result);
    }
    if (actual->type == TYPE_CHARACTER)
        return f2c_strdup(code);
    return f2c_emit_scalar_temporary_address(
        actual->type != TYPE_UNKNOWN ? f2c_expression_c_type(actual) : f2c_c_type(TYPE_REAL),
        actual->type != TYPE_UNKNOWN ? actual->type : TYPE_REAL, code);
}

static char *emit_descriptor_actual(Unit *unit, const F2cExpr *actual, int *supported) {
    Buffer result = {0};
    char *character_length = NULL;
    F2cDescriptorView view = {0};
    Symbol *symbol;
    size_t dimension;
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        actual = actual->children[0];
    if (actual != NULL && actual->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return f2c_strdup("NULL");
    if (actual == NULL || actual->symbol == NULL || !f2c_descriptor_view(unit, actual, &view)) {
        *supported = 0;
        return NULL;
    }
    symbol = actual->symbol;
    if (actual->type == TYPE_CHARACTER)
        character_length = f2c_character_length_expression(unit, actual);
    f2c_buffer_printf(&result,
                      "(&(f2c_descriptor){.data = f2c_implicit_mutable_actual(%s), "
                      ".element_size = sizeof(%s), .rank = %zuU, .lower = {",
                      view.data, f2c_symbol_c_type(symbol), view.rank);
    for (dimension = 0U; dimension < view.rank; ++dimension) {
        f2c_buffer_printf(&result, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ",
                          view.lower[dimension]);
    }
    f2c_buffer_append(&result, "}, .extent = {");
    for (dimension = 0U; dimension < view.rank; ++dimension) {
        f2c_buffer_printf(&result, "%sf2c_descriptor_extent((size_t)(%s))",
                          dimension == 0U ? "" : ", ", view.extent[dimension]);
    }
    f2c_buffer_append(&result, "}, .stride = {");
    for (dimension = 0U; dimension < view.rank; ++dimension)
        f2c_buffer_printf(&result, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ",
                          view.stride[dimension]);
    f2c_buffer_printf(&result, "}, .character_length = (size_t)(%s)})",
                      character_length != NULL ? character_length : "0U");
    free(character_length);
    f2c_descriptor_view_free(&view);
    return f2c_buffer_take(&result);
}

static char *emit_type_bound_call(Unit *unit, const F2cExpr *expression, int *supported) {
    const Symbol *procedure = expression->symbol;
    const F2cExpr *callee_expression =
        expression->child_count != 0U ? expression->children[0] : NULL;
    const F2cExpr *passed_object = callee_expression != NULL &&
                                           callee_expression->kind == F2C_EXPR_COMPONENT &&
                                           callee_expression->child_count != 0U
                                       ? callee_expression->children[0]
                                       : NULL;
    const int allocatable_result = procedure != NULL && !procedure->external_subroutine &&
                                   procedure->external_result_allocatable;
    const int character_result = procedure != NULL && !allocatable_result &&
                                 !procedure->external_subroutine &&
                                 procedure->type == TYPE_CHARACTER;
    Buffer result = {0};
    char *callee;
    size_t parameter;
    size_t explicit_argument = 1U;
    if (procedure == NULL || !procedure->type_bound || callee_expression == NULL ||
        passed_object == NULL) {
        *supported = 0;
        return NULL;
    }
    callee = f2c_expression_emit(unit, callee_expression, supported);
    if (!*supported || callee == NULL)
        return NULL;
    if (character_result) {
        char *result_length;
        if (expression->temporary_index == SIZE_MAX) {
            free(callee);
            *supported = 0;
            return NULL;
        }
        result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result,
                          "(f2c_character_result_%zu = f2c_character_temporary_resize("
                          "f2c_character_result_%zu, (size_t)(%s)), "
                          "%s(f2c_character_result_%zu, (size_t)(%s)",
                          expression->temporary_index, expression->temporary_index,
                          result_length != NULL ? result_length : "1U", callee,
                          expression->temporary_index,
                          result_length != NULL ? result_length : "1U");
        free(result_length);
    } else {
        f2c_buffer_printf(&result, "%s(", callee);
    }
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        const F2cExpr *actual;
        char *code;
        char *lowered;
        if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index) {
            actual = passed_object;
        } else {
            if (explicit_argument >= expression->child_count) {
                free(callee);
                free(result.data);
                *supported = 0;
                return NULL;
            }
            actual = expression->children[explicit_argument++];
        }
        code = f2c_expression_emit(unit, actual, supported);
        lowered = *supported && code != NULL
                      ? (procedure->external_parameter_descriptor[parameter]
                             ? emit_descriptor_actual(unit, actual, supported)
                             : emit_external_actual(unit, actual, code))
                      : NULL;
        free(code);
        if (lowered == NULL) {
            free(callee);
            free(result.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "%s%s", parameter == 0U && !character_result ? "" : ", ",
                          lowered);
        free(lowered);
    }
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        const F2cExpr *actual;
        char *length;
        if (procedure->external_parameter_types[parameter] != TYPE_CHARACTER ||
            procedure->external_parameter_allocatable[parameter] ||
            procedure->external_parameter_pointer[parameter] ||
            procedure->external_parameter_descriptor[parameter])
            continue;
        if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index)
            actual = passed_object;
        else {
            size_t index = parameter + 1U;
            if (!procedure->type_bound_nopass && parameter > procedure->type_bound_pass_index)
                --index;
            actual = index < expression->child_count ? expression->children[index] : NULL;
        }
        length = actual != NULL ? f2c_character_length_expression(unit, actual) : NULL;
        f2c_buffer_printf(&result, ", %s", length != NULL ? length : "1U");
        free(length);
    }
    if (character_result) {
        char *result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result,
                          "), f2c_character_result_%zu[(size_t)(%s)] = '\\0', "
                          "f2c_character_result_%zu)",
                          expression->temporary_index, result_length != NULL ? result_length : "1U",
                          expression->temporary_index);
        free(result_length);
    } else {
        f2c_buffer_append(&result, ")");
    }
    free(callee);
    return f2c_buffer_take(&result);
}

char *f2c_expression_call(Unit *unit, const F2cExpr *expression, int *supported) {
    char **arguments = NULL;
    Type *types = NULL;
    Buffer result = {0};
    const Unit *resolved = expression->resolved_procedure != NULL &&
                                   !expression->resolved_procedure->interface_abstract
                               ? expression->resolved_procedure
                               : NULL;
    const Symbol *resolved_result = resolved != NULL && resolved->result_name != NULL
                                        ? f2c_find_symbol((Unit *)resolved, resolved->result_name)
                                        : NULL;
    const char *callee =
        resolved != NULL && resolved->name != NULL
            ? resolved->name
            : (expression->symbol != NULL ? f2c_symbol_c_name(unit, expression->symbol)
                                          : expression->text);
    const int allocatable_result =
        resolved_result != NULL
            ? resolved_result->allocatable
            : (expression->symbol != NULL && expression->symbol->external_result_allocatable);
    size_t i;
    if (expression->symbol != NULL && expression->symbol->type_bound)
        return emit_type_bound_call(unit, expression, supported);
    if (expression->symbol != NULL && expression->symbol->statement_function)
        return f2c_expression_statement_function(unit, expression, supported);
    if (expression->text != NULL && strcmp(expression->text, "present") == 0 &&
        expression->child_count == 1U && expression->children[0] != NULL &&
        expression->children[0]->kind == F2C_EXPR_NAME && expression->children[0]->symbol != NULL) {
        const Symbol *present_symbol = expression->children[0]->symbol;
        if (present_symbol->allocatable || present_symbol->pointer)
            f2c_buffer_printf(&result, "(f2c_descriptor_%s != NULL)",
                              f2c_symbol_c_name(unit, present_symbol));
        else
            f2c_buffer_printf(&result, "(%s != NULL)", f2c_symbol_c_name(unit, present_symbol));
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "allocated") == 0 &&
        expression->child_count == 1U && expression->children[0] != NULL &&
        (expression->children[0]->kind == F2C_EXPR_NAME ||
         expression->children[0]->kind == F2C_EXPR_COMPONENT) &&
        expression->children[0]->symbol != NULL && expression->children[0]->symbol->allocatable) {
        char *storage = f2c_expression_emit(unit, expression->children[0], supported);
        if (!*supported || storage == NULL) {
            free(storage);
            return NULL;
        }
        f2c_buffer_printf(&result, "(%s != NULL)", storage);
        free(storage);
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "associated") == 0 &&
        expression->child_count >= 1U && expression->child_count <= 2U &&
        expression->children[0] != NULL && expression->children[0]->symbol != NULL &&
        (expression->children[0]->symbol->pointer ||
         expression->children[0]->symbol->procedure_pointer)) {
        const Symbol *pointer = expression->children[0]->symbol;
        char *pointer_storage =
            pointer->procedure_pointer
                ? f2c_expression_emit(unit, expression->children[0], supported)
                : f2c_emit_pointer_designator(unit, expression->children[0], supported);
        if (!*supported || pointer_storage == NULL)
            return NULL;
        if (expression->child_count == 1U) {
            f2c_buffer_printf(&result, "(%s != NULL)", pointer_storage);
        } else if (expression->children[1] != NULL &&
                   expression->children[1]->kind == F2C_EXPR_NAME &&
                   expression->children[1]->symbol != NULL) {
            const Symbol *target = expression->children[1]->symbol;
            f2c_buffer_printf(&result, "(%s == %s%s)", pointer_storage,
                              pointer->procedure_pointer || target->pointer ||
                                      target->allocatable || target->argument ||
                                      target->rank != 0U || target->type == TYPE_CHARACTER
                                  ? ""
                                  : "&",
                              f2c_symbol_c_name(unit, target));
        } else {
            free(pointer_storage);
            *supported = 0;
            return NULL;
        }
        free(pointer_storage);
        (void)pointer;
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL &&
        (strcmp(expression->text, "size") == 0 ||
         ((strcmp(expression->text, "lbound") == 0 || strcmp(expression->text, "ubound") == 0) &&
          expression->rank == 0U)))
        return f2c_expression_array_inquiry(unit, expression, supported);
    if (expression->text != NULL && expression->child_count == 1U &&
        expression->children[0]->type == TYPE_CHARACTER &&
        (strcmp(expression->text, "len") == 0 || strcmp(expression->text, "len_trim") == 0)) {
        char *length = f2c_character_length_expression(unit, expression->children[0]);
        if (length == NULL) {
            *supported = 0;
            return NULL;
        }
        if (strcmp(expression->text, "len") == 0) {
            f2c_buffer_printf(&result, "((int32_t)(%s))", length);
        } else {
            char *value;
            char *pointer;
            if (expression->children[0]->rank != 0U) {
                free(length);
                *supported = 0;
                return NULL;
            }
            value = f2c_expression_emit(unit, expression->children[0], supported);
            pointer = *supported && value != NULL
                          ? f2c_character_source_pointer(unit, expression->children[0], value)
                          : NULL;
            if (pointer == NULL) {
                free(value);
                free(length);
                *supported = 0;
                return NULL;
            }
            f2c_buffer_printf(&result, "((int32_t)f2c_character_trim_length(%s, (size_t)(%s)))",
                              pointer, length);
            free(value);
            free(pointer);
        }
        free(length);
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "ichar") == 0 &&
        expression->child_count >= 1U && expression->children[0]->type == TYPE_CHARACTER) {
        char *value = f2c_expression_emit(unit, expression->children[0], supported);
        char *pointer = *supported && value != NULL
                            ? f2c_character_source_pointer(unit, expression->children[0], value)
                            : NULL;
        if (pointer == NULL) {
            free(value);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "((int32_t)(unsigned char)(%s[0]))", pointer);
        free(value);
        free(pointer);
        return f2c_buffer_take(&result);
    }
    {
        int matched = 0;
        char *reduction = f2c_expression_relation_reduction(unit, expression, supported, &matched);
        if (matched)
            return reduction;
    }
    if (expression->text != NULL &&
        (strcmp(expression->text, "sum") == 0 || strcmp(expression->text, "product") == 0 ||
         strcmp(expression->text, "maxval") == 0 || strcmp(expression->text, "minval") == 0 ||
         strcmp(expression->text, "maxloc") == 0 || strcmp(expression->text, "minloc") == 0 ||
         strcmp(expression->text, "count") == 0 || strcmp(expression->text, "any") == 0 ||
         strcmp(expression->text, "all") == 0) &&
        expression->child_count >= 1U && expression->child_count <= 2U) {
        const F2cExpr *array = intrinsic_argument_value(expression->children[0]);
        const F2cExpr *dimension = expression->child_count == 2U
                                       ? intrinsic_argument_value(expression->children[1])
                                       : NULL;
        const char *macro = strcmp(expression->text, "sum") == 0       ? "F2C_SUM"
                            : strcmp(expression->text, "product") == 0 ? "F2C_PRODUCT"
                            : strcmp(expression->text, "maxval") == 0  ? "F2C_MAXIMUM"
                            : strcmp(expression->text, "minval") == 0  ? "F2C_MINIMUM"
                            : strcmp(expression->text, "maxloc") == 0  ? "F2C_MAXIMUM_LOCATION"
                            : strcmp(expression->text, "minloc") == 0  ? "F2C_MINIMUM_LOCATION"
                            : strcmp(expression->text, "count") == 0   ? "f2c_count_l"
                            : strcmp(expression->text, "any") == 0     ? "f2c_any_l"
                                                                       : "f2c_all_l";
        char *pointer = NULL;
        char *count = NULL;
        char *stride = NULL;
        char *dimension_code = NULL;
        if (dimension != NULL && (dimension->type != TYPE_INTEGER || dimension->rank != 0U)) {
            *supported = 0;
            return NULL;
        }
        if (!f2c_expression_array_view(unit, array, &pointer, &count, &stride, supported)) {
            free(pointer);
            free(count);
            free(stride);
            *supported = 0;
            return NULL;
        }
        if (dimension != NULL)
            dimension_code = f2c_expression_emit(unit, dimension, supported);
        if (!*supported || (dimension != NULL && dimension_code == NULL)) {
            free(pointer);
            free(count);
            free(stride);
            free(dimension_code);
            return NULL;
        }
        if (dimension_code != NULL)
            f2c_buffer_printf(&result, "((%s) == 1 ? ", dimension_code);
        f2c_buffer_printf(&result, "%s(%s, %s, %s)", macro, pointer, count, stride);
        if (dimension_code != NULL)
            f2c_buffer_printf(&result, " : (abort(), (%s)0))", f2c_expression_c_type(expression));
        free(pointer);
        free(count);
        free(stride);
        free(dimension_code);
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "dot_product") == 0 &&
        expression->child_count == 2U) {
        const F2cExpr *left_array = intrinsic_argument_value(expression->children[0]);
        const F2cExpr *right_array = intrinsic_argument_value(expression->children[1]);
        char *left_pointer = NULL;
        char *left_count = NULL;
        char *left_stride = NULL;
        char *right_pointer = NULL;
        char *right_count = NULL;
        char *right_stride = NULL;
        if (!f2c_expression_array_view(unit, left_array, &left_pointer, &left_count, &left_stride,
                                       supported) ||
            !f2c_expression_array_view(unit, right_array, &right_pointer, &right_count,
                                       &right_stride, supported)) {
            free(left_pointer);
            free(left_count);
            free(left_stride);
            free(right_pointer);
            free(right_count);
            free(right_stride);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result,
                          "((%s) == (%s) ? %s(%s, %s, %s, %s, %s) : "
                          "(abort(), (%s)0))",
                          left_count, right_count,
                          expression->type == TYPE_LOGICAL ? "F2C_LOGICAL_DOT" : "F2C_DOT",
                          left_pointer, left_stride, right_pointer, right_stride, left_count,
                          f2c_expression_c_type(expression));
        free(left_pointer);
        free(left_count);
        free(left_stride);
        free(right_pointer);
        free(right_count);
        free(right_stride);
        return f2c_buffer_take(&result);
    }
    if ((strcmp(expression->text, "maxloc") == 0 || strcmp(expression->text, "maxval") == 0) &&
        expression->child_count != 0U &&
        expression->children[0]->kind == F2C_EXPR_ARRAY_REFERENCE) {
        const F2cExpr *array = expression->children[0];
        const F2cExpr *section = NULL;
        char *reference;
        char *lower;
        char *upper;
        char *stride;
        for (i = 0U; i < array->child_count; ++i) {
            if (array->children[i]->kind == F2C_EXPR_ARRAY_SECTION) {
                section = array->children[i];
                break;
            }
        }
        if (section != NULL && section->child_count == 3U &&
            section->children[1]->kind != F2C_EXPR_INVALID) {
            reference = f2c_expression_emit_array_reference(unit, array, supported);
            lower =
                section->children[0]->kind == F2C_EXPR_INVALID
                    ? (i < array->symbol->rank ? f2c_symbol_dimension_lower(unit, array->symbol, i)
                                               : f2c_strdup("1"))
                    : f2c_expression_emit(unit, section->children[0], supported);
            upper = f2c_expression_emit(unit, section->children[1], supported);
            stride = section->children[2]->kind == F2C_EXPR_INVALID
                         ? f2c_strdup("1")
                         : f2c_expression_emit(unit, section->children[2], supported);
            if (*supported && reference != NULL && lower != NULL && upper != NULL &&
                stride != NULL) {
                f2c_buffer_printf(&result, "%s(&%s, (int32_t)((((%s) - (%s)) / (%s)) + 1))",
                                  strcmp(expression->text, "maxloc") == 0 ? "F2C_MAXLOC"
                                                                          : "F2C_MAXVAL",
                                  reference, upper, lower, stride);
                free(reference);
                free(lower);
                free(upper);
                free(stride);
                return f2c_buffer_take(&result);
            }
            free(reference);
            free(lower);
            free(upper);
            free(stride);
            *supported = 0;
            return NULL;
        }
    }
    if (!f2c_expression_children(unit, expression, &arguments, &types)) {
        *supported = 0;
        return NULL;
    }
    if (f2c_is_intrinsic_name(expression->text)) {
        char *intrinsic = f2c_emit_intrinsic(expression->text, arguments, types,
                                             expression->child_count, expression->type);
        f2c_expression_free_arguments(arguments, types, expression->child_count);
        return intrinsic;
    }
    if (expression->type == TYPE_CHARACTER && !allocatable_result) {
        char *result_length;
        if (expression->temporary_index == SIZE_MAX) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            *supported = 0;
            return NULL;
        }
        result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result,
                          "(f2c_character_result_%zu = f2c_character_temporary_resize("
                          "f2c_character_result_%zu, (size_t)(%s)), "
                          "%s(f2c_character_result_%zu, (size_t)(%s)",
                          expression->temporary_index, expression->temporary_index,
                          result_length != NULL ? result_length : "1U",
                          callee != NULL ? callee : "", expression->temporary_index,
                          result_length != NULL ? result_length : "1U");
        free(result_length);
    } else {
        f2c_buffer_printf(&result, "%s(", callee != NULL ? callee : "");
    }
    for (i = 0U; i < expression->child_count; ++i) {
        const Symbol *resolved_dummy =
            resolved != NULL && i < resolved->argument_count
                ? f2c_find_symbol((Unit *)resolved, resolved->arguments[i])
                : NULL;
        const int descriptor =
            resolved_dummy != NULL
                ? f2c_symbol_uses_descriptor(resolved_dummy)
                : (expression->symbol != NULL && i < expression->symbol->external_parameter_count &&
                   expression->symbol->external_parameter_descriptor[i]);
        char *actual = descriptor
                           ? emit_descriptor_actual(unit, expression->children[i], supported)
                           : emit_external_actual(unit, expression->children[i], arguments[i]);
        char *bridged;
        if (actual == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            *supported = 0;
            return NULL;
        }
        bridged = f2c_bridge_implicit_mutable_actual(expression->symbol, i, expression->children[i],
                                                     actual);
        free(actual);
        actual = bridged;
        if (actual == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(
            &result, "%s%s",
            i == 0U && (expression->type != TYPE_CHARACTER || allocatable_result) ? "" : ", ",
            actual);
        free(actual);
    }
    for (i = 0U; i < expression->child_count; ++i) {
        const F2cExpr *actual = expression->children[i];
        char *length;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U)
            actual = actual->children[0];
        if (actual == NULL || actual->type != TYPE_CHARACTER ||
            (actual->kind == F2C_EXPR_NAME && actual->symbol != NULL && actual->symbol->external))
            continue;
        length = f2c_character_length_expression(unit, actual);
        f2c_buffer_printf(&result, ", %s", length != NULL ? length : "1U");
        free(length);
    }
    if (expression->type == TYPE_CHARACTER && !allocatable_result) {
        char *result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result,
                          "), f2c_character_result_%zu[(size_t)(%s)] = '\\0', "
                          "f2c_character_result_%zu)",
                          expression->temporary_index, result_length != NULL ? result_length : "1U",
                          expression->temporary_index);
        free(result_length);
    } else {
        f2c_buffer_append(&result, ")");
    }
    f2c_expression_free_arguments(arguments, types, expression->child_count);
    return f2c_buffer_take(&result);
}
