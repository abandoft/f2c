#include "codegen/allocation/context.h"
#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static int is_allocation_option(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT;
}

int f2c_emit_deallocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth) {
    F2cAllocationContext allocation = {0};
    char *target_name = NULL;
    char *target_binding = NULL;
    size_t i;
    if (!f2c_allocation_context_begin(&allocation, context, unit, statement, depth))
        return 0;
    ++depth;
    for (i = 0U; i < statement->item_count; ++i) {
        F2cExpr *target = allocation.arguments[i];
        Symbol *symbol;
        Buffer target_prelude = {0};
        size_t d;
        if (is_allocation_option(target))
            continue;
        symbol = target != NULL ? target->symbol : NULL;
        if (symbol == NULL || (!symbol->allocatable && !symbol->pointer))
            continue;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        if (!f2c_allocation_prepare_value(&allocation, target, 1, depth + 1)) {
            f2c_allocation_context_clear(&allocation);
            return 0;
        }
        const F2cStorageReference reference = f2c_ir_storage_reference(target);
        target_binding = f2c_allocation_target_storage(unit, target, &target_prelude);
        target_name =
            f2c_storage_bound_property(unit, &reference, F2C_OBJECT_DATA, 0U, target_binding, 1);
        if (target_binding == NULL || target_name == NULL) {
            free(target_name);
            free(target_binding);
            free(target_prelude.data);
            f2c_allocation_context_clear(&allocation);
            return 0;
        }
        if (target_prelude.data != NULL) {
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, target_prelude.data);
        }
        free(target_prelude.data);
        indent(&context->output, depth + 1);
        if (symbol->pointer) {
            char *deallocatable = f2c_storage_bound_property(
                unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U, target_binding, 0);
            f2c_buffer_printf(&context->output,
                              "const bool f2c_dealloc_ok = %s != NULL && "
                              "%s;\n",
                              target_name, deallocatable != NULL ? deallocatable : "false");
            if (deallocatable == NULL)
                context->output.failed = 1;
            free(deallocatable);
        } else
            f2c_buffer_printf(&context->output, "const bool f2c_dealloc_ok = %s != NULL;\n",
                              target_name);
        indent(&context->output, depth + 1);
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            Buffer count = {0};
            for (d = 0U; d < symbol->rank; ++d) {
                char *extent = f2c_storage_bound_property(unit, &reference, F2C_OBJECT_EXTENT, d,
                                                          target_binding, 0);
                if (extent == NULL)
                    context->output.failed = 1;
                else
                    f2c_buffer_printf(&count, "%s(size_t)(%s)", d == 0U ? "" : " * ", extent);
                free(extent);
            }
            f2c_buffer_printf(&context->output,
                              "if (f2c_dealloc_ok) f2c_destroy_array_%s(%s, (size_t)(%s), "
                              "%zuU);\n",
                              symbol->derived_type->c_name, target_name,
                              count.data != NULL ? count.data : "1U", symbol->rank);
            indent(&context->output, depth + 1);
            free(count.data);
        }
        f2c_buffer_printf(&context->output, "if (f2c_dealloc_ok) { free(%s); %s = NULL; }\n",
                          target_name, target_name);
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_dealloc_ok) {\n");
        if (symbol->pointer || reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR)
            f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U,
                                 target_binding, "false", depth + 2);
        if (symbol->deferred_character) {
            f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH,
                                 0U, target_binding, "0U", depth + 2);
        }
        for (d = 0U; d < symbol->rank; ++d) {
            f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_LOWER, d,
                                 target_binding, "1", depth + 2);
            f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_EXTENT, d,
                                 target_binding, "0", depth + 2);
            if (symbol->pointer || reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR)
                f2c_allocation_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, d,
                                     target_binding, symbol->pointer || d != 0U ? "0" : "1",
                                     depth + 2);
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        f2c_allocation_failure(&allocation, "f2c_dealloc_ok", "object is not deallocatable",
                               depth + 1);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(target_name);
        free(target_binding);
    }
    const int emitted = f2c_allocation_context_finish(&allocation);
    f2c_allocation_context_clear(&allocation);
    return emitted;
}
