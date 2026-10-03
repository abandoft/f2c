#include "codegen/character/private.h"

#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    for (int level = 0; level < depth; ++level)
        f2c_buffer_append(output, "    ");
}

int f2c_emit_deferred_character_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                           const F2cExpr *right, const char *right_code,
                                           int depth) {
    const F2cStorageReference reference = f2c_ir_storage_reference(left);
    const Symbol *symbol = reference.symbol;
    Buffer binding = {0};
    Buffer transaction = {0};
    char *owner = NULL;
    char *data = NULL;
    char *length = NULL;
    char *source = NULL;
    int success = 0;
    if (context == NULL || unit == NULL || symbol == NULL || right == NULL || right_code == NULL ||
        symbol->type != TYPE_CHARACTER || !symbol->allocatable || !symbol->deferred_character ||
        symbol->rank != 0U || !left->definable ||
        (left->kind != F2C_EXPR_NAME && left->kind != F2C_EXPR_COMPONENT))
        return 0;
    indent(&transaction, depth);
    f2c_buffer_append(&transaction, "{\n");
    if (reference.owner != NULL) {
        int supported = 0;
        if (reference.owner->derived_type == NULL)
            goto done;
        owner = f2c_expression_storage_designator(unit, reference.owner, &supported);
        if (!supported || owner == NULL)
            goto done;
        indent(&transaction, depth + 1);
        f2c_buffer_printf(&transaction, "%s *f2c_deferred_owner = &(%s);\n",
                          reference.owner->derived_type->c_name, owner);
        f2c_buffer_printf(&binding, "f2c_deferred_owner->%s",
                          symbol->c_name != NULL ? symbol->c_name : symbol->name);
    }
    if (binding.failed)
        goto done;
    data = f2c_storage_bound_property(unit, &reference, F2C_OBJECT_DATA, 0U, binding.data, 1);
    length = f2c_character_length_expression(unit, right);
    source = f2c_character_source_pointer(unit, right, right_code);
    if (data == NULL || length == NULL || source == NULL)
        goto done;
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "const size_t f2c_deferred_length = (size_t)(%s);\n", length);
    indent(&transaction, depth + 1);
    f2c_buffer_append(&transaction, "if (f2c_deferred_length == SIZE_MAX) abort();\n");
    indent(&transaction, depth + 1);
    f2c_buffer_append(&transaction,
                      "char *f2c_deferred_value = (char *)malloc(f2c_deferred_length + 1U);\n");
    indent(&transaction, depth + 1);
    f2c_buffer_append(&transaction, "if (f2c_deferred_value == NULL) abort();\n");
    const int qualified = (right->storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U;
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "const %schar *f2c_deferred_source = (%s);\n",
                      qualified ? "volatile " : "", source);
    indent(&transaction, depth + 1);
    if (qualified)
        f2c_buffer_append(&transaction,
                          "f2c_character_copy_volatile(f2c_deferred_value, f2c_deferred_length, "
                          "f2c_deferred_source, f2c_deferred_length);\n");
    else
        f2c_buffer_append(&transaction,
                          "if (f2c_deferred_length != 0U) memmove(f2c_deferred_value, "
                          "f2c_deferred_source, f2c_deferred_length);\n");
    indent(&transaction, depth + 1);
    f2c_buffer_append(&transaction, "f2c_deferred_value[f2c_deferred_length] = '\\0';\n");
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "free(%s);\n", data);
    if (!f2c_storage_emit_store(&transaction, unit, &reference, F2C_OBJECT_DATA, 0U, binding.data,
                                "f2c_deferred_value", depth + 1) ||
        !f2c_storage_emit_store(&transaction, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH, 0U,
                                binding.data, "f2c_deferred_length", depth + 1))
        goto done;
    indent(&transaction, depth);
    f2c_buffer_append(&transaction, "}\n");
    if (transaction.failed)
        goto done;
    f2c_buffer_append(&context->output, transaction.data);
    success = !context->output.failed;
done:
    free(owner);
    free(data);
    free(length);
    free(source);
    free(f2c_buffer_take(&binding));
    free(f2c_buffer_take(&transaction));
    return success;
}
