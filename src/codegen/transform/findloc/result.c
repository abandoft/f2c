#include "codegen/operator.h"
#include "codegen/storage/private.h"
#include "codegen/transform/findloc/private.h"
#include "semantic/numeric_model.h"

#include <stdlib.h>

void f2c_findloc_store_position(F2cFindloc *lowering, const char *index, int depth) {
    const F2cNumericModel *model = f2c_numeric_model(TYPE_INTEGER, lowering->result_kind);
    Buffer value = {0};
    f2c_buffer_printf(&value, "((%s)%s_position)",
                      f2c_c_type_kind(TYPE_INTEGER, lowering->result_kind), lowering->prefix);
    char *converted = f2c_emit_scalar_conversion(
        (F2cScalarOperand){value.data, {TYPE_INTEGER, lowering->result_kind}},
        f2c_scalar_type(lowering->target->type, lowering->target->kind));
    if (model == NULL || converted == NULL) {
        lowering->context->output.failed = 1;
    } else {
        f2c_transform_indent(&lowering->context->output, depth);
        f2c_buffer_printf(&lowering->context->output,
                          "if ((uintmax_t)%s_position > UINT64_C(%llu)) abort();\n",
                          lowering->prefix, (unsigned long long)model->integer_huge);
        f2c_transform_indent(&lowering->context->output, depth);
        f2c_buffer_printf(&lowering->context->output, "%s_result[%s] = %s;\n", lowering->prefix,
                          index, converted);
    }
    free(value.data);
    free(converted);
}

int f2c_findloc_result_prepare(F2cFindloc *lowering, int depth) {
    Buffer *output = &lowering->context->output;
    const char *name = lowering->prefix;
    const size_t rank = lowering->result_rank;
    const char *type = f2c_symbol_c_type(lowering->target);
    f2c_transform_indent(output, depth);
    f2c_buffer_printf(output, "size_t %s_result_extents[%zu] = {0U};\n", name,
                      rank != 0U ? rank : 1U);
    f2c_transform_indent(output, depth);
    if (lowering->has_dimension)
        f2c_buffer_printf(output,
                          "for (size_t %s_axis = 0U, %s_output = 0U; %s_axis < %zuU; ++%s_axis) "
                          "if ((int64_t)(%s_axis + 1U) != %s_dimension) "
                          "%s_result_extents[%s_output++] = %s_extents[%s_axis];\n",
                          name, name, name, lowering->source.rank, name, name, name, name, name,
                          name, name);
    else
        f2c_buffer_printf(output, "%s_result_extents[0] = %zuU;\n", name, lowering->source.rank);
    f2c_transform_indent(output, depth);
    f2c_buffer_printf(output,
                      "const size_t %s_result_count = f2c_inquiry_size(%zuU, %s_result_extents);\n",
                      name, rank, name);
    if (lowering->target->allocatable)
        for (size_t axis = 0U; axis < rank; ++axis) {
            f2c_transform_indent(output, depth);
            f2c_buffer_printf(output, "if (%s_result_extents[%zu] > (size_t)INT32_MAX) abort();\n",
                              name, axis);
        }
    f2c_transform_indent(output, depth);
    if (rank == 0U) {
        f2c_buffer_printf(output, "%s %s_result[1] = {0};\n", type, name);
        return !output->failed;
    }
    f2c_buffer_printf(output, "if (%s_result_count > SIZE_MAX / sizeof(%s)) abort();\n", name,
                      type);
    f2c_transform_indent(output, depth);
    f2c_buffer_printf(output,
                      "%s *%s_result = (%s *)malloc(%s_result_count == 0U ? sizeof(%s) : "
                      "%s_result_count * sizeof(%s));\n",
                      type, name, type, name, type, name, type);
    f2c_transform_indent(output, depth);
    f2c_buffer_printf(output, "if (%s_result == NULL) abort();\n", name);
    char *zero =
        f2c_emit_scalar_conversion((F2cScalarOperand){"0", {TYPE_INTEGER, 4}},
                                   f2c_scalar_type(lowering->target->type, lowering->target->kind));
    if (zero == NULL)
        return 0;
    f2c_transform_indent(output, depth);
    f2c_buffer_printf(output,
                      "for (size_t %s_output = 0U; %s_output < %s_result_count; ++%s_output) "
                      "%s_result[%s_output] = %s;\n",
                      name, name, name, name, name, name, zero);
    free(zero);
    return !output->failed;
}

static int copy_array(F2cFindloc *lowering, int depth) {
    Buffer index = {0};
    Buffer *output = &lowering->context->output;
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(lowering->target);
    f2c_buffer_printf(&index, "%s_output", lowering->prefix);
    char *destination =
        lowering->target->equivalence_unaligned
            ? f2c_emit_unaligned_linear_address(lowering->unit, lowering->target, index.data)
            : f2c_storage_linear_element(lowering->unit, &reference, index.data);
    if (destination == NULL) {
        free(index.data);
        return 0;
    }
    f2c_transform_indent(output, depth);
    f2c_buffer_printf(output, "for (size_t %s = 0U; %s < %s_result_count; ++%s) ", index.data,
                      index.data, lowering->prefix, index.data);
    if (lowering->target->equivalence_unaligned)
        f2c_buffer_printf(output, "f2c_unaligned_store_%s(%s, %s_result[%s]);\n",
                          f2c_unaligned_access_suffix(lowering->target), destination,
                          lowering->prefix, index.data);
    else
        f2c_buffer_printf(output, "%s = %s_result[%s];\n", destination, lowering->prefix,
                          index.data);
    free(destination);
    free(index.data);
    return !output->failed;
}

static int commit_scalar(F2cFindloc *lowering, int depth) {
    Symbol *target = lowering->target;
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(target);
    if (target->allocatable) {
        Buffer *output = &lowering->context->output;
        char *data = f2c_storage_read_property(lowering->unit, &reference, F2C_OBJECT_DATA, 0U);
        if (data == NULL)
            return 0;
        f2c_transform_indent(output, depth);
        f2c_buffer_printf(output, "if (%s == NULL) {\n", data);
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output, "%s *%s_scalar_storage = (%s *)malloc(sizeof(%s));\n",
                          f2c_symbol_c_type(target), lowering->prefix, f2c_symbol_c_type(target),
                          f2c_symbol_c_type(target));
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output, "if (%s_scalar_storage == NULL) abort();\n", lowering->prefix);
        Buffer storage = {0};
        f2c_buffer_printf(&storage, "%s_scalar_storage", lowering->prefix);
        const int success = f2c_storage_emit_store(
            output, lowering->unit, &reference, F2C_OBJECT_DATA, 0U, NULL, storage.data, depth + 1);
        free(storage.data);
        free(data);
        if (!success)
            return 0;
        f2c_transform_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    }
    F2cExpr variable = {0};
    variable.kind = F2C_EXPR_NAME;
    variable.type = target->type;
    variable.type_kind = target->kind;
    variable.symbol = target;
    variable.storage_qualifiers = reference.object_qualifiers;
    int supported = 0;
    char *destination =
        target->equivalence_unaligned
            ? f2c_emit_unaligned_designator_address(lowering->unit, &variable, &supported)
            : f2c_emit_expression_ast(lowering->unit, &variable, &supported);
    if (destination == NULL || !supported) {
        free(destination);
        return 0;
    }
    f2c_transform_indent(&lowering->context->output, depth);
    if (target->equivalence_unaligned)
        f2c_buffer_printf(&lowering->context->output, "f2c_unaligned_store_%s(%s, %s_result[0]);\n",
                          f2c_unaligned_access_suffix(target), destination, lowering->prefix);
    else
        f2c_buffer_printf(&lowering->context->output, "%s = %s_result[0];\n", destination,
                          lowering->prefix);
    free(destination);
    return !lowering->context->output.failed;
}

int f2c_findloc_result_commit(F2cFindloc *lowering, int depth) {
    Buffer *output = &lowering->context->output;
    Unit *unit = lowering->unit;
    Symbol *target = lowering->target;
    const char *name = lowering->prefix;
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(target);
    if (target->rank == 0U)
        return commit_scalar(lowering, depth);
    if (target->allocatable) {
        char *data = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        if (data == NULL)
            return 0;
        f2c_transform_indent(output, depth);
        f2c_buffer_printf(output, "bool %s_conforms = %s != NULL;\n", name, data);
        for (size_t axis = 0U; axis < target->rank; ++axis) {
            char *extent = f2c_symbol_dimension_extent(unit, target, axis);
            if (extent == NULL) {
                free(data);
                return 0;
            }
            f2c_transform_indent(output, depth);
            f2c_buffer_printf(output,
                              "if (%s_conforms && (size_t)(%s) != %s_result_extents[%zu]) "
                              "%s_conforms = false;\n",
                              name, extent, name, axis, name);
            free(extent);
        }
        f2c_transform_indent(output, depth);
        f2c_buffer_printf(output, "if (!%s_conforms) {\n", name);
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output, "free(%s);\n", data);
        Buffer result = {0};
        f2c_buffer_printf(&result, "%s_result", name);
        int success = f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_DATA, 0U, NULL,
                                             result.data, depth + 1);
        free(result.data);
        for (size_t axis = 0U; success && axis < target->rank; ++axis) {
            Buffer extent = {0};
            f2c_buffer_printf(&extent, "(int32_t)%s_result_extents[%zu]", name, axis);
            success = f2c_storage_emit_contiguous_dimension(output, unit, &reference, axis, NULL,
                                                            "1", extent.data, depth + 1);
            free(extent.data);
        }
        free(data);
        if (!success)
            return 0;
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output, "%s_result = NULL;\n", name);
        f2c_transform_indent(output, depth);
        f2c_buffer_append(output, "}\n");
        f2c_transform_indent(output, depth);
        f2c_buffer_printf(output, "if (%s_result != NULL) {\n", name);
        if (!copy_array(lowering, depth + 1))
            return 0;
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output, "free(%s_result);\n", name);
        f2c_transform_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    } else {
        for (size_t axis = 0U; axis < target->rank; ++axis) {
            char *extent = f2c_symbol_dimension_extent(unit, target, axis);
            if (extent == NULL)
                return 0;
            f2c_transform_indent(output, depth);
            f2c_buffer_printf(output, "if ((size_t)(%s) != %s_result_extents[%zu]) abort();\n",
                              extent, name, axis);
            free(extent);
        }
        if (!copy_array(lowering, depth))
            return 0;
        f2c_transform_indent(output, depth);
        f2c_buffer_printf(output, "free(%s_result);\n", name);
    }
    return !output->failed;
}
