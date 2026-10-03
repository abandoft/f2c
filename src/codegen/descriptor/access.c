#include "codegen/descriptor/private.h"

const char *f2c_descriptor_address_member(unsigned int qualifiers, int readonly_storage) {
    if ((qualifiers & F2C_STORAGE_VOLATILE) != 0U)
        return readonly_storage ? "readonly_volatile_data" : "volatile_data";
    return readonly_storage ? "readonly_data" : "data";
}

int f2c_descriptor_readonly_storage(const F2cExpr *expression) {
    if (expression == NULL || expression->symbol == NULL)
        return 0;
    const Symbol *symbol = expression->symbol;
    if (symbol->parameter ||
        (symbol->argument && symbol->intent == F2C_INTENT_IN && !symbol->pointer))
        return 1;
    return expression->kind == F2C_EXPR_COMPONENT && !symbol->pointer &&
           expression->child_count != 0U &&
           f2c_descriptor_readonly_storage(expression->children[0]);
}
