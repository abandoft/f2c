#include "codegen/call/private.h"

static int designates_readonly_dummy(const F2cExpr *actual) {
    if (actual == NULL)
        return 0;
    if (actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        return designates_readonly_dummy(actual->children[0]);
    if (actual->kind != F2C_EXPR_NAME && actual->kind != F2C_EXPR_ARRAY_REFERENCE &&
        actual->kind != F2C_EXPR_SUBSTRING && actual->kind != F2C_EXPR_COMPONENT)
        return 0;
    if (actual->symbol != NULL && actual->symbol->argument &&
        actual->symbol->intent == F2C_INTENT_IN)
        return 1;
    for (size_t child = 0U; child < actual->child_count; ++child)
        if (designates_readonly_dummy(actual->children[child]))
            return 1;
    return 0;
}

char *f2c_call_bridge_readonly_actual(const Symbol *callee, size_t parameter, const F2cExpr *actual,
                                      const char *code) {
    if (code == NULL)
        return NULL;
    if (callee == NULL || parameter >= callee->external_parameter_count ||
        callee->external_parameter_const[parameter] ||
        callee->external_parameter_value[parameter] ||
        callee->external_parameter_intents[parameter] != F2C_INTENT_UNSPECIFIED ||
        callee->external_parameter_allocatable[parameter] ||
        callee->external_parameter_pointer[parameter] ||
        callee->external_parameter_descriptor[parameter] ||
        callee->external_parameter_procedures[parameter] != NULL ||
        !designates_readonly_dummy(actual))
        return f2c_strdup(code);
    /* An unspecified-INTENT dummy may read a readonly actual. Preserve its
     * address identity; this ABI adapter grants no Fortran definition rights. */
    Buffer result = {0};
    f2c_buffer_printf(&result, "f2c_borrowed_actual_address(%s)", code);
    return f2c_buffer_take(&result);
}
