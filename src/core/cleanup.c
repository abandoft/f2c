#include "internal/f2c.h"

#include <stdlib.h>

static void free_symbol(Symbol *symbol) {
    size_t index;
    (void)f2c_symbol_resize_external_parameters(symbol, 0U);
    free(symbol->name);
    free(symbol->c_name);
    free(symbol->initializer);
    f2c_expr_free(symbol->initializer_expression);
    for (index = 0U; index < symbol->data_element_initializer_count; ++index)
        f2c_expr_free(symbol->data_element_initializers[index]);
    free(symbol->data_element_initializers);
    for (index = 0U; index < symbol->statement_function_argument_count; ++index)
        free(symbol->statement_function_arguments[index]);
    free(symbol->statement_function_arguments);
    free(symbol->statement_function_text);
    f2c_expr_free(symbol->statement_function_expression);
    free(symbol->character_length);
    f2c_expr_free(symbol->character_length_expression);
    free(symbol->procedure_interface_name);
    free(symbol->alias_to);
    free(symbol->common_block);
    free(symbol->derived_type_name);
    free(symbol->c_type);
    for (index = 0U; index < symbol->rank; ++index) {
        free(symbol->dimensions[index].lower);
        free(symbol->dimensions[index].upper);
        f2c_expr_free(symbol->dimensions[index].lower_expression);
        f2c_expr_free(symbol->dimensions[index].upper_expression);
    }
}

static void free_derived_type(F2cDerivedType *derived) {
    size_t index;
    free(derived->name);
    free(derived->c_name);
    free(derived->parent_name);
    for (index = 0U; index < derived->component_count; ++index)
        free_symbol(&derived->components[index]);
    free(derived->components);
    for (index = 0U; index < derived->finalizer_count; ++index)
        free(derived->finalizers[index]);
    free(derived->finalizers);
    free(derived->finalizer_procedures);
    free(derived->finalizer_ranks);
    for (index = 0U; index < derived->binding_count; ++index) {
        F2cTypeBinding *binding = &derived->bindings[index];
        free(binding->name);
        free(binding->target_name);
        free(binding->interface_name);
        free(binding->pass_name);
        free_symbol(&binding->procedure);
    }
    free(derived->bindings);
    for (index = 0U; index < F2C_DEFINED_IO_COUNT; ++index)
        free(derived->defined_io_bindings[index]);
}

void f2c_free_unit(Unit *unit) {
    size_t index;
    free(unit->name);
    free(unit->fortran_name);
    free(unit->result_name);
    free(unit->result_character_length);
    free(unit->result_derived_type_name);
    free(unit->interface_generic_name);
    free((char *)unit->options.source_name);
    for (index = 0U; index < unit->argument_count; ++index)
        free(unit->arguments[index]);
    free(unit->arguments);
    for (index = 0U; index < 26U; ++index)
        free(unit->implicit_character_lengths[index]);
    for (index = 0U; index < unit->statement_count; ++index)
        f2c_statement_free(&unit->statements[index]);
    free(unit->statements);
    for (index = 0U; index < unit->namelist_count; ++index) {
        size_t member;
        free(unit->namelists[index].name);
        for (member = 0U; member < unit->namelists[index].member_count; ++member)
            free(unit->namelists[index].members[member]);
        free(unit->namelists[index].members);
    }
    free(unit->namelists);
    for (index = 0U; index < unit->symbol_count; ++index)
        free_symbol(&unit->symbols[index]);
    free(unit->symbols);
    for (index = 0U; index < unit->derived_type_count; ++index)
        free_derived_type(&unit->derived_types[index]);
    free(unit->derived_types);
    free(unit->imported_derived_types);
    for (index = 0U; index < unit->interface_count; ++index)
        f2c_free_unit(&unit->interfaces[index]);
    free(unit->interfaces);
}
