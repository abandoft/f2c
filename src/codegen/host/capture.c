#include "codegen/host/private.h"

#include <stdlib.h>
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
