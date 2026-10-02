#include "codegen/type/initialization.h"

#include "codegen/array/static_shape.h"
#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static Unit *type_scope(Unit *caller, const F2cDerivedType *derived) {
    Units *collections[] = {&caller->context->modules, &caller->context->units};
    for (size_t collection = 0U; collection < 2U; ++collection) {
        for (size_t index = 0U; index < collections[collection]->count; ++index) {
            Unit *unit = &collections[collection]->items[index];
            for (size_t type = 0U; type < unit->derived_type_count; ++type)
                if (&unit->derived_types[type] == derived)
                    return unit;
        }
    }
    return caller;
}

static const F2cTypeBinding *effective_binding(const F2cDerivedType *derived, const char *name) {
    for (; derived != NULL; derived = derived->parent)
        for (size_t index = 0U; index < derived->binding_count; ++index)
            if (strcmp(derived->bindings[index].name, name) == 0)
                return &derived->bindings[index];
    return NULL;
}

static int binding_has_definition(Unit *unit, const F2cTypeBinding *binding) {
    if (binding == NULL || binding->deferred || binding->target_name == NULL)
        return 0;
    for (size_t index = 0U; index < unit->context->units.count; ++index) {
        const Unit *definition = &unit->context->units.items[index];
        const char *name =
            definition->fortran_name != NULL ? definition->fortran_name : definition->name;
        if (name != NULL && strcmp(name, binding->target_name) == 0)
            return 1;
    }
    return 0;
}

static char *aggregate(Unit *caller, const F2cDerivedType *derived, const F2cDerivedType *dynamic,
                       const F2cExpr *constructor);
static char *component_initializer(Unit *caller, Unit *scope, const Symbol *component,
                                   const F2cExpr *actual);

static char *broadcast_initializer(Unit *caller, Unit *scope, const Symbol *scalar,
                                   const F2cExpr *actual, size_t count) {
    Buffer output = {.limit = caller->context->output.limit};
    char *item;
    size_t length;
    size_t index;
    if (count == 0U)
        return f2c_strdup("{0}");
    item = component_initializer(caller, scope, scalar, actual);
    if (item == NULL)
        return NULL;
    length = strlen(item);
    /* Braces and separators make the exact expansion size count * (length + 2). */
    if (length > SIZE_MAX - 2U || count > SIZE_MAX / (length + 2U) ||
        (output.limit != 0U && count > output.limit / (length + 2U))) {
        caller->context->output.limit_exceeded = 1;
        free(item);
        return NULL;
    }
    if (!f2c_reserve_constant_steps(caller, count)) {
        free(item);
        return NULL;
    }
    f2c_buffer_append(&output, "{");
    for (index = 0U; index < count && !output.failed; ++index) {
        if (index != 0U)
            f2c_buffer_append(&output, ", ");
        f2c_buffer_append_n(&output, item, length);
    }
    f2c_buffer_append(&output, "}");
    free(item);
    if (output.limit_exceeded)
        caller->context->output.limit_exceeded = 1;
    return f2c_buffer_take(&output);
}

static char *component_initializer(Unit *caller, Unit *scope, const Symbol *component,
                                   const F2cExpr *actual) {
    Symbol value = *component;
    const F2cExpr *expression =
        f2c_expr_value_source(actual != NULL ? actual : component->initializer_expression);
    char *initializer;
    if (component->allocatable || component->pointer || component->procedure_pointer) {
        if (expression == NULL ||
            (expression->kind == F2C_EXPR_CALL && expression->intrinsic == F2C_INTRINSIC_NULL))
            return f2c_strdup("NULL");
        return NULL;
    }
    if (actual != NULL) {
        value.initializer_expression = (F2cExpr *)actual;
        value.initializer = (char *)"";
        scope = caller;
    }
    if (component->rank != 0U) {
        Buffer output = {.limit = caller->context->output.limit};
        size_t count;
        if (!f2c_static_array_element_count(scope, component, &count))
            return NULL;
        if (component->type == TYPE_CHARACTER) {
            int64_t length;
            if (expression == NULL)
                return f2c_strdup("{0}");
            if (!f2c_character_declaration_length(scope, component, &length))
                return NULL;
            if (output.limit != 0U && length > 0 &&
                ((uint64_t)length > output.limit || count > output.limit / (size_t)length)) {
                caller->context->output.limit_exceeded = 1;
                return NULL;
            }
            return f2c_unit_static_storage_initializer(scope, &value);
        }
        /* Even the shortest emitted scalar occupies one byte. Reject an
         * impossible expansion before iterating its declared element count. */
        if (output.limit != 0U && count > output.limit) {
            caller->context->output.limit_exceeded = 1;
            return NULL;
        }
        if (expression != NULL && expression->rank != 0U &&
            expression->kind != F2C_EXPR_ARRAY_CONSTRUCTOR)
            return NULL;
        if (expression != NULL && expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR &&
            expression->child_count != count)
            return NULL;
        value.rank = 0U;
        if (expression == NULL || expression->rank == 0U)
            return broadcast_initializer(caller, scope, &value, actual, count);
        f2c_buffer_append(&output, "{");
        for (size_t element = 0U; element < count; ++element) {
            const F2cExpr *item =
                expression != NULL && expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR
                    ? expression->children[element]
                    : expression;
            value.initializer_expression = (F2cExpr *)item;
            initializer =
                component_initializer(caller, scope, &value, actual != NULL ? item : NULL);
            if (initializer == NULL) {
                free(output.data);
                return NULL;
            }
            f2c_buffer_printf(&output, "%s%s", element == 0U ? "" : ", ", initializer);
            free(initializer);
            if (output.failed || output.limit_exceeded) {
                if (output.limit_exceeded)
                    caller->context->output.limit_exceeded = 1;
                free(output.data);
                return NULL;
            }
        }
        f2c_buffer_append(&output, count == 0U ? "0}" : "}");
        return f2c_buffer_take(&output);
    }
    if (component->type == TYPE_DERIVED) {
        if (component->derived_type == NULL)
            return NULL;
        if (expression == NULL || expression->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR)
            return aggregate(scope, component->derived_type, component->derived_type, expression);
        return actual != NULL ? f2c_emit_typed_expression(caller, actual) : NULL;
    }
    if (expression == NULL)
        return f2c_strdup(component->type == TYPE_CHARACTER ? "{0}" : "0");
    initializer = f2c_unit_static_storage_initializer(scope, &value);
    if (initializer == NULL && actual != NULL && component->type != TYPE_CHARACTER) {
        char *code = f2c_emit_typed_expression(caller, actual);
        if (code != NULL)
            initializer = f2c_emit_numeric_conversion_as(code, actual->type, component->type,
                                                         f2c_symbol_c_type(component));
        free(code);
    }
    return initializer;
}

static char *aggregate(Unit *caller, const F2cDerivedType *derived, const F2cDerivedType *dynamic,
                       const F2cExpr *constructor) {
    Buffer output = {.limit = caller != NULL ? caller->context->output.limit : 0U};
    Unit *scope;
    const F2cExpr **actuals;
    size_t next = 0U;
    if (caller == NULL || derived == NULL || dynamic == NULL)
        return NULL;
    scope = type_scope(caller, derived);
    actuals = derived->component_count != 0U
                  ? (const F2cExpr **)calloc(derived->component_count, sizeof(*actuals))
                  : NULL;
    if (derived->component_count != 0U && actuals == NULL)
        return NULL;
    for (size_t argument = 0U; constructor != NULL && argument < constructor->child_count;
         ++argument) {
        const F2cExpr *actual = constructor->children[argument];
        size_t component = next++;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U) {
            component = derived->component_count;
            for (size_t index = 0U; index < derived->component_count; ++index)
                if (actual->text != NULL &&
                    strcmp(actual->text, derived->components[index].name) == 0)
                    component = index;
            actual = actual->children[0];
            --next;
        }
        if (component >= derived->component_count || actual == NULL || actuals[component] != NULL)
            goto failed;
        actuals[component] = actual;
    }
    f2c_buffer_printf(&output, "{.f2c_type_tag = F2C_TYPE_ID_%s, .f2c_dynamic_size = sizeof(%s)",
                      dynamic->c_name, dynamic->c_name);
    if (derived->parent != NULL) {
        char *parent = aggregate(caller, derived->parent, dynamic, NULL);
        if (parent == NULL)
            goto failed;
        f2c_buffer_printf(&output, ", .parent = %s", parent);
        free(parent);
    }
    for (size_t component = 0U; component < derived->component_count; ++component) {
        const Symbol *symbol = &derived->components[component];
        char *initializer = component_initializer(caller, scope, symbol, actuals[component]);
        if (initializer == NULL)
            goto failed;
        f2c_buffer_printf(&output, ", .%s = %s", f2c_symbol_c_name(scope, symbol), initializer);
        free(initializer);
        if (output.failed || output.limit_exceeded)
            goto failed;
    }
    for (size_t index = 0U; index < derived->binding_count; ++index) {
        const F2cTypeBinding *storage = &derived->bindings[index];
        const F2cTypeBinding *effective;
        if (storage->overridden != NULL)
            continue;
        effective = effective_binding(dynamic, storage->name);
        f2c_buffer_printf(&output, ", .%s = ", storage->name);
        if (binding_has_definition(caller, effective))
            f2c_buffer_printf(&output, "f2c_dispatch_%s_%s", dynamic->c_name, storage->name);
        else
            f2c_buffer_append(&output, "NULL");
    }
    f2c_buffer_append(&output, "}");
    free(actuals);
    return f2c_buffer_take(&output);
failed:
    if (output.limit_exceeded)
        caller->context->output.limit_exceeded = 1;
    free(actuals);
    free(output.data);
    return NULL;
}

char *f2c_derived_storage_initializer(Unit *unit, const F2cDerivedType *derived) {
    return aggregate(unit, derived, derived, NULL);
}

char *f2c_derived_entity_initializer(Unit *unit, const Symbol *symbol) {
    if (unit == NULL || symbol == NULL || symbol->type != TYPE_DERIVED)
        return NULL;
    return component_initializer(unit, unit, symbol, symbol->initializer_expression);
}

char *f2c_derived_constructor_initializer(Unit *unit, const F2cExpr *constructor) {
    constructor = f2c_expr_value_source(constructor);
    if (constructor == NULL || constructor->kind != F2C_EXPR_STRUCTURE_CONSTRUCTOR)
        return NULL;
    return aggregate(unit, constructor->derived_type, constructor->derived_type, constructor);
}
