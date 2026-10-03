#include "ir/storage.h"

#include "ir/expression.h"

F2cStorageReference f2c_ir_symbol_storage_reference(const Symbol *symbol) {
    F2cStorageReference reference = {0};
    reference.symbol = symbol;
    if (symbol == NULL)
        return reference;
    reference.object_qualifiers = (symbol->volatile_entity ? F2C_STORAGE_VOLATILE : 0U) |
                                  (symbol->asynchronous ? F2C_STORAGE_ASYNCHRONOUS : 0U);
    reference.readonly_storage =
        symbol->parameter ||
        (symbol->argument && symbol->intent == F2C_INTENT_IN && !symbol->pointer);
    reference.readonly_state =
        symbol->parameter || (symbol->argument && symbol->intent == F2C_INTENT_IN);
    if (symbol->pointer || symbol->allocatable || symbol->procedure_pointer) {
        reference.state_qualifiers = reference.object_qualifiers;
        reference.state_source = symbol->derived_owner != NULL ? F2C_OBJECT_STATE_COMPONENT
                                 : symbol->argument && !symbol->procedure_pointer
                                     ? F2C_OBJECT_STATE_DESCRIPTOR
                                     : F2C_OBJECT_STATE_LOCAL;
    }
    return reference;
}

F2cStorageReference f2c_ir_storage_reference(const F2cExpr *expression) {
    F2cStorageReference reference = {0};
    if (expression == NULL)
        return reference;
    switch (expression->kind) {
    case F2C_EXPR_NAME:
    case F2C_EXPR_ARRAY_REFERENCE:
    case F2C_EXPR_COMPONENT:
        if (expression->symbol == NULL)
            return reference;
        reference = f2c_ir_symbol_storage_reference(expression->symbol);
        reference.expression = expression;
        reference.object_qualifiers = expression->storage_qualifiers;
        if (reference.state_source != F2C_OBJECT_STATE_NONE)
            reference.state_qualifiers = reference.object_qualifiers;
        if (expression->kind == F2C_EXPR_COMPONENT && expression->child_count != 0U) {
            const F2cStorageReference owner = f2c_ir_storage_reference(expression->children[0]);
            reference.owner = expression->children[0];
            reference.readonly_storage |= !expression->symbol->pointer && owner.readonly_storage;
            reference.readonly_state |= owner.readonly_storage;
            if (reference.state_source != F2C_OBJECT_STATE_NONE)
                reference.state_source = F2C_OBJECT_STATE_COMPONENT;
        }
        break;
    case F2C_EXPR_SUBSTRING:
    case F2C_EXPR_KEYWORD_ARGUMENT:
        if (expression->child_count != 0U)
            reference = f2c_ir_storage_reference(expression->children[0]);
        break;
    default:
        break;
    }
    return reference;
}

int f2c_ir_storage_state_definable(const F2cExpr *expression) {
    const F2cStorageReference reference = f2c_ir_storage_reference(expression);
    return reference.state_source != F2C_OBJECT_STATE_NONE && !reference.readonly_state;
}
