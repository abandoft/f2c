#include "codegen/statement/private.h"

#include "codegen/names.h"
#include "codegen/operator.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    for (int level = 0; level < depth; ++level)
        f2c_buffer_append(output, "    ");
}

int f2c_emit_allocatable_scalar_assignment(Context *context, Unit *unit,
                                           const F2cStatement *statement, const char *right,
                                           size_t line, int depth) {
    const F2cExpr *left = statement->left;
    const Symbol *symbol = left != NULL ? left->symbol : NULL;
    if (symbol == NULL || !symbol->allocatable || symbol->rank != 0U || left->rank != 0U ||
        (left->kind != F2C_EXPR_NAME && left->kind != F2C_EXPR_COMPONENT) ||
        left->type == TYPE_CHARACTER || left->type == TYPE_DERIVED)
        return 0;
    static const char *const suffixes[] = {"value", "state", "storage"};
    const F2cStorageReference reference = f2c_ir_storage_reference(left);
    char *family = f2c_codegen_local_family(unit, "f2c_scalar_assignment", suffixes,
                                            sizeof(suffixes) / sizeof(suffixes[0]));
    char *state = f2c_storage_write_property(unit, &reference, F2C_OBJECT_DATA, 0U);
    char *converted = f2c_emit_scalar_conversion(
        (F2cScalarOperand){right, f2c_expression_scalar_type(statement->right)},
        f2c_expression_scalar_type(left));
    if (family == NULL || state == NULL || converted == NULL) {
        free(family);
        free(state);
        free(converted);
        f2c_diagnostic(context, line, 1,
                       "allocatable scalar assignment could not be lowered from typed storage");
        return -1;
    }
    const char *type = f2c_c_type_kind(left->type, left->type_kind);
    const char *state_type = reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR ? "void" : type;
    const char *state_qualification =
        (reference.state_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "";
    const char *object_qualification =
        (reference.object_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "";
    Buffer *output = &context->output;
    indent(output, depth);
    f2c_buffer_append(output, "{\n");
    indent(output, depth + 1);
    /* Capture the complete RHS before changing allocation status. This also
     * keeps effectful scalar calls single-evaluated and model-converted. */
    f2c_buffer_printf(output, "const %s %s_value = %s;\n", type, family, converted);
    indent(output, depth + 1);
    /* Capture the state lvalue once, including a component owner expression.
     * Descriptor state stores void*, local/component state stores typed data. */
    f2c_buffer_printf(output, "%s *%s*const %s_state = &(%s);\n", state_type, state_qualification,
                      family, state);
    indent(output, depth + 1);
    f2c_buffer_printf(output, "if (*%s_state == NULL) {\n", family);
    indent(output, depth + 2);
    f2c_buffer_printf(output, "%s *const %s_storage = (%s *)malloc(sizeof(%s));\n", type, family,
                      type, type);
    indent(output, depth + 2);
    f2c_buffer_printf(output, "if (%s_storage == NULL) abort();\n", family);
    indent(output, depth + 2);
    f2c_buffer_printf(output, "*%s_state = %s_storage;\n", family, family);
    indent(output, depth + 1);
    f2c_buffer_append(output, "}\n");
    indent(output, depth + 1);
    f2c_buffer_printf(output, "*((%s%s *)*%s_state) = %s_value;\n", object_qualification, type,
                      family, family);
    indent(output, depth);
    f2c_buffer_append(output, "}\n");
    free(family);
    free(state);
    free(converted);
    return output->failed ? -1 : 1;
}
