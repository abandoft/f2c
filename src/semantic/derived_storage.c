#include "semantic/semantic.h"

#include "internal/f2c.h"

#include <stdlib.h>

typedef struct StorageNode {
    F2cDerivedType *type;
    unsigned char state;
} StorageNode;

static int visit_storage(Context *context, StorageNode *nodes, size_t count, F2cDerivedType *type) {
    StorageNode *node = NULL;
    for (size_t index = 0U; index < count; ++index)
        if (nodes[index].type == type) {
            node = &nodes[index];
            break;
        }
    if (node == NULL || node->state == 2U)
        return 1;
    if (node->state == 1U) {
        f2c_diagnostic(context, context->lines.items[type->begin].number, 1,
                       "derived type '%s' has a recursive nonpointer, nonallocatable storage cycle",
                       type->name);
        return 0;
    }
    node->state = 1U;
    if (type->parent != NULL && !visit_storage(context, nodes, count, type->parent))
        return 0;
    for (size_t index = 0U; index < type->component_count; ++index) {
        const Symbol *component = &type->components[index];
        if (component->type == TYPE_DERIVED && !component->pointer && !component->allocatable &&
            component->derived_type != NULL &&
            !visit_storage(context, nodes, count, component->derived_type))
            return 0;
    }
    node->state = 2U;
    return 1;
}

void f2c_validate_derived_storage(Context *context) {
    Units *collections[] = {&context->modules, &context->units};
    size_t count = 0U;
    size_t next = 0U;
    StorageNode *nodes;
    for (size_t collection = 0U; collection < 2U; ++collection)
        for (size_t unit = 0U; unit < collections[collection]->count; ++unit) {
            const size_t types = collections[collection]->items[unit].derived_type_count;
            if (types > SIZE_MAX - count)
                goto exhausted;
            count += types;
        }
    if (count == 0U)
        return;
    if (count > SIZE_MAX / sizeof(*nodes))
        goto exhausted;
    nodes = (StorageNode *)calloc(count, sizeof(*nodes));
    if (nodes == NULL)
        goto exhausted;
    for (size_t collection = 0U; collection < 2U; ++collection)
        for (size_t unit = 0U; unit < collections[collection]->count; ++unit)
            for (size_t type = 0U; type < collections[collection]->items[unit].derived_type_count;
                 ++type)
                nodes[next++].type = &collections[collection]->items[unit].derived_types[type];
    for (size_t index = 0U; index < count; ++index)
        if (!visit_storage(context, nodes, count, nodes[index].type))
            break;
    free(nodes);
    return;
exhausted:
    f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                        "out of memory checking derived-type storage dependencies");
}
