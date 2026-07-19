#include "frontend/module_constants.h"
#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static char *module_number_initializer(const char *initializer) {
    char *result = f2c_strdup(initializer);
    char *cursor;
    if (result == NULL)
        return NULL;
    for (cursor = result; *cursor != '\0'; ++cursor) {
        if (*cursor == 'd' || *cursor == 'D' || *cursor == 'q' || *cursor == 'Q')
            *cursor = 'e';
    }
    return result;
}

static char *module_complex_initializer(const F2cModuleConstant *constant) {
    char *copy = f2c_strdup(constant->initializer);
    char *comma;
    char *closing;
    char *real_part;
    char *imaginary_part;
    char *real_c;
    char *imaginary_c;
    Buffer result = {0};
    const char *real_type = constant->type == TYPE_DOUBLE_COMPLEX ? "double" : "float";
    if (copy == NULL)
        return NULL;
    comma = strchr(copy, ',');
    closing = strrchr(copy, ')');
    if (copy[0] != '(' || comma == NULL || closing == NULL || comma >= closing) {
        free(copy);
        return NULL;
    }
    *comma = '\0';
    *closing = '\0';
    real_part = f2c_trim(copy + 1);
    imaginary_part = f2c_trim(comma + 1);
    real_c = module_number_initializer(real_part);
    imaginary_c = module_number_initializer(imaginary_part);
    if (real_c != NULL && imaginary_c != NULL)
        f2c_buffer_printf(&result, "%s((%s)(%s), (%s)(%s))",
                          constant->type == TYPE_DOUBLE_COMPLEX ? "F2C_COMPLEX_DOUBLE_INITIALIZER"
                                                                : "F2C_COMPLEX_FLOAT_INITIALIZER",
                          real_type, real_c, real_type, imaginary_c);
    free(real_c);
    free(imaginary_c);
    free(copy);
    return f2c_buffer_take(&result);
}

void f2c_emit_supported_modules(Context *context) {
    const F2cModuleConstant *constants;
    size_t count = 0U;
    size_t index;
    if (!f2c_has_la_constants_module(context))
        return;
    constants = f2c_la_constants(&count);
    f2c_buffer_append(&context->output, "/* Fortran module LA_CONSTANTS. */\n");
    for (index = 0U; index < count; ++index) {
        const F2cModuleConstant *constant = &constants[index];
        char *initializer = constant->type == TYPE_COMPLEX || constant->type == TYPE_DOUBLE_COMPLEX
                                ? module_complex_initializer(constant)
                            : constant->type != TYPE_CHARACTER
                                ? module_number_initializer(constant->initializer)
                                : NULL;
        if (constant->type != TYPE_CHARACTER && initializer == NULL) {
            f2c_diagnostic(context, 1U, 1,
                           "out of memory emitting the LA_CONSTANTS compatibility module");
            return;
        }
        f2c_buffer_printf(&context->output, "const %s f2c_la_constants_%s = ",
                          f2c_c_type_kind(constant->type, f2c_default_kind(constant->type)),
                          constant->name);
        if (constant->type == TYPE_CHARACTER) {
            const char value = constant->initializer != NULL && constant->initializer[0] != '\0'
                                   ? constant->initializer[1]
                                   : '\0';
            f2c_buffer_append(&context->output, value == '\'' || value == '\\' ? "'\\" : "'");
            f2c_buffer_append_n(&context->output, &value, 1U);
            f2c_buffer_append(&context->output, "'");
        } else if (constant->type == TYPE_COMPLEX || constant->type == TYPE_DOUBLE_COMPLEX) {
            f2c_buffer_append(&context->output, initializer);
        } else {
            f2c_buffer_printf(&context->output, "(%s)(%s)",
                              f2c_c_type_kind(constant->type, f2c_default_kind(constant->type)),
                              initializer);
        }
        f2c_buffer_append(&context->output, ";\n");
        free(initializer);
    }
    f2c_buffer_append(&context->output, "\n");
}

void f2c_emit_project_modules(Context *context) {
    size_t module_index;
    for (module_index = 0U; module_index < context->modules.count; ++module_index) {
        Unit *module = &context->modules.items[module_index];
        size_t symbol_index;
        f2c_buffer_printf(&context->output, "/* Fortran module %s. */\n", module->name);
        for (symbol_index = 0U; symbol_index < module->symbol_count; ++symbol_index) {
            Symbol *symbol = &module->symbols[symbol_index];
            if (symbol->external)
                continue;
            const char *name = f2c_symbol_c_name(module, symbol);
            const size_t line = symbol->declaration_line != 0U
                                    ? symbol->declaration_line
                                    : context->lines.items[module->begin].number;
            size_t dimension;
            char *initializer =
                symbol->initializer != NULL
                    ? f2c_emit_typed_expression(module, symbol->initializer_expression)
                    : NULL;
            if (symbol->initializer != NULL && initializer == NULL) {
                f2c_diagnostic(context, line, 1,
                               "typed initializer for module entity '%s' cannot be emitted",
                               symbol->name);
                return;
            }
            f2c_buffer_append(&context->output, symbol->parameter ? "static F2C_UNUSED const "
                                                                  : "static F2C_UNUSED ");
            if (symbol->allocatable || symbol->pointer) {
                f2c_buffer_printf(&context->output, "%s *%s = NULL;\n", f2c_symbol_c_type(symbol),
                                  name);
                if (symbol->deferred_character)
                    f2c_buffer_printf(&context->output, "static size_t f2c_char_len_%s = 0U;\n",
                                      name);
                for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                    f2c_buffer_printf(&context->output,
                                      "static int32_t %s_lower_%zu = 1;\n"
                                      "static int32_t %s_extent_%zu = 0;\n",
                                      name, dimension + 1U, name, dimension + 1U);
                }
                free(initializer);
                continue;
            }
            f2c_buffer_printf(&context->output, "%s %s", f2c_symbol_c_type(symbol), name);
            if (symbol->type == TYPE_CHARACTER && symbol->rank == 0U) {
                char *length = f2c_symbol_character_length(module, symbol);
                if (length == NULL) {
                    free(initializer);
                    f2c_diagnostic(context, line, 1,
                                   "typed character length for module entity '%s' cannot be "
                                   "emitted",
                                   symbol->name);
                    return;
                }
                f2c_buffer_printf(&context->output, "[(%s) + 1]", length);
                free(length);
            } else if (symbol->rank != 0U) {
                f2c_buffer_append(&context->output, "[");
                for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                    char *lower = f2c_emit_typed_expression(
                        module, symbol->dimensions[dimension].lower_expression);
                    char *upper = f2c_emit_typed_expression(
                        module, symbol->dimensions[dimension].upper_expression);
                    if (lower == NULL || upper == NULL) {
                        free(lower);
                        free(upper);
                        free(initializer);
                        f2c_diagnostic(context, line, 1,
                                       "typed bounds for module entity '%s' cannot be emitted",
                                       symbol->name);
                        return;
                    }
                    f2c_buffer_printf(&context->output, "%s((%s) - (%s) + 1)",
                                      dimension == 0U ? "" : " * ", upper != NULL ? upper : "0",
                                      lower != NULL ? lower : "1");
                    free(lower);
                    free(upper);
                }
                f2c_buffer_append(&context->output, "]");
            }
            if (initializer != NULL)
                f2c_buffer_printf(&context->output, " = %s", initializer);
            else
                f2c_buffer_append(&context->output, " = {0}");
            f2c_buffer_append(&context->output, ";\n");
            free(initializer);
        }
        f2c_buffer_append(&context->output, "\n");
    }
}
