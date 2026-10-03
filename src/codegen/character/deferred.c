#include "codegen/character/private.h"

#include "codegen/expression/private.h"
#include "codegen/names.h"
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
    char *owner_name = NULL;
    char *length_name = NULL;
    char *value_name = NULL;
    char *source_name = NULL;
    int success = 0;
    if (context == NULL || unit == NULL || symbol == NULL || right == NULL || right_code == NULL ||
        symbol->type != TYPE_CHARACTER || !symbol->allocatable || !symbol->deferred_character ||
        symbol->rank != 0U || !left->definable ||
        (left->kind != F2C_EXPR_NAME && left->kind != F2C_EXPR_COMPONENT))
        return 0;
    length_name = f2c_codegen_local_name(unit, "f2c_deferred_length");
    value_name = f2c_codegen_local_name(unit, "f2c_deferred_value");
    source_name = f2c_codegen_local_name(unit, "f2c_deferred_source");
    if (length_name == NULL || value_name == NULL || source_name == NULL)
        goto done;
    indent(&transaction, depth);
    f2c_buffer_append(&transaction, "{\n");
    if (reference.owner != NULL) {
        int supported = 0;
        if (reference.owner->derived_type == NULL)
            goto done;
        owner = f2c_expression_storage_designator(unit, reference.owner, &supported);
        owner_name = f2c_codegen_local_name(unit, "f2c_deferred_owner");
        if (!supported || owner == NULL || owner_name == NULL)
            goto done;
        indent(&transaction, depth + 1);
        f2c_buffer_printf(&transaction, "%s *%s = &(%s);\n", reference.owner->derived_type->c_name,
                          owner_name, owner);
        f2c_buffer_printf(&binding, "%s->%s", owner_name,
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
    f2c_buffer_printf(&transaction, "const size_t %s = (size_t)(%s);\n", length_name, length);
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "if (%s == SIZE_MAX) abort();\n", length_name);
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "char *%s = (char *)malloc(%s + 1U);\n", value_name,
                      length_name);
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "if (%s == NULL) abort();\n", value_name);
    const int qualified = (right->storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U;
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "const %schar *%s = (%s);\n", qualified ? "volatile " : "",
                      source_name, source);
    indent(&transaction, depth + 1);
    if (qualified)
        f2c_buffer_printf(&transaction, "f2c_character_copy_volatile(%s, %s, %s, %s);\n",
                          value_name, length_name, source_name, length_name);
    else
        f2c_buffer_printf(&transaction, "if (%s != 0U) memmove(%s, %s, %s);\n", length_name,
                          value_name, source_name, length_name);
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "%s[%s] = '\\0';\n", value_name, length_name);
    indent(&transaction, depth + 1);
    f2c_buffer_printf(&transaction, "free(%s);\n", data);
    if (!f2c_storage_emit_store(&transaction, unit, &reference, F2C_OBJECT_DATA, 0U, binding.data,
                                value_name, depth + 1) ||
        !f2c_storage_emit_store(&transaction, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH, 0U,
                                binding.data, length_name, depth + 1))
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
    free(owner_name);
    free(length_name);
    free(value_name);
    free(source_name);
    free(f2c_buffer_take(&binding));
    free(f2c_buffer_take(&transaction));
    return success;
}
