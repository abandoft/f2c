#include "codegen/codegen.h"

#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static int function_result_symbol(const Unit *unit, const Symbol *symbol) {
    return unit != NULL && symbol != NULL && unit->kind == UNIT_FUNCTION &&
           unit->result_name != NULL && strcmp(unit->result_name, symbol->name) == 0;
}

static char *capture_actual(Unit *caller, const Symbol *actual) {
    const char *name;
    Buffer result = {0};
    if (caller == NULL || actual == NULL)
        return NULL;
    name = f2c_symbol_c_name(caller, actual);
    if (f2c_symbol_uses_descriptor(actual)) {
        if (function_result_symbol(caller, actual) && actual->allocatable)
            return f2c_strdup("&f2c_result_descriptor");
        if (actual->argument || actual->host_associated) {
            f2c_buffer_printf(&result, "f2c_descriptor_%s", name);
            return f2c_buffer_take(&result);
        }
        return NULL;
    }
    if (function_result_symbol(caller, actual)) {
        if (actual->type == TYPE_CHARACTER || actual->rank != 0U)
            return f2c_strdup("f2c_result");
        return f2c_strdup("&f2c_result");
    }
    if (actual->argument || actual->host_associated || actual->rank != 0U ||
        actual->type == TYPE_CHARACTER)
        return f2c_strdup(name);
    f2c_buffer_printf(&result, "&%s", name);
    return f2c_buffer_take(&result);
}

static const Symbol *find_capture_actual(Unit *caller, const Unit *procedure,
                                         const Symbol *capture) {
    size_t symbol_index;
    if (caller == NULL || procedure == NULL || capture == NULL || caller->context == NULL ||
        procedure->host_index >= caller->context->units.count)
        return NULL;
    if (caller == &caller->context->units.items[procedure->host_index]) {
        return capture->host_symbol_index < caller->symbol_count
                   ? &caller->symbols[capture->host_symbol_index]
                   : NULL;
    }
    for (symbol_index = 0U; symbol_index < caller->symbol_count; ++symbol_index) {
        const Symbol *candidate = &caller->symbols[symbol_index];
        if (candidate->host_associated &&
            candidate->host_symbol_index == capture->host_symbol_index)
            return candidate;
    }
    return NULL;
}

int f2c_emit_host_capture_actuals(Buffer *output, Unit *caller, const Unit *procedure,
                                  int has_prior_argument) {
    size_t capture;
    if (output == NULL || caller == NULL)
        return 0;
    if (procedure == NULL || !procedure->internal || procedure->host_capture_count == 0U)
        return 1;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const size_t parameter = procedure->host_capture_begin + capture;
        const char *name =
            parameter < procedure->argument_count ? procedure->arguments[parameter] : NULL;
        const Symbol *formal = name != NULL ? f2c_find_symbol((Unit *)procedure, name) : NULL;
        const Symbol *actual = find_capture_actual(caller, procedure, formal);
        char *code = capture_actual(caller, actual);
        if (code == NULL) {
            f2c_diagnostic(caller->context, caller->context->lines.items[caller->begin].number, 1,
                           "cannot lower host capture '%s' for internal procedure '%s'",
                           name != NULL ? name : "<invalid>", procedure->name);
            return 0;
        }
        f2c_buffer_printf(output, "%s%s", has_prior_argument || capture != 0U ? ", " : "", code);
        free(code);
    }
    return 1;
}

int f2c_emit_host_capture_lengths(Buffer *output, Unit *caller, const Unit *procedure) {
    size_t capture;
    if (output == NULL || caller == NULL)
        return 0;
    if (procedure == NULL || !procedure->internal || procedure->host_capture_count == 0U)
        return 1;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const size_t parameter = procedure->host_capture_begin + capture;
        const char *name =
            parameter < procedure->argument_count ? procedure->arguments[parameter] : NULL;
        const Symbol *formal = name != NULL ? f2c_find_symbol((Unit *)procedure, name) : NULL;
        const Symbol *actual = find_capture_actual(caller, procedure, formal);
        char *length;
        if (actual == NULL || actual->type != TYPE_CHARACTER || f2c_symbol_uses_descriptor(actual))
            continue;
        length = function_result_symbol(caller, actual)
                     ? f2c_strdup("f2c_result_len")
                     : f2c_symbol_character_length(caller, actual);
        if (length == NULL) {
            f2c_diagnostic(caller->context, caller->context->lines.items[caller->begin].number, 1,
                           "cannot lower CHARACTER length for host capture '%s'", actual->name);
            return 0;
        }
        f2c_buffer_printf(output, ", %s", length);
        free(length);
    }
    return 1;
}
