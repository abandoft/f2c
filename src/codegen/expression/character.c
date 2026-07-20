#include "codegen/expression/private.h"

#include <stdlib.h>

typedef struct F2cCharacterArgument {
    const F2cExpr *expression;
    char *value;
    char *pointer;
    char *length;
} F2cCharacterArgument;

static void free_character_argument(F2cCharacterArgument *argument) {
    if (argument == NULL)
        return;
    free(argument->value);
    free(argument->pointer);
    free(argument->length);
    argument->value = NULL;
    argument->pointer = NULL;
    argument->length = NULL;
}

static int emit_character_argument(Unit *unit, const F2cExpr *call, const char *keyword,
                                   size_t position, F2cCharacterArgument *argument,
                                   int *supported) {
    argument->expression =
        f2c_intrinsic_argument(call->children, call->child_count, keyword, position);
    if (argument->expression == NULL || argument->expression->type != TYPE_CHARACTER)
        return 0;
    argument->value = f2c_expression_emit(unit, argument->expression, supported);
    argument->pointer =
        *supported && argument->value != NULL
            ? f2c_character_source_pointer(unit, argument->expression, argument->value)
            : NULL;
    argument->length = f2c_character_length_expression(unit, argument->expression);
    if (!*supported || argument->value == NULL || argument->pointer == NULL ||
        argument->length == NULL) {
        free_character_argument(argument);
        return 0;
    }
    return 1;
}

static char *cast_position(const F2cExpr *expression, const char *position) {
    const int kind =
        expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(TYPE_INTEGER);
    Buffer result = {0};
    f2c_buffer_printf(&result, "((%s)f2c_character_position((size_t)(%s), %d))",
                      f2c_c_type_kind(TYPE_INTEGER, kind), position, kind);
    return f2c_buffer_take(&result);
}

static char *emit_code_character(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *argument =
        f2c_intrinsic_argument(expression->children, expression->child_count, "i", 0U);
    char *code = argument != NULL ? f2c_expression_emit(unit, argument, supported) : NULL;
    Buffer result = {0};
    if (!*supported || code == NULL) {
        free(code);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "((char)f2c_character_code((int64_t)(%s)))", code);
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_adjustment(Unit *unit, const F2cExpr *expression, int right, int *supported) {
    F2cCharacterArgument string = {0};
    Buffer result = {0};
    if (expression->temporary_index == SIZE_MAX ||
        !emit_character_argument(unit, expression, "string", 0U, &string, supported)) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(
        &result, "f2c_character_adjust(&f2c_character_result_%zu, %s, (size_t)(%s), %s)",
        expression->temporary_index, string.pointer, string.length, right ? "true" : "false");
    free_character_argument(&string);
    return f2c_buffer_take(&result);
}

static char *emit_repeat(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *ncopies =
        f2c_intrinsic_argument(expression->children, expression->child_count, "ncopies", 1U);
    F2cCharacterArgument string = {0};
    char *count = ncopies != NULL ? f2c_expression_emit(unit, ncopies, supported) : NULL;
    Buffer result = {0};
    if (expression->temporary_index == SIZE_MAX || !*supported || count == NULL ||
        !emit_character_argument(unit, expression, "string", 0U, &string, supported)) {
        free(count);
        free_character_argument(&string);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result,
                      "f2c_character_repeat(&f2c_character_result_%zu, %s, (size_t)(%s), "
                      "(int64_t)(%s))",
                      expression->temporary_index, string.pointer, string.length, count);
    free(count);
    free_character_argument(&string);
    return f2c_buffer_take(&result);
}

static char *emit_trim(Unit *unit, const F2cExpr *expression, int *supported) {
    F2cCharacterArgument string = {0};
    Buffer result = {0};
    if (expression->temporary_index == SIZE_MAX ||
        !emit_character_argument(unit, expression, "string", 0U, &string, supported)) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "f2c_character_trim(&f2c_character_result_%zu, %s, (size_t)(%s))",
                      expression->temporary_index, string.pointer, string.length);
    free_character_argument(&string);
    return f2c_buffer_take(&result);
}

static char *emit_code_value(Unit *unit, const F2cExpr *expression, int *supported) {
    F2cCharacterArgument character = {0};
    Buffer position = {0};
    char *result;
    if (!emit_character_argument(unit, expression, "c", 0U, &character, supported)) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&position, "f2c_character_code_value(%s, (size_t)(%s))", character.pointer,
                      character.length);
    result = cast_position(expression, position.data);
    free(position.data);
    free_character_argument(&character);
    return result;
}

static char *emit_length(Unit *unit, const F2cExpr *expression, int trimmed, int *supported) {
    const F2cExpr *string =
        f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    char *length;
    char *result;
    if (string == NULL || string->type != TYPE_CHARACTER) {
        *supported = 0;
        return NULL;
    }
    if (!trimmed) {
        length = f2c_character_length_expression(unit, string);
    } else {
        F2cCharacterArgument argument = {0};
        Buffer trimmed_length = {0};
        if (!emit_character_argument(unit, expression, "string", 0U, &argument, supported)) {
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&trimmed_length, "f2c_character_trim_length(%s, (size_t)(%s))",
                          argument.pointer, argument.length);
        length = f2c_buffer_take(&trimmed_length);
        free_character_argument(&argument);
    }
    if (length == NULL) {
        *supported = 0;
        return NULL;
    }
    result = cast_position(expression, length);
    free(length);
    return result;
}

static char *emit_search(Unit *unit, const F2cExpr *expression, int *supported) {
    const char *secondary_name = expression->intrinsic == F2C_INTRINSIC_INDEX ? "substring" : "set";
    const F2cExpr *back =
        f2c_intrinsic_argument(expression->children, expression->child_count, "back", 2U);
    F2cCharacterArgument string = {0};
    F2cCharacterArgument secondary = {0};
    char *back_code =
        back != NULL ? f2c_expression_emit(unit, back, supported) : f2c_strdup("false");
    const char *helper =
        expression->intrinsic == F2C_INTRINSIC_INDEX
            ? "f2c_character_index"
            : (expression->intrinsic == F2C_INTRINSIC_SCAN ? "f2c_character_scan"
                                                           : "f2c_character_verify");
    Buffer position = {0};
    char *result;
    if (!*supported || back_code == NULL ||
        !emit_character_argument(unit, expression, "string", 0U, &string, supported) ||
        !emit_character_argument(unit, expression, secondary_name, 1U, &secondary, supported)) {
        free(back_code);
        free_character_argument(&string);
        free_character_argument(&secondary);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&position, "%s(%s, (size_t)(%s), %s, (size_t)(%s), (bool)(%s))", helper,
                      string.pointer, string.length, secondary.pointer, secondary.length,
                      back_code);
    result = cast_position(expression, position.data);
    free(position.data);
    free(back_code);
    free_character_argument(&string);
    free_character_argument(&secondary);
    return result;
}

char *f2c_expression_character_intrinsic(Unit *unit, const F2cExpr *expression, int *supported) {
    if (unit == NULL || expression == NULL || supported == NULL) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_ACHAR:
    case F2C_INTRINSIC_CHAR:
        return emit_code_character(unit, expression, supported);
    case F2C_INTRINSIC_ADJUSTL:
        return emit_adjustment(unit, expression, 0, supported);
    case F2C_INTRINSIC_ADJUSTR:
        return emit_adjustment(unit, expression, 1, supported);
    case F2C_INTRINSIC_IACHAR:
    case F2C_INTRINSIC_ICHAR:
        return emit_code_value(unit, expression, supported);
    case F2C_INTRINSIC_INDEX:
    case F2C_INTRINSIC_SCAN:
    case F2C_INTRINSIC_VERIFY:
        return emit_search(unit, expression, supported);
    case F2C_INTRINSIC_LEN:
        return emit_length(unit, expression, 0, supported);
    case F2C_INTRINSIC_LEN_TRIM:
        return emit_length(unit, expression, 1, supported);
    case F2C_INTRINSIC_REPEAT:
        return emit_repeat(unit, expression, supported);
    case F2C_INTRINSIC_TRIM:
        return emit_trim(unit, expression, supported);
    case F2C_INTRINSIC_NONE:
    default:
        *supported = 0;
        return NULL;
    }
}
