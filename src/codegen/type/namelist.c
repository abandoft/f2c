#include "internal/f2c.h"

#include <stdlib.h>

static char *component_count(Unit *unit, const Symbol *component, const char *object) {
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

static char *component_character_length(Unit *unit, const Symbol *component, const char *object) {
    Buffer result = {0};
    if (component->deferred_character) {
        f2c_buffer_printf(&result, "(size_t)%s->%s_character_length", object,
                          f2c_symbol_c_name(unit, component));
        return f2c_buffer_take(&result);
    }
    return f2c_symbol_character_length(unit, component);
}

static void emit_type_prototypes(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            const F2cDerivedType *derived = &unit->derived_types[type_index];
            f2c_buffer_printf(
                &context->output,
                "static F2C_UNUSED bool f2c_namelist_stage_fields_%s(%s *stage, const %s "
                "*original, f2c_namelist_transaction *transaction);\n"
                "static F2C_UNUSED void f2c_namelist_rebind_fields_%s(%s *stage, const %s "
                "*original);\n"
                "static F2C_UNUSED void f2c_namelist_rebind_%s(void *stage, const void "
                "*original, size_t count, size_t rank);\n"
                "static F2C_UNUSED void f2c_namelist_commit_%s(void *original, const void "
                "*stage, size_t count, size_t rank);\n"
                "static F2C_UNUSED void f2c_namelist_destroy_%s(void *stage, size_t count, "
                "size_t rank);\n",
                derived->c_name, derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                derived->c_name);
        }
    }
}

void f2c_emit_namelist_type_prototypes(Context *context) {
    emit_type_prototypes(context, &context->modules);
    emit_type_prototypes(context, &context->units);
}

static void emit_stage_pointer(Context *context, Unit *unit, const Symbol *component) {
    const char *name = f2c_symbol_c_name(unit, component);
    const char *type = f2c_symbol_c_type(component);
    char *count = component_count(unit, component, "original");
    char *character_length = component->type == TYPE_CHARACTER
                                 ? component_character_length(unit, component, "original")
                                 : NULL;
    f2c_buffer_printf(&context->output,
                      "    if (original->%s != NULL) {\n"
                      "        void *mapped = f2c_namelist_transaction_find(transaction, "
                      "original->%s);\n"
                      "        if (mapped != NULL) { stage->%s = (%s *)mapped; } else {\n"
                      "            const size_t count = (size_t)(%s);\n",
                      name, name, name, type, count != NULL ? count : "0U");
    if (component->type == TYPE_CHARACTER) {
        f2c_buffer_printf(&context->output,
                          "            size_t bytes = 0U; if "
                          "(!f2c_namelist_size_product(count, (size_t)(%s), &bytes)) return "
                          "false;\n"
                          "            %s *copy = (%s *)malloc(bytes == 0U ? 1U : bytes);\n"
                          "            if (copy == NULL) return false; if (bytes != 0U) "
                          "memmove(copy, original->%s, bytes);\n"
                          "            if (!f2c_namelist_transaction_own(transaction, "
                          "original->%s, copy, bytes, sizeof(*copy), %zuU, NULL, NULL, NULL)) "
                          "{ free(copy); return false; }\n",
                          character_length != NULL ? character_length : "0U", type, type, name,
                          name, component->rank);
    } else if (component->type == TYPE_DERIVED && component->derived_type != NULL) {
        const char *derived = component->derived_type->c_name;
        f2c_buffer_printf(&context->output,
                          "            if (count > SIZE_MAX / sizeof(%s)) return false;\n"
                          "            %s *copy = (%s *)calloc(count == 0U ? 1U : count, "
                          "sizeof(*copy));\n"
                          "            if (copy == NULL) return false;\n"
                          "            for (size_t i = 0U; i < count; ++i) "
                          "f2c_clone_%s(&copy[i], &original->%s[i]);\n"
                          "            if (!f2c_namelist_transaction_own(transaction, "
                          "original->%s, copy, count, sizeof(*copy), %zuU, "
                          "f2c_namelist_rebind_%s, f2c_namelist_commit_%s, "
                          "f2c_namelist_destroy_%s)) { f2c_destroy_array_%s(copy, count, %zuU); "
                          "free(copy); return false; }\n"
                          "            for (size_t i = 0U; i < count; ++i) if "
                          "(!f2c_namelist_transaction_bind(transaction, &original->%s[i], "
                          "&copy[i])) return false;\n"
                          "            for (size_t i = 0U; i < count; ++i) if "
                          "(!f2c_namelist_stage_fields_%s(&copy[i], &original->%s[i], "
                          "transaction)) return false;\n",
                          type, type, type, derived, name, name, component->rank, derived, derived,
                          derived, derived, component->rank, name, derived, name);
    } else {
        f2c_buffer_printf(&context->output,
                          "            if (count > SIZE_MAX / sizeof(%s)) return false;\n"
                          "            %s *copy = (%s *)malloc(count == 0U ? 1U : count * "
                          "sizeof(*copy));\n"
                          "            if (copy == NULL) return false; if (count != 0U) "
                          "memmove(copy, original->%s, count * sizeof(*copy));\n"
                          "            if (!f2c_namelist_transaction_own(transaction, "
                          "original->%s, copy, count, sizeof(*copy), %zuU, NULL, NULL, NULL)) "
                          "{ free(copy); return false; }\n",
                          type, type, type, name, name, component->rank);
    }
    f2c_buffer_printf(&context->output,
                      "            stage->%s = copy;\n"
                      "        }\n"
                      "    }\n",
                      name);
    free(count);
    free(character_length);
}

static void emit_stage_owned_derived(Context *context, Unit *unit, const Symbol *component) {
    const char *name = f2c_symbol_c_name(unit, component);
    const char *derived = component->derived_type->c_name;
    char *count = component_count(unit, component, "original");
    if (component->rank == 0U) {
        f2c_buffer_printf(&context->output,
                          "    if (!f2c_namelist_stage_fields_%s(&stage->%s, &original->%s, "
                          "transaction)) return false;\n",
                          derived, name, name);
    } else if (component->allocatable) {
        f2c_buffer_printf(&context->output,
                          "    if (original->%s != NULL && stage->%s != NULL) for (size_t i = "
                          "0U; i < (size_t)(%s); ++i) if (!f2c_namelist_stage_fields_%s("
                          "&stage->%s[i], &original->%s[i], transaction)) return false;\n",
                          name, name, count != NULL ? count : "0U", derived, name, name);
    } else {
        f2c_buffer_printf(&context->output,
                          "    for (size_t i = 0U; i < (size_t)(%s); ++i) if "
                          "(!f2c_namelist_stage_fields_%s(&stage->%s[i], &original->%s[i], "
                          "transaction)) return false;\n",
                          count != NULL ? count : "0U", derived, name, name);
    }
    free(count);
}

static void emit_stage_definition(Context *context, Unit *unit, F2cDerivedType *derived) {
    size_t component_index;
    f2c_buffer_printf(&context->output,
                      "static F2C_UNUSED bool f2c_namelist_stage_fields_%s(%s *stage, const %s "
                      "*original, f2c_namelist_transaction *transaction) {\n"
                      "    if (!f2c_namelist_transaction_bind(transaction, original, stage)) "
                      "return false;\n",
                      derived->c_name, derived->c_name, derived->c_name);
    if (derived->parent != NULL)
        f2c_buffer_printf(&context->output,
                          "    if (!f2c_namelist_stage_fields_%s(&stage->parent, "
                          "&original->parent, transaction)) return false;\n",
                          derived->parent->c_name);
    for (component_index = 0U; component_index < derived->component_count; ++component_index) {
        const Symbol *component = &derived->components[component_index];
        if (component->procedure_pointer)
            continue;
        if (component->pointer) {
            emit_stage_pointer(context, unit, component);
        } else if (component->type == TYPE_DERIVED && component->derived_type != NULL) {
            emit_stage_owned_derived(context, unit, component);
        }
    }
    f2c_buffer_append(&context->output, "    return true;\n}\n");
}

static void emit_rebind_owned_derived(Context *context, Unit *unit, const Symbol *component) {
    const char *name = f2c_symbol_c_name(unit, component);
    const char *derived = component->derived_type->c_name;
    char *count = component_count(unit, component, "original");
    if (component->rank == 0U) {
        f2c_buffer_printf(&context->output,
                          "    f2c_namelist_rebind_fields_%s(&stage->%s, &original->%s);\n",
                          derived, name, name);
    } else if (component->allocatable) {
        f2c_buffer_printf(&context->output,
                          "    if (original->%s != NULL && stage->%s != NULL) for (size_t i = "
                          "0U; i < (size_t)(%s); ++i) f2c_namelist_rebind_fields_%s("
                          "&stage->%s[i], &original->%s[i]);\n",
                          name, name, count != NULL ? count : "0U", derived, name, name);
    } else {
        f2c_buffer_printf(&context->output,
                          "    for (size_t i = 0U; i < (size_t)(%s); ++i) "
                          "f2c_namelist_rebind_fields_%s(&stage->%s[i], &original->%s[i]);\n",
                          count != NULL ? count : "0U", derived, name, name);
    }
    free(count);
}

static void emit_rebind_definitions(Context *context, Unit *unit, F2cDerivedType *derived) {
    size_t component_index;
    f2c_buffer_printf(&context->output,
                      "static F2C_UNUSED void f2c_namelist_rebind_fields_%s(%s *stage, const %s "
                      "*original) {\n"
                      "    (void)stage; (void)original;\n",
                      derived->c_name, derived->c_name, derived->c_name);
    if (derived->parent != NULL)
        f2c_buffer_printf(&context->output,
                          "    f2c_namelist_rebind_fields_%s(&stage->parent, "
                          "&original->parent);\n",
                          derived->parent->c_name);
    for (component_index = 0U; component_index < derived->component_count; ++component_index) {
        const Symbol *component = &derived->components[component_index];
        const char *name = f2c_symbol_c_name(unit, component);
        if (component->procedure_pointer)
            continue;
        if (component->pointer) {
            f2c_buffer_printf(&context->output, "    stage->%s = original->%s;\n", name, name);
        } else if (component->type == TYPE_DERIVED && component->derived_type != NULL) {
            emit_rebind_owned_derived(context, unit, component);
        }
    }
    f2c_buffer_append(&context->output, "}\n");
    f2c_buffer_printf(&context->output,
                      "static F2C_UNUSED void f2c_namelist_rebind_%s(void *stage_value, const "
                      "void *original_value, size_t count, size_t rank) {\n"
                      "    %s *stage = (%s *)stage_value; const %s *original = (const %s "
                      "*)original_value; (void)rank;\n"
                      "    for (size_t i = 0U; i < count; ++i) "
                      "f2c_namelist_rebind_fields_%s(&stage[i], &original[i]);\n"
                      "}\n"
                      "static F2C_UNUSED void f2c_namelist_commit_%s(void *original_value, const "
                      "void *stage_value, size_t count, size_t rank) {\n"
                      "    %s *original = (%s *)original_value; const %s *stage = (const %s "
                      "*)stage_value; (void)rank;\n"
                      "    for (size_t i = 0U; i < count; ++i) f2c_copy_%s(&original[i], "
                      "&stage[i]);\n"
                      "}\n"
                      "static F2C_UNUSED void f2c_namelist_destroy_%s(void *stage_value, size_t "
                      "count, size_t rank) { f2c_destroy_array_%s((%s *)stage_value, count, "
                      "rank); }\n",
                      derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                      derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                      derived->c_name, derived->c_name, derived->c_name, derived->c_name,
                      derived->c_name, derived->c_name, derived->c_name, derived->c_name);
}

static void emit_type_definitions(Context *context, Units *units) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < units->count; ++unit_index) {
        Unit *unit = &units->items[unit_index];
        size_t type_index;
        for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
            F2cDerivedType *derived = &unit->derived_types[type_index];
            emit_stage_definition(context, unit, derived);
            emit_rebind_definitions(context, unit, derived);
        }
    }
}

void f2c_emit_namelist_type_definitions(Context *context) {
    emit_type_definitions(context, &context->modules);
    emit_type_definitions(context, &context->units);
}
