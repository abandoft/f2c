#include "codegen/allocation/context.h"
#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

char *f2c_allocation_target_storage(Unit *unit, const F2cExpr *target, Buffer *prelude) {
    Buffer result = {0};
    char *base;
    if (target == NULL || target->symbol == NULL)
        return NULL;
    if (target->kind == F2C_EXPR_NAME || target->kind == F2C_EXPR_ARRAY_REFERENCE)
        return f2c_strdup(f2c_symbol_c_name(unit, target->symbol));
    if (target->kind != F2C_EXPR_COMPONENT || target->child_count == 0U || prelude == NULL ||
        target->children[0] == NULL || target->children[0]->derived_type == NULL)
        return NULL;
    int supported = 0;
    base = f2c_expression_storage_designator(unit, target->children[0], &supported);
    if (!supported) {
        free(base);
        return NULL;
    }
    if (base == NULL)
        return NULL;
    f2c_buffer_printf(prelude, "%s *f2c_allocation_object = &(%s);\n",
                      target->children[0]->derived_type->c_name, base);
    f2c_buffer_printf(&result, "f2c_allocation_object->%s",
                      f2c_symbol_c_name(unit, target->symbol));
    free(base);
    return f2c_buffer_take(&result);
}

void f2c_allocation_store(Buffer *output, Unit *unit, const F2cStorageReference *reference,
                          F2cObjectStateProperty property, size_t dimension, const char *binding,
                          const char *value, int depth) {
    if (!f2c_storage_emit_store(output, unit, reference, property, dimension, binding, value,
                                depth))
        output->failed = 1;
}
