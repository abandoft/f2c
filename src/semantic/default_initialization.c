#include "semantic/default_initialization.h"

typedef struct DefaultPath {
    const F2cDerivedType *type;
    const struct DefaultPath *parent;
} DefaultPath;

static int has_default_initialization(const F2cDerivedType *type, const DefaultPath *path) {
    DefaultPath next = {type, path};
    if (type == NULL)
        return 0;
    for (const DefaultPath *prior = path; prior != NULL; prior = prior->parent)
        if (prior->type == type)
            return 0;
    if (has_default_initialization(type->parent, &next))
        return 1;
    for (size_t index = 0U; index < type->component_count; ++index) {
        const Symbol *component = &type->components[index];
        if (component->initializer != NULL || component->initializer_expression != NULL)
            return 1;
        if (!component->allocatable && !component->pointer && component->type == TYPE_DERIVED &&
            has_default_initialization(component->derived_type, &next))
            return 1;
    }
    return 0;
}

int f2c_component_may_be_omitted(const Symbol *component) {
    return component != NULL && (component->allocatable || component->initializer != NULL ||
                                 component->initializer_expression != NULL ||
                                 (!component->pointer && component->type == TYPE_DERIVED &&
                                  has_default_initialization(component->derived_type, NULL)));
}
