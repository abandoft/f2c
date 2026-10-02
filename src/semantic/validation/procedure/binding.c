#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

int f2c_validation_bound_arguments(Context *context, Unit *caller, size_t line,
                                   const char *statement_text, F2cExpr *binding,
                                   F2cExpr ***arguments, char ***items, size_t *count,
                                   int subroutine) {
    const Symbol *procedure = binding != NULL ? binding->symbol : NULL;
    Unit *definition = procedure != NULL ? procedure->procedure_interface : NULL;
    size_t implicit;
    F2cExpr *owner;
    size_t explicit_count = 0U;
    if (procedure == NULL || !procedure->type_bound || definition == NULL ||
        binding->kind != F2C_EXPR_COMPONENT || binding->child_count != 1U) {
        f2c_diagnostic(context, line, 1,
                       "type-bound call has no resolved interface or passed object");
        return 0;
    }
    if ((subroutine && definition->kind != UNIT_SUBROUTINE) ||
        (!subroutine && definition->kind != UNIT_FUNCTION)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &binding->span, 1,
                                 "type-bound procedure '%s' cannot be invoked as a %s",
                                 binding->text, subroutine ? "SUBROUTINE" : "FUNCTION");
        return 0;
    }
    implicit = procedure->type_bound_nopass ? SIZE_MAX : procedure->type_bound_pass_index;
    owner = implicit != SIZE_MAX ? binding->children[0] : NULL;
    if (!f2c_validation_bind_procedure_arguments(context, definition, line, statement_text,
                                                 binding->text, &binding->span, arguments, items,
                                                 count, NULL, implicit, owner))
        return 0;
    for (size_t parameter = 0U; parameter < *count; ++parameter) {
        const Symbol *dummy = f2c_find_symbol(definition, definition->arguments[parameter]);
        f2c_validation_procedure_actual(context, caller, definition, dummy, (*arguments)[parameter],
                                        parameter, line, statement_text);
        if (parameter == implicit)
            continue;
        (*arguments)[explicit_count] = (*arguments)[parameter];
        if (items != NULL)
            (*items)[explicit_count] = (*items)[parameter];
        ++explicit_count;
    }
    *count = explicit_count;
    return 1;
}

void f2c_validation_bound_function(Context *context, Unit *caller, size_t line,
                                   const char *statement_text, F2cExpr *call) {
    F2cExpr **arguments = NULL;
    F2cExpr **children = NULL;
    F2cExpr *binding;
    size_t count;
    size_t capacity;
    if (call == NULL || call->child_count == 0U || call->symbol == NULL)
        return;
    binding = call->children[0];
    count = call->child_count - 1U;
    capacity = call->symbol->external_parameter_count + 1U;
    if (count != 0U)
        arguments = (F2cExpr **)malloc(count * sizeof(*arguments));
    children = (F2cExpr **)calloc(capacity, sizeof(*children));
    if ((count != 0U && arguments == NULL) || children == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call->span, 1,
                                 "out of memory associating type-bound function arguments");
        free(arguments);
        free(children);
        return;
    }
    if (count != 0U)
        memcpy(arguments, call->children + 1U, count * sizeof(*arguments));
    if (!f2c_validation_bound_arguments(context, caller, line, statement_text, binding, &arguments,
                                        NULL, &count, 0)) {
        free(arguments);
        free(children);
        return;
    }
    children[0] = binding;
    if (count != 0U)
        memcpy(children + 1U, arguments, count * sizeof(*arguments));
    free(arguments);
    free(call->children);
    call->children = children;
    call->child_count = count + 1U;
    call->child_capacity = capacity;
}
