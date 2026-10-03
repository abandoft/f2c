#include "codegen/type/snapshot.h"

#include "internal/f2c.h"

#include <stdlib.h>

static int extends_type(const F2cDerivedType *type, const F2cDerivedType *ancestor) {
    for (; type != NULL; type = type->parent)
        if (type == ancestor)
            return 1;
    return 0;
}

static void emit_prototypes(Context *context, const Units *units) {
    for (size_t index = 0U; index < units->count; ++index) {
        const Unit *unit = &units->items[index];
        for (size_t type = 0U; type < unit->derived_type_count; ++type) {
            const char *name = unit->derived_types[type].c_name;
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_discard_array_%s(%s *value, "
                              "size_t count);\n"
                              "static F2C_UNUSED void f2c_discard_dynamic_%s(%s *value, "
                              "size_t count);\n",
                              name, name, name, name);
        }
    }
}

void f2c_emit_snapshot_type_prototypes(Context *context) {
    emit_prototypes(context, &context->modules);
    emit_prototypes(context, &context->units);
}

static void emit_component_discard(Context *context, Unit *unit, Symbol *component) {
    Buffer *output = &context->output;
    const char *name = f2c_symbol_c_name(unit, component);
    if (component->pointer || component->procedure_pointer)
        return;
    if (!component->allocatable &&
        (component->type != TYPE_DERIVED || component->derived_type == NULL))
        return;
    f2c_buffer_append(output, "        {\n");
    if (component->type == TYPE_DERIVED && component->derived_type != NULL) {
        f2c_buffer_append(output, "            size_t component_count = 1U;\n");
        for (size_t dimension = 0U; dimension < component->rank; ++dimension) {
            char *extent = component->allocatable
                               ? NULL
                               : f2c_symbol_dimension_extent(unit, component, dimension);
            Buffer dynamic_extent = {0};
            if (component->allocatable)
                f2c_buffer_printf(&dynamic_extent, "object->%s_extent_%zu", name, dimension + 1U);
            f2c_buffer_printf(output,
                              "            if (!f2c_size_multiply(component_count, "
                              "(size_t)(%s), &component_count)) abort();\n",
                              component->allocatable ? dynamic_extent.data
                                                     : (extent != NULL ? extent : "0U"));
            free(extent);
            free(dynamic_extent.data);
        }
        f2c_buffer_printf(output, "            f2c_discard_%s_%s(%sobject->%s, component_count);\n",
                          component->polymorphic ? "dynamic" : "array",
                          component->derived_type->c_name,
                          component->rank == 0U && !component->allocatable ? "&" : "", name);
    }
    if (component->allocatable)
        f2c_buffer_printf(output, "            free(object->%s); object->%s = NULL;\n", name, name);
    f2c_buffer_append(output, "        }\n");
}

static void emit_dynamic_cases(Context *context, const Units *units,
                               const F2cDerivedType *declared) {
    for (size_t index = 0U; index < units->count; ++index) {
        const Unit *unit = &units->items[index];
        for (size_t type = 0U; type < unit->derived_type_count; ++type) {
            const F2cDerivedType *candidate = &unit->derived_types[type];
            if (!extends_type(candidate, declared))
                continue;
            f2c_buffer_printf(&context->output,
                              "    case F2C_TYPE_ID_%s: if (value->f2c_dynamic_size != "
                              "sizeof(%s)) abort(); f2c_discard_array_%s((%s *)(void *)value, "
                              "count); return;\n",
                              candidate->c_name, candidate->c_name, candidate->c_name,
                              candidate->c_name);
        }
    }
}

static void emit_definitions(Context *context, Units *units) {
    for (size_t index = 0U; index < units->count; ++index) {
        Unit *unit = &units->items[index];
        for (size_t type = 0U; type < unit->derived_type_count; ++type) {
            F2cDerivedType *derived = &unit->derived_types[type];
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_discard_array_%s(%s *value, "
                              "size_t count) {\n"
                              "    if (value == NULL) return;\n"
                              "    for (size_t i = count; i-- > 0U;) {\n"
                              "        %s *object = &value[i]; (void)object;\n",
                              derived->c_name, derived->c_name, derived->c_name);
            for (size_t component = derived->component_count; component != 0U; --component)
                emit_component_discard(context, unit, &derived->components[component - 1U]);
            if (derived->parent != NULL)
                f2c_buffer_printf(&context->output,
                                  "        f2c_discard_array_%s(&object->parent, 1U);\n",
                                  derived->parent->c_name);
            f2c_buffer_append(&context->output, "    }\n}\n");
            f2c_buffer_printf(&context->output,
                              "static F2C_UNUSED void f2c_discard_dynamic_%s(%s *value, "
                              "size_t count) {\n"
                              "    if (value == NULL || count == 0U) return;\n"
                              "    switch (value->f2c_type_tag) {\n",
                              derived->c_name, derived->c_name);
            emit_dynamic_cases(context, &context->modules, derived);
            emit_dynamic_cases(context, &context->units, derived);
            f2c_buffer_append(&context->output, "    default: abort();\n    }\n}\n");
        }
    }
}

void f2c_emit_snapshot_type_definitions(Context *context) {
    emit_definitions(context, &context->modules);
    emit_definitions(context, &context->units);
}
