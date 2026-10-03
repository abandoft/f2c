#include "semantic/semantic.h"

unsigned int f2c_symbol_storage_qualifiers(const Symbol *symbol) {
    unsigned int qualifiers = F2C_STORAGE_UNQUALIFIED;
    if (symbol != NULL) {
        if (symbol->volatile_entity)
            qualifiers |= F2C_STORAGE_VOLATILE;
        if (symbol->asynchronous)
            qualifiers |= F2C_STORAGE_ASYNCHRONOUS;
    }
    return qualifiers;
}

void f2c_analyze_expression_access(F2cExpr *expression) {
    if (expression == NULL)
        return;
    expression->storage_qualifiers = F2C_STORAGE_UNQUALIFIED;
    switch (expression->kind) {
    case F2C_EXPR_NAME:
    case F2C_EXPR_ARRAY_REFERENCE:
        if (expression->value_category != F2C_VALUE_PROCEDURE ||
            (expression->symbol != NULL && expression->symbol->procedure_pointer))
            expression->storage_qualifiers = f2c_symbol_storage_qualifiers(expression->symbol);
        break;
    case F2C_EXPR_COMPONENT:
        expression->storage_qualifiers = f2c_symbol_storage_qualifiers(expression->symbol);
        if (expression->child_count != 0U && expression->children[0] != NULL)
            expression->storage_qualifiers |= expression->children[0]->storage_qualifiers;
        break;
    case F2C_EXPR_SUBSTRING:
    case F2C_EXPR_KEYWORD_ARGUMENT:
        if (expression->child_count != 0U && expression->children[0] != NULL)
            expression->storage_qualifiers = expression->children[0]->storage_qualifiers;
        break;
    default:
        /* Arithmetic, constructors and function results are values, not aliases. */
        break;
    }
}

static void analyze_access(F2cExpr *expression, void *state) {
    (void)state;
    f2c_analyze_expression_access(expression);
}

static void analyze_symbol_access(Symbol *symbol) {
    size_t dimension;
    size_t element;
    f2c_visit_expression(symbol->initializer_expression, analyze_access, NULL);
    f2c_visit_expression(symbol->character_length_expression, analyze_access, NULL);
    f2c_visit_expression(symbol->statement_function_expression, analyze_access, NULL);
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_visit_expression(symbol->dimensions[dimension].lower_expression, analyze_access, NULL);
        f2c_visit_expression(symbol->dimensions[dimension].upper_expression, analyze_access, NULL);
    }
    for (element = 0U; element < symbol->data_element_initializer_count; ++element)
        f2c_visit_expression(symbol->data_element_initializers[element], analyze_access, NULL);
}

void f2c_analyze_unit_access(Unit *unit) {
    size_t index;
    if (unit == NULL)
        return;
    for (index = 0U; index < unit->symbol_count; ++index)
        analyze_symbol_access(&unit->symbols[index]);
    for (index = 0U; index < unit->derived_type_count; ++index) {
        F2cDerivedType *type = &unit->derived_types[index];
        size_t component;
        for (component = 0U; component < type->component_count; ++component)
            analyze_symbol_access(&type->components[component]);
    }
    for (index = 0U; index < unit->statement_count; ++index)
        f2c_visit_statement_expressions(&unit->statements[index], analyze_access, NULL);
}
