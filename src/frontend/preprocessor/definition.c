#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int identifier_start(char value) {
    return isalpha((unsigned char)value) != 0 || value == '_';
}

static int identifier_continue(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_';
}

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor) != 0)
        ++cursor;
    return cursor;
}

static void diagnose_at(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                        size_t column, const char *message) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    ++span.end.column;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s", message);
}

static void diagnose_name(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                          size_t column, const char *prefix, const char *name, size_t length) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    span.end.column += length != 0U ? length : 1U;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s '%.*s'", prefix,
                             (int)(length > (size_t)INT32_MAX ? (size_t)INT32_MAX : length), name);
}

static void discard_parameters(PreprocessorMacroParameter *parameters, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index)
        free(parameters[index].name);
    free(parameters);
}

static void discard_macro(PreprocessorMacro *macro) {
    free(macro->name);
    free(macro->value);
    discard_parameters(macro->parameters, macro->parameter_count);
    memset(macro, 0, sizeof(*macro));
}

void f2c_preprocessor_discard_macros(Preprocessor *preprocessor) {
    size_t index;
    for (index = 0U; index < preprocessor->macro_count; ++index)
        discard_macro(&preprocessor->macros[index]);
    free(preprocessor->macros);
    preprocessor->macros = NULL;
    preprocessor->macro_count = 0U;
    preprocessor->macro_capacity = 0U;
}

size_t f2c_preprocessor_find_macro(const Preprocessor *preprocessor, const char *name,
                                   size_t length) {
    size_t index;
    for (index = 0U; index < preprocessor->macro_count; ++index) {
        const PreprocessorMacro *macro = &preprocessor->macros[index];
        if (macro->name_length == length && strncmp(macro->name, name, length) == 0)
            return index;
    }
    return SIZE_MAX;
}

static int reserve_macros(Preprocessor *preprocessor) {
    PreprocessorMacro *replacement;
    size_t capacity;
    if (preprocessor->macro_count < preprocessor->macro_capacity)
        return 1;
    capacity = preprocessor->macro_capacity == 0U ? 8U : preprocessor->macro_capacity * 2U;
    if (capacity < preprocessor->macro_capacity || capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement =
        (PreprocessorMacro *)realloc(preprocessor->macros, capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    preprocessor->macros = replacement;
    preprocessor->macro_capacity = capacity;
    return 1;
}

static int valid_macro_name(const char *name, size_t length) {
    size_t index;
    if (length == 0U || !identifier_start(name[0]))
        return 0;
    for (index = 1U; index < length; ++index) {
        if (!identifier_continue(name[index]))
            return 0;
    }
    return 1;
}

static int replacement_equal(const PreprocessorMacro *left, const PreprocessorMacro *right) {
    size_t left_index = 0U;
    size_t right_index = 0U;
    for (;;) {
        int left_space = 0;
        int right_space = 0;
        while (left_index < left->value_length &&
               isspace((unsigned char)left->value[left_index]) != 0) {
            left_space = 1;
            ++left_index;
        }
        while (right_index < right->value_length &&
               isspace((unsigned char)right->value[right_index]) != 0) {
            right_space = 1;
            ++right_index;
        }
        if (left_index == left->value_length || right_index == right->value_length)
            return left_index == left->value_length && right_index == right->value_length;
        if (left_space != right_space && left_index != 0U && right_index != 0U)
            return 0;
        if (left->value[left_index++] != right->value[right_index++])
            return 0;
    }
}

static int macro_equal(const PreprocessorMacro *left, const PreprocessorMacro *right) {
    size_t index;
    if (left->function_like != right->function_like || left->variadic != right->variadic ||
        left->parameter_count != right->parameter_count)
        return 0;
    for (index = 0U; index < left->parameter_count; ++index) {
        if (left->parameters[index].name_length != right->parameters[index].name_length ||
            strncmp(left->parameters[index].name, right->parameters[index].name,
                    left->parameters[index].name_length) != 0)
            return 0;
    }
    return replacement_equal(left, right);
}

static int install_macro(Preprocessor *preprocessor, const char *name, size_t name_length,
                         const char *value, size_t value_length,
                         PreprocessorMacroParameter *parameters, size_t parameter_count,
                         int function_like, int variadic, size_t line, size_t column,
                         const char *definition_source_name, size_t definition_column,
                         F2cDiagnosticCode invalid_code) {
    const size_t existing = f2c_preprocessor_find_macro(preprocessor, name, name_length);
    PreprocessorMacro macro;
    memset(&macro, 0, sizeof(macro));
    if (!valid_macro_name(name, name_length)) {
        diagnose_name(preprocessor, invalid_code, line, column, "invalid preprocessor name", name,
                      name_length);
        discard_parameters(parameters, parameter_count);
        return 0;
    }
    macro.name = f2c_strdup_n(name, name_length);
    macro.value = f2c_strdup_n(value, value_length);
    if (macro.name == NULL || macro.value == NULL) {
        free(macro.name);
        free(macro.value);
        discard_parameters(parameters, parameter_count);
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while defining a preprocessor macro");
        return 0;
    }
    macro.name_length = name_length;
    macro.value_length = value_length;
    macro.parameters = parameters;
    macro.parameter_count = parameter_count;
    macro.function_like = function_like;
    macro.variadic = variadic;
    macro.definition_source_name = definition_source_name;
    macro.definition_line = line;
    macro.definition_column = definition_column;
    if (existing != SIZE_MAX) {
        const int compatible = macro_equal(&preprocessor->macros[existing], &macro);
        discard_macro(&macro);
        if (compatible)
            return 1;
        diagnose_name(preprocessor, invalid_code, line, column,
                      "incompatible redefinition of preprocessor macro", name, name_length);
        return 0;
    }
    if (preprocessor->macro_count >= preprocessor->context->limits.max_preprocessor_definitions) {
        discard_macro(&macro);
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                    "preprocessor definition limit exceeded");
        return 0;
    }
    if (!reserve_macros(preprocessor)) {
        discard_macro(&macro);
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while growing the preprocessor definition table");
        return 0;
    }
    preprocessor->macros[preprocessor->macro_count++] = macro;
    return 1;
}

int f2c_preprocessor_define_object(Preprocessor *preprocessor, const char *name, size_t name_length,
                                   const char *value, size_t value_length, size_t line,
                                   size_t column, const char *definition_source_name,
                                   size_t definition_column, F2cDiagnosticCode invalid_code) {
    return install_macro(preprocessor, name, name_length, value, value_length, NULL, 0U, 0, 0, line,
                         column, definition_source_name, definition_column, invalid_code);
}

static int parameter_exists(const PreprocessorMacroParameter *parameters, size_t count,
                            const char *name, size_t length) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        if (parameters[index].name_length == length &&
            strncmp(parameters[index].name, name, length) == 0)
            return 1;
    }
    return 0;
}

static int append_parameter(Preprocessor *preprocessor, PreprocessorMacroParameter **parameters,
                            size_t *count, size_t *capacity, const char *name, size_t length,
                            size_t line, size_t column) {
    PreprocessorMacroParameter *replacement;
    size_t next_capacity;
    char *owned_name;
    if ((length == 11U && strncmp(name, "__VA_ARGS__", 11U) == 0) ||
        (length == 10U && strncmp(name, "__VA_OPT__", 10U) == 0)) {
        diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                      "reserved function-like macro parameter", name, length);
        return 0;
    }
    if (*count >= preprocessor->context->limits.max_macro_arguments) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                    "function-like macro parameter limit exceeded");
        return 0;
    }
    if (parameter_exists(*parameters, *count, name, length)) {
        diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                      "duplicate function-like macro parameter", name, length);
        return 0;
    }
    if (*count == *capacity) {
        next_capacity = *capacity == 0U ? 4U : *capacity * 2U;
        if (next_capacity < *capacity || next_capacity > SIZE_MAX / sizeof(*replacement)) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                        "function-like macro parameter count is too large");
            return 0;
        }
        replacement = (PreprocessorMacroParameter *)realloc(*parameters,
                                                            next_capacity * sizeof(*replacement));
        if (replacement == NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                        "out of memory while parsing function-like macro parameters");
            return 0;
        }
        *parameters = replacement;
        *capacity = next_capacity;
    }
    owned_name = f2c_strdup_n(name, length);
    if (owned_name == NULL) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while storing a function-like macro parameter");
        return 0;
    }
    (*parameters)[*count] = (PreprocessorMacroParameter){owned_name, length};
    ++*count;
    return 1;
}

int f2c_preprocessor_process_define(Preprocessor *preprocessor, const char *rest, size_t line,
                                    size_t column) {
    const char *name = skip_space(rest);
    const char *cursor;
    const char *name_end;
    const char *value;
    PreprocessorMacroParameter *parameters = NULL;
    size_t parameter_count = 0U;
    size_t parameter_capacity = 0U;
    int function_like = 0;
    int variadic = 0;
    if (!identifier_start(*name)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                    "expected a name after #define");
        return 0;
    }
    cursor = name + 1;
    while (identifier_continue(*cursor))
        ++cursor;
    name_end = cursor;
    if (*cursor == '(') {
        function_like = 1;
        ++cursor;
        cursor = skip_space(cursor);
        while (*cursor != ')') {
            const char *parameter = cursor;
            const char *end;
            if (cursor[0] == '.' && cursor[1] == '.' && cursor[2] == '.') {
                variadic = 1;
                cursor = skip_space(cursor + 3);
                if (*cursor != ')') {
                    diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line,
                                column + (size_t)(cursor - rest),
                                "variadic marker must be the final macro parameter");
                    goto failure;
                }
                break;
            }
            if (!identifier_start(*parameter)) {
                diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line,
                            column + (size_t)(parameter - rest),
                            "expected a function-like macro parameter");
                goto failure;
            }
            end = parameter + 1;
            while (identifier_continue(*end))
                ++end;
            if (!append_parameter(preprocessor, &parameters, &parameter_count, &parameter_capacity,
                                  parameter, (size_t)(end - parameter), line,
                                  column + (size_t)(parameter - rest)))
                goto failure;
            cursor = skip_space(end);
            if (*cursor == ')')
                break;
            if (*cursor != ',') {
                diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line,
                            column + (size_t)(cursor - rest),
                            "expected ',' or ')' after macro parameter");
                goto failure;
            }
            cursor = skip_space(cursor + 1);
            if (*cursor == ')') {
                diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line,
                            column + (size_t)(cursor - rest),
                            "trailing comma in function-like macro parameters");
                goto failure;
            }
        }
        ++cursor;
    }
    value = skip_space(cursor);
    if (!install_macro(preprocessor, name, (size_t)(name_end - name), value, strlen(value),
                       parameters, parameter_count, function_like, variadic, line,
                       column + (size_t)(name - rest), preprocessor->current_source_name,
                       column + (size_t)(value - rest), F2C_DIAGNOSTIC_SYNTAX))
        return 0;
    return 1;

failure:
    discard_parameters(parameters, parameter_count);
    return 0;
}

void f2c_preprocessor_undefine(Preprocessor *preprocessor, const char *name, size_t length) {
    const size_t index = f2c_preprocessor_find_macro(preprocessor, name, length);
    if (index == SIZE_MAX)
        return;
    discard_macro(&preprocessor->macros[index]);
    --preprocessor->macro_count;
    if (index != preprocessor->macro_count)
        preprocessor->macros[index] = preprocessor->macros[preprocessor->macro_count];
    memset(&preprocessor->macros[preprocessor->macro_count], 0,
           sizeof(preprocessor->macros[preprocessor->macro_count]));
}
