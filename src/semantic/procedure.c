#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static Procedure *find_procedure(Context *context, const char *name) {
    size_t i;
    for (i = 0U; i < context->procedures.count; ++i) {
        if (strcmp(context->procedures.items[i].name, name) == 0)
            return &context->procedures.items[i];
    }
    return NULL;
}

static const char *interface_visible_name(const Unit *procedure) {
    return procedure->interface_generic_name != NULL ? procedure->interface_generic_name
                                                     : procedure->name;
}

static Symbol *function_result(Unit *procedure) {
    return procedure != NULL && procedure->kind == UNIT_FUNCTION && procedure->result_name != NULL
               ? f2c_find_symbol(procedure, procedure->result_name)
               : NULL;
}

static int same_function_result_type(const Unit *left, const Unit *right) {
    Symbol *left_result;
    Symbol *right_result;
    if (left->kind != UNIT_FUNCTION || right->kind != UNIT_FUNCTION)
        return 1;
    if (left->return_type != right->return_type || left->return_kind != right->return_kind)
        return 0;
    if (left->return_type != TYPE_DERIVED)
        return 1;
    left_result = function_result((Unit *)left);
    right_result = function_result((Unit *)right);
    return left_result != NULL && right_result != NULL &&
           left_result->derived_type == right_result->derived_type;
}

static Unit *find_explicit_interface(Unit *caller, const Symbol *external) {
    size_t i;
    if (external->procedure_interface != NULL)
        return external->procedure_interface;
    for (i = 0U; i < caller->interface_count; ++i) {
        Unit *procedure = &caller->interfaces[i];
        if (procedure->interface_abstract)
            continue;
        if (strcmp(interface_visible_name(procedure), external->name) == 0 ||
            (external->c_name != NULL && strcmp(procedure->name, external->c_name) == 0))
            return procedure;
    }
    return NULL;
}

static int same_character_length(const Symbol *left, const Symbol *right) {
    if (left == NULL || right == NULL || left->type != TYPE_CHARACTER ||
        right->type != TYPE_CHARACTER)
        return 1;
    if (left->character_length == NULL || right->character_length == NULL)
        return left->character_length == right->character_length;
    return strcmp(left->character_length, right->character_length) == 0;
}

static int same_shape_contract(const Symbol *left, const Symbol *right) {
    size_t dimension;
    if (left == NULL || right == NULL || left->rank != right->rank ||
        left->shape.kind != right->shape.kind)
        return 0;
    for (dimension = 0U; dimension < left->rank && dimension < F2C_MAX_RANK; ++dimension) {
        const F2cShapeDimension *left_dimension = &left->shape.dimensions[dimension];
        const F2cShapeDimension *right_dimension = &right->shape.dimensions[dimension];
        if (left_dimension->kind != right_dimension->kind ||
            (left_dimension->extent_known && right_dimension->extent_known &&
             left_dimension->extent != right_dimension->extent))
            return 0;
    }
    return 1;
}

static const char *function_character_length(Unit *definition) {
    Symbol *result;
    if (definition == NULL || definition->kind != UNIT_FUNCTION ||
        definition->return_type != TYPE_CHARACTER)
        return NULL;
    result = definition->result_name != NULL ? f2c_find_symbol(definition, definition->result_name)
                                             : NULL;
    if (result != NULL && result->character_length != NULL)
        return result->character_length;
    return definition->result_character_length != NULL ? definition->result_character_length : "1";
}

static int copy_function_character_length(Symbol *target, Unit *definition) {
    const char *length = function_character_length(definition);
    Symbol *result = definition != NULL && definition->kind == UNIT_FUNCTION &&
                             definition->result_name != NULL
                         ? f2c_find_symbol(definition, definition->result_name)
                         : NULL;
    char *copy;
    if (length == NULL)
        return 1;
    copy = f2c_strdup(length);
    if (copy == NULL)
        return 0;
    free(target->character_length);
    target->character_length = copy;
    target->character_length_syntax = result != NULL ? result->character_length_syntax
                                                      : definition->result_character_length_syntax;
    return 1;
}

static void validate_interface_definition(Context *context, Unit *caller, const Symbol *external,
                                          const Unit *interface, const Unit *definition) {
    const size_t line = context->lines.items[interface->begin].number;
    const char *visible_name = interface_visible_name(interface);
    size_t i;
    context->options = &caller->options;
    if (interface->kind != definition->kind) {
        f2c_diagnostic(context, line, 1,
                       "explicit interface for '%s' declares a %s but the project definition is "
                       "a %s",
                       visible_name, interface->kind == UNIT_SUBROUTINE ? "SUBROUTINE" : "FUNCTION",
                       definition->kind == UNIT_SUBROUTINE ? "SUBROUTINE" : "FUNCTION");
        return;
    }
    if (interface->elemental != definition->elemental) {
        f2c_diagnostic(context, line, 1,
                       "explicit interface for '%s' has an incompatible ELEMENTAL attribute",
                       visible_name);
    }
    if (interface->kind == UNIT_FUNCTION && !same_function_result_type(interface, definition)) {
        f2c_diagnostic(context, line, 1,
                       "explicit interface for function '%s' has return type '%s' but the "
                       "project definition has '%s'",
                       visible_name, f2c_c_type(interface->return_type),
                       f2c_c_type(definition->return_type));
    }
    if (interface->argument_count != definition->argument_count) {
        f2c_diagnostic(context, line, 1,
                       "explicit interface for '%s' has %zu dummy arguments but the project "
                       "definition has %zu",
                       visible_name, interface->argument_count, definition->argument_count);
        return;
    }
    for (i = 0U; i < interface->argument_count; ++i) {
        Symbol *declared = f2c_find_symbol((Unit *)interface, interface->arguments[i]);
        Symbol *defined = f2c_find_symbol((Unit *)definition, definition->arguments[i]);
        if (declared == NULL || defined == NULL)
            continue;
        if (declared->type != defined->type || declared->kind != defined->kind ||
            !same_shape_contract(declared, defined) ||
            declared->allocatable != defined->allocatable ||
            declared->pointer != defined->pointer || declared->optional != defined->optional ||
            declared->intent != defined->intent || declared->external != defined->external ||
            !same_character_length(declared, defined)) {
            f2c_diagnostic(context, line, 1,
                           "dummy argument %zu of explicit interface '%s' is incompatible with "
                           "the project definition",
                           i + 1U, visible_name);
        }
    }
    (void)external;
}

static int register_procedure(Context *context, Unit *unit) {
    Procedure *existing = find_procedure(context, unit->name);
    Procedure *replacement;
    size_t capacity;
    if (existing != NULL) {
        context->options = &unit->options;
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "duplicate external procedure definition '%s'", unit->name);
        return 1;
    }
    if (context->procedures.count == context->procedures.capacity) {
        capacity = context->procedures.capacity == 0U ? 16U : context->procedures.capacity * 2U;
        replacement = (Procedure *)realloc(context->procedures.items,
                                           capacity * sizeof(*context->procedures.items));
        if (replacement == NULL)
            return 0;
        context->procedures.items = replacement;
        context->procedures.capacity = capacity;
    }
    context->procedures.items[context->procedures.count].name = unit->name;
    context->procedures.items[context->procedures.count].definition = unit;
    ++context->procedures.count;
    return 1;
}

static Procedure *find_visible_internal_procedure(Context *context, const Unit *caller,
                                                  const char *name) {
    size_t index;
    if (context == NULL || caller == NULL || name == NULL)
        return NULL;
    for (index = 0U; index < context->procedures.count; ++index) {
        Procedure *procedure = &context->procedures.items[index];
        const Unit *definition = procedure->definition;
        if (definition == NULL || !definition->internal || definition->fortran_name == NULL ||
            strcmp(definition->fortran_name, name) != 0)
            continue;
        if ((caller->internal && caller->host_index == definition->host_index) ||
            (!caller->internal && definition->host_index < context->units.count &&
             &context->units.items[definition->host_index] == caller))
            return procedure;
    }
    return NULL;
}

static void bind_external(Context *context, Unit *caller, Symbol *external) {
    Unit *interface =
        external->external_signature_explicit ? find_explicit_interface(caller, external) : NULL;
    Procedure *procedure = external->c_name != NULL ? find_procedure(context, external->c_name)
                                                    : find_procedure(context, external->name);
    Unit *definition;
    size_t i;
    context->options = &caller->options;
    if (procedure == NULL && external->c_name != NULL)
        procedure = find_procedure(context, external->name);
    if (procedure == NULL)
        procedure = find_visible_internal_procedure(context, caller, external->name);
    if (interface != NULL && procedure != NULL && procedure->definition != caller)
        validate_interface_definition(context, caller, external, interface, procedure->definition);
    if (interface != NULL)
        return;
    if (procedure == NULL || procedure->definition == caller)
        return;
    definition = procedure->definition;
    if (definition->internal &&
        (external->c_name == NULL || strcmp(external->c_name, definition->name) != 0)) {
        char *resolved_name = f2c_strdup(definition->name);
        if (resolved_name == NULL) {
            f2c_diagnostic(context, context->lines.items[definition->begin].number, 1,
                           "out of memory binding internal procedure '%s'", external->name);
            return;
        }
        free(external->c_name);
        external->c_name = resolved_name;
    }
    if ((external->external_subroutine && definition->kind == UNIT_FUNCTION) ||
        (!external->external_subroutine && external->type != TYPE_UNKNOWN &&
         definition->kind == UNIT_SUBROUTINE)) {
        f2c_diagnostic(context, context->lines.items[caller->begin].number, 1,
                       "procedure '%s' is used with an incompatible procedure kind",
                       external->name);
        return;
    }
    if (definition->kind == UNIT_FUNCTION && definition->return_type != TYPE_DERIVED &&
        external->type != TYPE_UNKNOWN && external->type != definition->return_type) {
        f2c_diagnostic(context, context->lines.items[caller->begin].number, 1,
                       "procedure '%s' has return type '%s' here but is defined as '%s'",
                       external->name, f2c_c_type(external->type),
                       f2c_c_type(definition->return_type));
        return;
    }
    external->external_subroutine = definition->kind == UNIT_SUBROUTINE;
    if (!f2c_copy_function_result_metadata(external, definition)) {
        f2c_diagnostic(context, context->lines.items[definition->begin].number, 1,
                       "out of memory while binding result of procedure '%s'", external->name);
        return;
    }
    if (!copy_function_character_length(external, definition)) {
        f2c_diagnostic(context, context->lines.items[definition->begin].number, 1,
                       "out of memory while binding CHARACTER result of procedure '%s'",
                       external->name);
        return;
    }
    if (!f2c_symbol_resize_external_parameters(external, definition->argument_count)) {
        f2c_diagnostic(context, context->lines.items[definition->begin].number, 1,
                       "out of memory recording %zu parameters for procedure '%s'",
                       definition->argument_count, external->name);
        return;
    }
    external->external_parameter_count = definition->argument_count;
    for (i = 0U; i < definition->argument_count; ++i) {
        Symbol *dummy = f2c_find_symbol(definition, definition->arguments[i]);
        external->external_parameter_types[i] = dummy != NULL ? dummy->type : TYPE_REAL;
        external->external_parameter_kinds[i] =
            dummy != NULL ? dummy->kind : f2c_default_kind(TYPE_REAL);
        external->external_parameter_ranks[i] = dummy != NULL ? dummy->rank : 0U;
        external->external_parameter_intents[i] =
            dummy != NULL ? dummy->intent : F2C_INTENT_UNSPECIFIED;
        external->external_parameter_optional[i] = dummy != NULL && dummy->optional;
        external->external_parameter_allocatable[i] = dummy != NULL && dummy->allocatable;
        external->external_parameter_pointer[i] = dummy != NULL && dummy->pointer;
        external->external_parameter_descriptor[i] = f2c_symbol_uses_descriptor(dummy);
        external->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        external->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        external->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
        external->external_parameter_procedures[i] =
            dummy != NULL && dummy->external ? dummy : NULL;
    }
}

static int bind_internal(Context *context, Unit *definition) {
    Unit *host;
    Symbol *symbol;
    size_t i;
    if (!definition->internal || definition->fortran_name == NULL ||
        definition->host_index >= context->units.count)
        return 1;
    host = &context->units.items[definition->host_index];
    symbol = f2c_ensure_symbol(host, definition->fortran_name);
    if (symbol == NULL)
        return 0;
    free(symbol->c_name);
    symbol->c_name = f2c_strdup(definition->name);
    if (symbol->c_name == NULL)
        return 0;
    symbol->external = 1;
    symbol->external_declared = 1;
    symbol->external_subroutine = definition->kind == UNIT_SUBROUTINE;
    symbol->external_signature_observed = 1;
    if (!f2c_copy_function_result_metadata(symbol, definition))
        return 0;
    if (!copy_function_character_length(symbol, definition))
        return 0;
    if (!f2c_symbol_resize_external_parameters(symbol, definition->argument_count))
        return 0;
    symbol->external_parameter_count = definition->argument_count;
    for (i = 0U; i < definition->argument_count; ++i) {
        Symbol *dummy = f2c_find_symbol(definition, definition->arguments[i]);
        symbol->external_parameter_types[i] = dummy != NULL ? dummy->type : TYPE_REAL;
        symbol->external_parameter_kinds[i] =
            dummy != NULL ? dummy->kind : f2c_default_kind(TYPE_REAL);
        symbol->external_parameter_ranks[i] = dummy != NULL ? dummy->rank : 0U;
        symbol->external_parameter_intents[i] =
            dummy != NULL ? dummy->intent : F2C_INTENT_UNSPECIFIED;
        symbol->external_parameter_optional[i] = dummy != NULL && dummy->optional;
        symbol->external_parameter_allocatable[i] = dummy != NULL && dummy->allocatable;
        symbol->external_parameter_pointer[i] = dummy != NULL && dummy->pointer;
        symbol->external_parameter_descriptor[i] = f2c_symbol_uses_descriptor(dummy);
        symbol->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        symbol->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        symbol->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
        symbol->external_parameter_procedures[i] = dummy != NULL && dummy->external ? dummy : NULL;
    }
    return 1;
}

int f2c_build_procedure_registry(Context *context) {
    size_t u;
    Unit *program = NULL;
    for (u = 0U; u < context->units.count; ++u) {
        Unit *unit = &context->units.items[u];
        if (unit->kind == UNIT_PROGRAM) {
            if (program != NULL) {
                context->options = &unit->options;
                f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                               "project contains more than one PROGRAM unit ('%s' and '%s')",
                               program->name, unit->name);
            } else {
                program = unit;
            }
        } else if (unit->kind != UNIT_BLOCK_DATA && !register_procedure(context, unit)) {
            return 0;
        }
    }
    for (u = 0U; u < context->units.count; ++u) {
        if (context->units.items[u].kind != UNIT_BLOCK_DATA &&
            !bind_internal(context, &context->units.items[u]))
            return 0;
    }
    for (u = 0U; u < context->units.count; ++u) {
        Unit *unit = &context->units.items[u];
        size_t s;
        for (s = 0U; s < unit->symbol_count; ++s) {
            if (unit->symbols[s].external)
                bind_external(context, unit, &unit->symbols[s]);
        }
    }
    return 1;
}
