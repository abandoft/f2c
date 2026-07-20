#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static Unit *find_finalizer(Context *context, const char *name) {
    size_t index;
    for (index = 0U; index < context->units.count; ++index) {
        Unit *candidate = &context->units.items[index];
        const char *visible =
            candidate->fortran_name != NULL ? candidate->fortran_name : candidate->name;
        if (visible != NULL && strcmp(visible, name) == 0)
            return candidate;
    }
    return NULL;
}

static F2cTypeBinding *find_binding(F2cDerivedType *derived, const char *name) {
    size_t index;
    if (derived == NULL || name == NULL)
        return NULL;
    for (index = 0U; index < derived->binding_count; ++index)
        if (strcmp(derived->bindings[index].name, name) == 0)
            return &derived->bindings[index];
    return find_binding(derived->parent, name);
}

static int type_extends(const F2cDerivedType *candidate, const F2cDerivedType *ancestor) {
    while (candidate != NULL) {
        if (candidate == ancestor)
            return 1;
        candidate = candidate->parent;
    }
    return 0;
}

static void emit_binding_designator(Buffer *output, const char *object,
                                    const F2cDerivedType *dynamic_type,
                                    const F2cDerivedType *storage_owner, const char *name) {
    const F2cDerivedType *owner = dynamic_type;
    int through_parent = 0;
    f2c_buffer_append(output, object);
    while (owner != NULL && owner != storage_owner) {
        f2c_buffer_append(output, "->parent");
        through_parent = 1;
        owner = owner->parent;
    }
    f2c_buffer_printf(output, "%s%s", through_parent ? "." : "->", name);
}

static void emit_component(Context *context, Unit *unit, Symbol *component) {
    Buffer *output = &context->output;
    size_t dimension;
    if (component->procedure_pointer) {
        f2c_buffer_append(output, "    ");
        f2c_emit_procedure_pointer_type(output, component, f2c_symbol_c_name(unit, component));
        f2c_buffer_append(output, ";\n");
        return;
    }
    f2c_buffer_printf(output, "    %s%s %s", f2c_symbol_c_type(component),
                      component->pointer || component->allocatable ? " *" : "",
                      f2c_symbol_c_name(unit, component));
    if (component->allocatable || component->pointer) {
        f2c_buffer_append(output, ";\n");
        if (component->deferred_character)
            f2c_buffer_printf(output, "    size_t %s_character_length;\n",
                              f2c_symbol_c_name(unit, component));
        for (dimension = 0U; dimension < component->rank; ++dimension) {
            f2c_buffer_printf(output, "    int32_t %s_lower_%zu;\n",
                              f2c_symbol_c_name(unit, component), dimension + 1U);
            f2c_buffer_printf(output, "    int32_t %s_extent_%zu;\n",
                              f2c_symbol_c_name(unit, component), dimension + 1U);
        }
        return;
    }
    if (component->type == TYPE_CHARACTER && component->rank == 0U) {
        char *length = component->character_length != NULL
                           ? f2c_emit_typed_expression(unit, component->character_length_expression)
                           : f2c_strdup("1");
        f2c_buffer_printf(output, "[(%s) + 1]", length != NULL ? length : "1");
        free(length);
    } else if (!component->pointer && component->rank != 0U) {
        f2c_buffer_append(output, "[");
        if (component->type == TYPE_CHARACTER) {
            char *length =
                component->character_length != NULL
                    ? f2c_emit_typed_expression(unit, component->character_length_expression)
                    : f2c_strdup("1");
            f2c_buffer_printf(output, "(size_t)(%s) * ", length != NULL ? length : "1");
            free(length);
        }
        for (dimension = 0U; dimension < component->rank; ++dimension) {
            char *lower =
                f2c_emit_typed_expression(unit, component->dimensions[dimension].lower_expression);
            char *upper =
                f2c_emit_typed_expression(unit, component->dimensions[dimension].upper_expression);
            f2c_buffer_printf(output, "%s((%s) - (%s) + 1)", dimension == 0U ? "" : " * ",
                              upper != NULL ? upper : "0", lower != NULL ? lower : "1");
            free(lower);
            free(upper);
        }
        f2c_buffer_append(output, "]");
    }
    f2c_buffer_append(output, ";\n");
}

static void emit_unit_type_declarations(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            f2c_buffer_printf(&context->output, "typedef struct %s %s;\n", derived->c_name,
                              derived->c_name);
        }
    }
}

static void emit_unit_types(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            size_t component;
            f2c_buffer_printf(&context->output, "struct %s {\n", derived->c_name);
            if (derived->parent != NULL)
                f2c_buffer_printf(&context->output, "    %s parent;\n", derived->parent->c_name);
            f2c_buffer_append(&context->output, "    uint64_t f2c_type_tag;\n");
            f2c_buffer_append(&context->output, "    size_t f2c_dynamic_size;\n");
            for (component = 0U; component < derived->component_count; ++component)
                emit_component(context, unit, &derived->components[component]);
            for (component = 0U; component < derived->binding_count; ++component) {
                F2cTypeBinding *binding = &derived->bindings[component];
                if (binding->overridden != NULL)
                    continue;
                f2c_buffer_append(&context->output, "    ");
                f2c_emit_procedure_pointer_type(&context->output, &binding->procedure,
                                                binding->name);
                f2c_buffer_append(&context->output, ";\n");
            }
            f2c_buffer_append(&context->output, "};\n\n");
        }
    }
}

static void emit_type_identifiers(Context *context, Units *units, uint64_t *next_identifier) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            f2c_buffer_printf(&context->output, "#define F2C_TYPE_ID_%s UINT64_C(%llu)\n",
                              derived->c_name, (unsigned long long)(*next_identifier));
            ++*next_identifier;
        }
    }
}

static void emit_lifecycle_prototypes(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_initialize_%s(%s *value);\n"
                              "static F2C_UNUSED void f2c_destroy_own_components_%s(%s *value);\n"
                              "static F2C_UNUSED void f2c_destroy_components_%s(%s *value);\n"
                              "static F2C_UNUSED void f2c_destroy_%s(%s *value);\n"
                              "static F2C_UNUSED void f2c_destroy_array_%s(%s *value, size_t "
                              "count, size_t rank);\n"
                              "static F2C_UNUSED void f2c_destroy_dynamic_%s(%s *value, size_t "
                              "count, size_t rank);\n"
                              "static F2C_UNUSED void f2c_initialize_dynamic_%s(%s *value, "
                              "size_t count);\n"
                              "static F2C_UNUSED void f2c_clone_%s(%s *target, const %s *source);\n"
                              "static F2C_UNUSED void f2c_copy_%s(%s *target, const %s *source);\n",
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name);
            for (size_t finalizer = 0U; finalizer < derived->finalizer_count; ++finalizer) {
                Unit *procedure = find_finalizer(context, derived->finalizers[finalizer]);
                if (procedure != NULL)
                    f2c_emit_procedure_prototype(&context->output, procedure);
            }
        }
    }
}

static char *component_count(Unit *unit, Symbol *component, const char *object) {
    Buffer result = {0};
    size_t dimension;
    if (component->rank == 0U)
        return f2c_strdup("1U");
    for (dimension = 0U; dimension < component->rank; ++dimension) {
        if (component->allocatable || component->pointer) {
            f2c_buffer_printf(&result, "%s(size_t)%s->%s_extent_%zu", dimension == 0U ? "" : " * ",
                              object, f2c_symbol_c_name(unit, component), dimension + 1U);
        } else {
            char *extent = f2c_symbol_dimension_extent(unit, component, dimension);
            f2c_buffer_printf(&result, "%s(size_t)(%s)", dimension == 0U ? "" : " * ",
                              extent != NULL ? extent : "0U");
            free(extent);
        }
    }
    return f2c_buffer_take(&result);
}

static const char *procedure_parameter_type(const Symbol *procedure, size_t parameter) {
    if (procedure->external_parameter_types[parameter] == TYPE_DERIVED &&
        procedure->external_parameter_derived_types[parameter] != NULL)
        return procedure->external_parameter_derived_types[parameter]->c_name;
    return f2c_c_type_kind(procedure->external_parameter_types[parameter],
                           procedure->external_parameter_kinds[parameter]);
}

static void emit_dispatch_parameter(Buffer *output, const Symbol *procedure, size_t parameter) {
    Symbol *nested = procedure->external_parameter_procedures[parameter];
    if (nested != NULL) {
        Buffer name = {0};
        f2c_buffer_printf(&name, "f2c_argument_%zu", parameter);
        f2c_emit_procedure_pointer_type(output, nested, name.data);
        free(name.data);
    } else if (procedure->external_parameter_descriptor[parameter]) {
        f2c_buffer_printf(output, "f2c_descriptor *f2c_argument_%zu", parameter);
    } else if (procedure->type_bound && parameter == procedure->type_bound_pass_index &&
               !procedure->type_bound_nopass) {
        f2c_buffer_printf(output, "%svoid *f2c_argument_%zu",
                          procedure->external_parameter_const[parameter] ? "const " : "",
                          parameter);
    } else {
        f2c_buffer_printf(output, "%s%s *f2c_argument_%zu",
                          procedure->external_parameter_const[parameter] ? "const " : "",
                          procedure_parameter_type(procedure, parameter), parameter);
    }
}

static void emit_dispatch_wrapper(Context *context, F2cDerivedType *dynamic_type,
                                  F2cTypeBinding *storage, F2cTypeBinding *effective) {
    Symbol *signature = &storage->procedure;
    Unit *target = find_finalizer(context, effective->target_name);
    const int allocatable_result =
        !signature->external_subroutine && signature->external_result_allocatable;
    const int character_result =
        !allocatable_result && !signature->external_subroutine && signature->type == TYPE_CHARACTER;
    size_t parameter;
    if (target == NULL || effective->deferred)
        return;
    f2c_emit_procedure_prototype(&context->output, target);
    f2c_buffer_printf(&context->output, "static %s f2c_dispatch_%s_%s(",
                      allocatable_result ? "f2c_descriptor"
                      : signature->external_subroutine || character_result
                          ? "void"
                          : f2c_symbol_c_type(signature),
                      dynamic_type->c_name, storage->name);
    if (character_result)
        f2c_buffer_append(&context->output, "char *f2c_result, size_t f2c_result_length");
    if (signature->external_parameter_count == 0U && !character_result)
        f2c_buffer_append(&context->output, "void");
    for (parameter = 0U; parameter < signature->external_parameter_count; ++parameter) {
        if (parameter != 0U || character_result)
            f2c_buffer_append(&context->output, ", ");
        emit_dispatch_parameter(&context->output, signature, parameter);
    }
    for (parameter = 0U; parameter < signature->external_parameter_count; ++parameter)
        if (signature->external_parameter_types[parameter] == TYPE_CHARACTER &&
            !signature->external_parameter_allocatable[parameter] &&
            !signature->external_parameter_pointer[parameter] &&
            !signature->external_parameter_descriptor[parameter])
            f2c_buffer_printf(&context->output, ", size_t f2c_length_%zu", parameter);
    f2c_buffer_append(&context->output, ") {\n    ");
    if (!signature->external_subroutine && !character_result)
        f2c_buffer_append(&context->output, "return ");
    f2c_buffer_printf(&context->output, "%s(", target->name);
    if (character_result)
        f2c_buffer_append(&context->output, "f2c_result, f2c_result_length");
    for (parameter = 0U; parameter < signature->external_parameter_count; ++parameter) {
        if (parameter != 0U || character_result)
            f2c_buffer_append(&context->output, ", ");
        if (!effective->nopass && parameter == effective->procedure.type_bound_pass_index) {
            F2cDerivedType *passed_type =
                effective->procedure.external_parameter_derived_types[parameter];
            f2c_buffer_printf(
                &context->output, "(%s%s *)f2c_argument_%zu",
                effective->procedure.external_parameter_const[parameter] ? "const " : "",
                passed_type != NULL ? passed_type->c_name : dynamic_type->c_name, parameter);
        } else {
            f2c_buffer_printf(&context->output, "f2c_argument_%zu", parameter);
        }
    }
    for (parameter = 0U; parameter < signature->external_parameter_count; ++parameter)
        if (signature->external_parameter_types[parameter] == TYPE_CHARACTER &&
            !signature->external_parameter_allocatable[parameter] &&
            !signature->external_parameter_pointer[parameter] &&
            !signature->external_parameter_descriptor[parameter])
            f2c_buffer_printf(&context->output, ", f2c_length_%zu", parameter);
    f2c_buffer_append(&context->output, ");\n}\n");
}

static void emit_dispatch_wrappers(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *dynamic_type = &unit->derived_types[type_index];
            F2cDerivedType *owner;
            for (owner = dynamic_type; owner != NULL; owner = owner->parent) {
                size_t binding_index;
                for (binding_index = 0U; binding_index < owner->binding_count; ++binding_index) {
                    F2cTypeBinding *storage = &owner->bindings[binding_index];
                    F2cTypeBinding *effective;
                    if (storage->overridden != NULL)
                        continue;
                    effective = find_binding(dynamic_type, storage->name);
                    if (effective != NULL)
                        emit_dispatch_wrapper(context, dynamic_type, storage, effective);
                }
            }
        }
    }
}

static void emit_dynamic_binding_initialization(Context *context, F2cDerivedType *derived,
                                                const char *object, int indentation) {
    F2cDerivedType *owner;
    for (owner = derived; owner != NULL; owner = owner->parent) {
        size_t binding_index;
        for (binding_index = 0U; binding_index < owner->binding_count; ++binding_index) {
            F2cTypeBinding *storage = &owner->bindings[binding_index];
            F2cTypeBinding *effective;
            if (storage->overridden != NULL)
                continue;
            effective = find_binding(derived, storage->name);
            f2c_buffer_printf(&context->output, "%*s", indentation, "");
            emit_binding_designator(&context->output, object, derived, owner, storage->name);
            if (effective != NULL && !effective->deferred &&
                find_finalizer(context, effective->target_name) != NULL)
                f2c_buffer_printf(&context->output, " = f2c_dispatch_%s_%s;\n", derived->c_name,
                                  storage->name);
            else
                f2c_buffer_append(&context->output, " = NULL;\n");
        }
    }
}

static void emit_component_initialization(Context *context, Unit *unit, F2cDerivedType *derived) {
    size_t component_index;
    for (component_index = 0U; component_index < derived->component_count; ++component_index) {
        Symbol *component = &derived->components[component_index];
        const char *name = f2c_symbol_c_name(unit, component);
        if (component->pointer || component->allocatable || component->type != TYPE_DERIVED ||
            component->derived_type == NULL)
            continue;
        if (component->rank == 0U) {
            f2c_buffer_printf(&context->output, "    f2c_initialize_%s(&value->%s);\n",
                              component->derived_type->c_name, name);
        } else {
            char *count = component_count(unit, component, "value");
            f2c_buffer_printf(&context->output,
                              "    for (size_t i = 0U; i < %s; ++i) "
                              "f2c_initialize_%s(&value->%s[i]);\n",
                              count != NULL ? count : "0U", component->derived_type->c_name, name);
            free(count);
        }
    }
}

static void emit_component_finalization(Context *context, Unit *unit, F2cDerivedType *derived) {
    size_t component_index = derived->component_count;
    while (component_index != 0U) {
        Symbol *component = &derived->components[--component_index];
        const char *name = f2c_symbol_c_name(unit, component);
        if (component->allocatable) {
            if (component->type == TYPE_DERIVED && component->derived_type != NULL) {
                char *count = component_count(unit, component, "value");
                f2c_buffer_printf(&context->output,
                                  "    if (value->%s != NULL) "
                                  "f2c_destroy_array_%s(value->%s, %s, %zuU);\n",
                                  name, component->derived_type->c_name, name,
                                  count != NULL ? count : "0U", component->rank);
                free(count);
            }
            f2c_buffer_printf(&context->output, "    free(value->%s); value->%s = NULL;\n", name,
                              name);
            for (size_t dimension = 0U; dimension < component->rank; ++dimension)
                f2c_buffer_printf(&context->output,
                                  "    value->%s_lower_%zu = 1; value->%s_extent_%zu = 0;\n", name,
                                  dimension + 1U, name, dimension + 1U);
            if (component->deferred_character)
                f2c_buffer_printf(&context->output, "    value->%s_character_length = 0U;\n", name);
        } else if (!component->pointer && component->type == TYPE_DERIVED &&
                   component->derived_type != NULL) {
            if (component->rank == 0U) {
                f2c_buffer_printf(&context->output, "    f2c_destroy_%s(&value->%s);\n",
                                  component->derived_type->c_name, name);
            } else {
                char *count = component_count(unit, component, "value");
                f2c_buffer_printf(&context->output,
                                  "    f2c_destroy_array_%s(value->%s, %s, %zuU);\n",
                                  component->derived_type->c_name, name,
                                  count != NULL ? count : "0U", component->rank);
                free(count);
            }
        }
    }
}

static void emit_lifecycle_definitions(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            size_t component_index;
            F2cDerivedType *ancestor;
            Buffer parent_path = {0};
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_initialize_%s(%s *value) {\n",
                              derived->c_name, derived->c_name);
            ancestor = derived;
            while (ancestor != NULL) {
                if (ancestor == derived) {
                    f2c_buffer_printf(&context->output,
                                      "    value->f2c_type_tag = F2C_TYPE_ID_%s;\n",
                                      derived->c_name);
                    f2c_buffer_printf(&context->output,
                                      "    value->f2c_dynamic_size = sizeof(%s);\n",
                                      derived->c_name);
                    f2c_buffer_append(&parent_path, "value->parent");
                } else {
                    f2c_buffer_printf(&context->output, "    %s.f2c_type_tag = F2C_TYPE_ID_%s;\n",
                                      parent_path.data, derived->c_name);
                    f2c_buffer_printf(&context->output, "    %s.f2c_dynamic_size = sizeof(%s);\n",
                                      parent_path.data, derived->c_name);
                    f2c_buffer_append(&parent_path, ".parent");
                }
                ancestor = ancestor->parent;
            }
            free(parent_path.data);
            emit_dynamic_binding_initialization(context, derived, "value", 4);
            emit_component_initialization(context, unit, derived);
            f2c_buffer_append(&context->output, "}\n");
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_destroy_own_components_%s(%s *value) {\n"
                              "    (void)value;\n",
                              derived->c_name, derived->c_name);
            emit_component_finalization(context, unit, derived);
            f2c_buffer_append(&context->output, "}\n");
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_destroy_components_%s(%s *value) {\n"
                              "    f2c_destroy_own_components_%s(value);\n",
                              derived->c_name, derived->c_name, derived->c_name);
            if (derived->parent != NULL)
                f2c_buffer_printf(&context->output, "    f2c_destroy_%s(&value->parent);\n",
                                  derived->parent->c_name);
            f2c_buffer_append(&context->output, "}\n");
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_destroy_%s(%s *value) {\n"
                              "    (void)value;\n",
                              derived->c_name, derived->c_name);
            for (size_t finalizer = 0U; finalizer < derived->finalizer_count; ++finalizer) {
                Unit *procedure = derived->finalizer_procedures != NULL
                                      ? derived->finalizer_procedures[finalizer]
                                      : find_finalizer(context, derived->finalizers[finalizer]);
                if (procedure != NULL &&
                    (derived->finalizer_ranks == NULL || derived->finalizer_ranks[finalizer] == 0U))
                    f2c_buffer_printf(&context->output, "    %s(value);\n", procedure->name);
            }
            f2c_buffer_printf(&context->output, "    f2c_destroy_components_%s(value);\n",
                              derived->c_name);
            f2c_buffer_append(&context->output, "}\n");
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_destroy_array_%s(%s *value, size_t "
                              "count, size_t rank) {\n    (void)rank;\n",
                              derived->c_name, derived->c_name);
            for (size_t finalizer = 0U; finalizer < derived->finalizer_count; ++finalizer) {
                Unit *procedure = derived->finalizer_procedures != NULL
                                      ? derived->finalizer_procedures[finalizer]
                                      : NULL;
                const size_t rank =
                    derived->finalizer_ranks != NULL ? derived->finalizer_ranks[finalizer] : 0U;
                if (procedure == NULL || rank == 0U)
                    continue;
                f2c_buffer_printf(&context->output,
                                  "    if (rank == %zuU) { f2c_descriptor f2c_finalizer = {"
                                  ".data = value, .element_size = sizeof(*value), .rank = rank}; "
                                  "for (size_t d = 0U; d < rank; ++d) { "
                                  "f2c_finalizer.lower[d] = 1; f2c_finalizer.extent[d] = "
                                  "d == 0U ? (int64_t)count : 1; f2c_finalizer.stride[d] = "
                                  "d == 0U ? 1 : f2c_descriptor_stride_extent("
                                  "f2c_finalizer.stride[d - 1U], "
                                  "(size_t)f2c_finalizer.extent[d - 1U]); } %s(&f2c_finalizer); "
                                  "for (size_t i = count; i-- > 0U;) "
                                  "f2c_destroy_own_components_%s(&value[i]); ",
                                  rank, procedure->name, derived->c_name);
                if (derived->parent != NULL)
                    f2c_buffer_printf(
                        &context->output,
                        "%s *parents = (%s *)malloc((count == 0U ? 1U : count) * "
                        "sizeof(*parents)); if (parents == NULL) abort(); "
                        "for (size_t i = 0U; i < count; ++i) { parents[i] = value[i].parent; "
                        "memset(&value[i].parent, 0, sizeof(value[i].parent)); } "
                        "f2c_destroy_array_%s(parents, count, rank); free(parents); ",
                        derived->parent->c_name, derived->parent->c_name, derived->parent->c_name);
                f2c_buffer_append(&context->output, "return; }\n");
            }
            f2c_buffer_printf(&context->output,
                              "    for (size_t i = count; i-- > 0U;) "
                              "f2c_destroy_%s(&value[i]);\n}\n",
                              derived->c_name);
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_clone_%s(%s *target, const %s *source) "
                              "{\n    %s temporary = {0};\n",
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name);
            f2c_buffer_append(&context->output, "    (void)source;\n");
            if (derived->parent != NULL)
                f2c_buffer_printf(&context->output,
                                  "    f2c_clone_%s(&temporary.parent, &source->parent);\n",
                                  derived->parent->c_name);
            for (component_index = 0U; component_index < derived->component_count;
                 ++component_index) {
                Symbol *component = &derived->components[component_index];
                const char *name = f2c_symbol_c_name(unit, component);
                if (component->allocatable) {
                    char *count = component_count(unit, component, "source");
                    f2c_buffer_printf(&context->output, "    if (source->%s != NULL) {\n", name);
                    for (size_t dimension = 0U; dimension < component->rank; ++dimension)
                        f2c_buffer_printf(&context->output,
                                          "        temporary.%s_lower_%zu = "
                                          "source->%s_lower_%zu; temporary.%s_extent_%zu = "
                                          "source->%s_extent_%zu;\n",
                                          name, dimension + 1U, name, dimension + 1U, name,
                                          dimension + 1U, name, dimension + 1U);
                    if (component->deferred_character)
                        f2c_buffer_printf(&context->output,
                                          "        temporary.%s_character_length = "
                                          "source->%s_character_length;\n",
                                          name, name);
                    f2c_buffer_printf(
                        &context->output,
                        "        const size_t count = %s; if (count > SIZE_MAX / sizeof(%s)) "
                        "abort(); temporary.%s = (%s *)calloc(count == 0U ? 1U : count, "
                        "sizeof(%s)); if (temporary.%s == NULL) abort();\n",
                        count != NULL ? count : "0U", f2c_symbol_c_type(component), name,
                        f2c_symbol_c_type(component), f2c_symbol_c_type(component), name);
                    if (component->type == TYPE_DERIVED && component->derived_type != NULL)
                        f2c_buffer_printf(&context->output,
                                          "        for (size_t i = 0U; i < count; ++i) "
                                          "f2c_clone_%s(&temporary.%s[i], &source->%s[i]);\n",
                                          component->derived_type->c_name, name, name);
                    else
                        f2c_buffer_printf(&context->output,
                                          "        if (count != 0U) memmove(temporary.%s, "
                                          "source->%s, count * sizeof(%s));\n",
                                          name, name, f2c_symbol_c_type(component));
                    f2c_buffer_append(&context->output, "    }\n");
                    free(count);
                } else if (component->pointer) {
                    f2c_buffer_printf(&context->output, "    temporary.%s = source->%s;\n", name,
                                      name);
                    for (size_t dimension = 0U; dimension < component->rank; ++dimension)
                        f2c_buffer_printf(&context->output,
                                          "    temporary.%s_lower_%zu = source->%s_lower_%zu; "
                                          "temporary.%s_extent_%zu = source->%s_extent_%zu;\n",
                                          name, dimension + 1U, name, dimension + 1U, name,
                                          dimension + 1U, name, dimension + 1U);
                } else if (component->type == TYPE_DERIVED && component->derived_type != NULL &&
                           component->rank == 0U) {
                    f2c_buffer_printf(&context->output,
                                      "    f2c_clone_%s(&temporary.%s, &source->%s);\n",
                                      component->derived_type->c_name, name, name);
                } else if (component->rank != 0U || component->type == TYPE_CHARACTER) {
                    f2c_buffer_printf(&context->output,
                                      "    memmove(&temporary.%s, &source->%s, "
                                      "sizeof(temporary.%s));\n",
                                      name, name, name);
                } else {
                    f2c_buffer_printf(&context->output, "    temporary.%s = source->%s;\n", name,
                                      name);
                }
            }
            f2c_buffer_printf(&context->output,
                              "    f2c_initialize_%s(&temporary);\n"
                              "    *target = temporary;\n}\n"
                              "static F2C_UNUSED void f2c_copy_%s(%s *target, const %s *source) "
                              "{\n    %s temporary;\n"
                              "    f2c_clone_%s(&temporary, source);\n"
                              "    f2c_destroy_%s(target);\n"
                              "    *target = temporary;\n}\n\n",
                              derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                              derived->c_name, derived->c_name, derived->c_name);
        }
    }
}

static void emit_dynamic_destroy_cases(Context *context, Units *units,
                                       F2cDerivedType *declared_type) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *candidate = &unit->derived_types[type_index];
            if (!type_extends(candidate, declared_type))
                continue;
            f2c_buffer_printf(&context->output,
                              "    case F2C_TYPE_ID_%s: if (value->f2c_dynamic_size != "
                              "sizeof(%s)) abort(); f2c_destroy_array_%s((%s *)(void *)value, "
                              "count, rank); return;\n",
                              candidate->c_name, candidate->c_name, candidate->c_name,
                              candidate->c_name);
        }
    }
}

static void emit_dynamic_initialize_cases(Context *context, Units *units,
                                          F2cDerivedType *declared_type) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *candidate = &unit->derived_types[type_index];
            if (!type_extends(candidate, declared_type))
                continue;
            f2c_buffer_printf(&context->output,
                              "    case F2C_TYPE_ID_%s: { %s *objects = (%s *)(void *)value; "
                              "for (size_t i = 0U; i < count; ++i) "
                              "f2c_initialize_%s(&objects[i]); return; }\n",
                              candidate->c_name, candidate->c_name, candidate->c_name,
                              candidate->c_name);
        }
    }
}

static void emit_dynamic_destroy_definitions(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_destroy_dynamic_%s(%s *value, size_t "
                              "count, size_t rank) {\n"
                              "    if (value == NULL || count == 0U) return;\n"
                              "    switch (value->f2c_type_tag) {\n",
                              derived->c_name, derived->c_name);
            emit_dynamic_destroy_cases(context, &context->modules, derived);
            emit_dynamic_destroy_cases(context, &context->units, derived);
            f2c_buffer_append(&context->output, "    default: abort();\n    }\n}\n");
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_initialize_dynamic_%s(%s *value, "
                              "size_t count) {\n"
                              "    if (value == NULL || count == 0U) return;\n"
                              "    switch (value->f2c_type_tag) {\n",
                              derived->c_name, derived->c_name);
            emit_dynamic_initialize_cases(context, &context->modules, derived);
            emit_dynamic_initialize_cases(context, &context->units, derived);
            f2c_buffer_append(&context->output, "    default: abort();\n    }\n}\n");
        }
    }
}

void f2c_emit_derived_types(Context *context) {
    uint64_t next_identifier = UINT64_C(1);
    emit_type_identifiers(context, &context->modules, &next_identifier);
    emit_type_identifiers(context, &context->units, &next_identifier);
    f2c_buffer_append(&context->output, "\n");
    emit_unit_type_declarations(context, &context->modules);
    emit_unit_type_declarations(context, &context->units);
    f2c_buffer_append(&context->output, "\n");
    emit_unit_types(context, &context->modules);
    emit_unit_types(context, &context->units);
    emit_lifecycle_prototypes(context, &context->modules);
    emit_lifecycle_prototypes(context, &context->units);
    f2c_buffer_append(&context->output, "\n");
    emit_dispatch_wrappers(context, &context->modules);
    emit_dispatch_wrappers(context, &context->units);
    f2c_buffer_append(&context->output, "\n");
    emit_lifecycle_definitions(context, &context->modules);
    emit_lifecycle_definitions(context, &context->units);
    emit_dynamic_destroy_definitions(context, &context->modules);
    emit_dynamic_destroy_definitions(context, &context->units);
}
