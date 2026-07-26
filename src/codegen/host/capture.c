#include "codegen/host/private.h"

#include <stdlib.h>
#include <string.h>

int f2c_host_function_result_symbol(const Unit *unit, const Symbol *symbol) {
    return unit != NULL && symbol != NULL && unit->kind == UNIT_FUNCTION &&
           unit->result_name != NULL && strcmp(unit->result_name, symbol->name) == 0;
}

static const Symbol *find_associated_actual(Unit *caller, const Unit *procedure,
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

const Symbol *f2c_host_capture_actual(Unit *caller, const Unit *procedure, size_t capture,
                                      const Symbol **formal) {
    const size_t parameter = procedure != NULL && capture < procedure->host_capture_count
                                 ? procedure->host_capture_begin + capture
                                 : SIZE_MAX;
    const char *name = procedure != NULL && parameter < procedure->argument_count
                           ? procedure->arguments[parameter]
                           : NULL;
    const Symbol *resolved_formal = name != NULL ? f2c_find_symbol((Unit *)procedure, name) : NULL;
    if (formal != NULL)
        *formal = resolved_formal;
    return find_associated_actual(caller, procedure, resolved_formal);
}

int f2c_host_capture_is_local_descriptor(const Unit *caller, const Symbol *actual) {
    return caller != NULL && actual != NULL && f2c_symbol_uses_descriptor(actual) &&
           !actual->argument && !actual->host_associated &&
           !f2c_host_function_result_symbol(caller, actual);
}

int f2c_host_capture_needs_descriptor_lifecycle(const Symbol *actual) {
    return actual != NULL && (actual->allocatable || actual->pointer);
}

size_t f2c_host_capture_local_descriptor_count(Unit *caller, const Unit *procedure) {
    size_t capture;
    size_t count = 0U;
    if (caller == NULL || procedure == NULL || !procedure->internal)
        return 0U;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const Symbol *actual = f2c_host_capture_actual(caller, procedure, capture, NULL);
        if (f2c_host_capture_is_local_descriptor(caller, actual))
            ++count;
    }
    return count;
}

int f2c_host_capture_has_descriptor_lifecycle(Unit *caller, const Unit *procedure) {
    size_t capture;
    if (caller == NULL || procedure == NULL || !procedure->internal)
        return 0;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const Symbol *actual = f2c_host_capture_actual(caller, procedure, capture, NULL);
        if (f2c_host_capture_needs_descriptor_lifecycle(actual))
            return 1;
    }
    return 0;
}

static char *capture_actual_code(Unit *caller, const Symbol *actual, size_t descriptor_begin,
                                 int expression_descriptor, size_t *descriptor_ordinal) {
    const char *name;
    Buffer result = {0};
    if (caller == NULL || actual == NULL)
        return NULL;
    name = f2c_symbol_c_name(caller, actual);
    if (f2c_symbol_uses_descriptor(actual)) {
        if (f2c_host_function_result_symbol(caller, actual) && actual->allocatable)
            return f2c_strdup("&f2c_result_descriptor");
        if (actual->argument || actual->host_associated) {
            f2c_buffer_printf(&result, "f2c_descriptor_%s", name);
            return f2c_buffer_take(&result);
        }
        if (descriptor_begin == SIZE_MAX || descriptor_ordinal == NULL)
            return NULL;
        f2c_buffer_printf(&result, "&f2c_host%s_descriptor_%zu",
                          expression_descriptor ? "" : "_call",
                          descriptor_begin + (*descriptor_ordinal)++);
        return f2c_buffer_take(&result);
    }
    if (f2c_host_function_result_symbol(caller, actual)) {
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

static int emit_host_capture_actuals(Buffer *output, Unit *caller, const Unit *procedure,
                                     size_t descriptor_begin, int expression_descriptor,
                                     int has_prior_argument) {
    size_t capture;
    size_t descriptor_ordinal = 0U;
    if (output == NULL || caller == NULL)
        return 0;
    if (procedure == NULL || !procedure->internal || procedure->host_capture_count == 0U)
        return 1;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const Symbol *formal = NULL;
        const Symbol *actual = f2c_host_capture_actual(caller, procedure, capture, &formal);
        const char *name = formal != NULL ? formal->name : NULL;
        char *code = capture_actual_code(caller, actual, descriptor_begin, expression_descriptor,
                                         &descriptor_ordinal);
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

int f2c_emit_host_capture_statement_actuals(Buffer *output, Unit *caller, const Unit *procedure,
                                            int has_prior_argument) {
    const size_t descriptor_count = f2c_host_capture_local_descriptor_count(caller, procedure);
    return emit_host_capture_actuals(output, caller, procedure,
                                     descriptor_count != 0U ? 0U : SIZE_MAX, 0, has_prior_argument);
}

int f2c_emit_host_capture_expression_actuals(Buffer *output, Unit *caller, const Unit *procedure,
                                             size_t descriptor_begin, int has_prior_argument) {
    return emit_host_capture_actuals(output, caller, procedure, descriptor_begin, 1,
                                     has_prior_argument);
}

int f2c_emit_host_capture_lengths(Buffer *output, Unit *caller, const Unit *procedure) {
    size_t capture;
    if (output == NULL || caller == NULL)
        return 0;
    if (procedure == NULL || !procedure->internal || procedure->host_capture_count == 0U)
        return 1;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const Symbol *actual = f2c_host_capture_actual(caller, procedure, capture, NULL);
        char *length;
        if (actual == NULL || actual->type != TYPE_CHARACTER || f2c_symbol_uses_descriptor(actual))
            continue;
        length = f2c_host_function_result_symbol(caller, actual)
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
