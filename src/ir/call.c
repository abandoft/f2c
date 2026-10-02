#include "ir/call.h"

static const F2cExpr *argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

size_t f2c_call_parameter_count(const F2cExpr *call) {
    if (call == NULL || call->kind != F2C_EXPR_CALL)
        return 0U;
    return call->symbol != NULL && call->symbol->type_bound ? call->symbol->external_parameter_count
                                                            : call->child_count;
}

const F2cExpr *f2c_call_passed_object(const F2cExpr *call) {
    const F2cExpr *binding;
    if (call == NULL || call->kind != F2C_EXPR_CALL || call->symbol == NULL ||
        !call->symbol->type_bound || call->child_count == 0U || call->children == NULL)
        return NULL;
    binding = call->children[0];
    return binding != NULL && binding->kind == F2C_EXPR_COMPONENT && binding->child_count == 1U
               ? binding->children[0]
               : NULL;
}

const F2cExpr *f2c_call_parameter_actual(const F2cExpr *call, size_t parameter) {
    const Symbol *procedure;
    size_t child = parameter;
    if (call == NULL || parameter >= f2c_call_parameter_count(call))
        return NULL;
    procedure = call->symbol;
    if (procedure != NULL && procedure->type_bound) {
        if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index)
            return f2c_call_passed_object(call);
        if (procedure->type_bound_nopass || parameter < procedure->type_bound_pass_index)
            ++child;
    }
    return call->children != NULL && child < call->child_count
               ? argument_value(call->children[child])
               : NULL;
}

size_t f2c_call_child_parameter(const F2cExpr *call, size_t child) {
    const Symbol *procedure;
    size_t parameter;
    if (call == NULL || call->kind != F2C_EXPR_CALL || child >= call->child_count)
        return SIZE_MAX;
    procedure = call->symbol;
    if (procedure == NULL || !procedure->type_bound)
        return child;
    if (child == 0U)
        return procedure->type_bound_nopass ||
                       procedure->type_bound_pass_index >= procedure->external_parameter_count
                   ? SIZE_MAX
                   : procedure->type_bound_pass_index;
    parameter = child - 1U;
    if (!procedure->type_bound_nopass && parameter >= procedure->type_bound_pass_index)
        ++parameter;
    return parameter < procedure->external_parameter_count ? parameter : SIZE_MAX;
}
