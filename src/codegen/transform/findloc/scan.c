#include "codegen/transform/findloc/private.h"

#include <stdlib.h>

void f2c_findloc_scan(F2cFindloc *lowering, int depth) {
    Buffer *output = &lowering->context->output;
    const char *name = lowering->prefix;
    f2c_transform_indent(output, depth);
    if (lowering->has_dimension) {
        f2c_buffer_printf(
            output, "for (size_t %s_output = 0U; %s_output < %s_result_count; ++%s_output) {\n",
            name, name, name, name);
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output,
                          "size_t %s_base = 0U, %s_source_stride = 1U, %s_result_stride = 1U, "
                          "%s_selected_stride = 1U;\n",
                          name, name, name, name);
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output, "for (size_t %s_axis = 0U; %s_axis < %zuU; ++%s_axis) {\n", name,
                          name, lowering->source.rank, name);
        f2c_transform_indent(output, depth + 2);
        f2c_buffer_printf(output,
                          "if (%s_dimension == (int64_t)(%s_axis + 1U)) "
                          "%s_selected_stride = %s_source_stride; else {\n",
                          name, name, name, name);
        f2c_transform_indent(output, depth + 3);
        f2c_buffer_printf(output,
                          "const size_t %s_coordinate = (%s_output / %s_result_stride) %% "
                          "%s_extents[%s_axis];\n",
                          name, name, name, name, name);
        f2c_transform_indent(output, depth + 3);
        f2c_buffer_printf(output,
                          "%s_base += %s_coordinate * %s_source_stride; "
                          "%s_result_stride *= %s_extents[%s_axis];\n",
                          name, name, name, name, name, name);
        f2c_transform_indent(output, depth + 2);
        f2c_buffer_append(output, "}\n");
        f2c_transform_indent(output, depth + 2);
        f2c_buffer_printf(output, "%s_source_stride *= %s_extents[%s_axis];\n", name, name, name);
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_append(output, "}\n");
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(
            output, "const size_t %s_selected_extent = %s_extents[(size_t)%s_dimension - 1U];\n",
            name, name, name);
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_printf(output,
                          "for (size_t %s_step = 0U; %s_step < %s_selected_extent; ++%s_step) {\n",
                          name, name, name, name);
        f2c_transform_indent(output, depth + 2);
        f2c_buffer_printf(output,
                          "const size_t %s_coordinate = %s_back ? %s_selected_extent - 1U - "
                          "%s_step : %s_step; const size_t %s_index = %s_base + "
                          "%s_coordinate * %s_selected_stride;\n",
                          name, name, name, name, name, name, name, name, name);
        f2c_transform_indent(output, depth + 2);
        f2c_buffer_printf(output, "if ((%s) && (%s)) {\n", lowering->condition,
                          lowering->comparison);
        f2c_transform_indent(output, depth + 3);
        f2c_buffer_printf(output, "const size_t %s_position = %s_coordinate + 1U;\n", name, name);
        Buffer index = {0};
        f2c_buffer_printf(&index, "%s_output", name);
        f2c_findloc_store_position(lowering, index.data, depth + 3);
        free(index.data);
        f2c_transform_indent(output, depth + 3);
        f2c_buffer_append(output, "break;\n");
        f2c_transform_indent(output, depth + 2);
        f2c_buffer_append(output, "}\n");
        f2c_transform_indent(output, depth + 1);
        f2c_buffer_append(output, "}\n");
        f2c_transform_indent(output, depth);
        f2c_buffer_append(output, "}\n");
        return;
    }
    f2c_buffer_printf(output, "for (size_t %s_step = 0U; %s_step < %s_source_count; ++%s_step) {\n",
                      name, name, name, name);
    f2c_transform_indent(output, depth + 1);
    f2c_buffer_printf(
        output, "const size_t %s_index = %s_back ? %s_source_count - 1U - %s_step : %s_step;\n",
        name, name, name, name, name);
    f2c_transform_indent(output, depth + 1);
    f2c_buffer_printf(output, "if ((%s) && (%s)) {\n", lowering->condition, lowering->comparison);
    f2c_transform_indent(output, depth + 2);
    f2c_buffer_printf(output, "size_t %s_stride = 1U;\n", name);
    f2c_transform_indent(output, depth + 2);
    f2c_buffer_printf(output, "for (size_t %s_axis = 0U; %s_axis < %zuU; ++%s_axis) {\n", name,
                      name, lowering->source.rank, name);
    f2c_transform_indent(output, depth + 3);
    f2c_buffer_printf(
        output, "const size_t %s_position = (%s_index / %s_stride) %% %s_extents[%s_axis] + 1U;\n",
        name, name, name, name, name);
    Buffer index = {0};
    f2c_buffer_printf(&index, "%s_axis", name);
    f2c_findloc_store_position(lowering, index.data, depth + 3);
    free(index.data);
    f2c_transform_indent(output, depth + 3);
    f2c_buffer_printf(output, "%s_stride *= %s_extents[%s_axis];\n", name, name, name);
    f2c_transform_indent(output, depth + 2);
    f2c_buffer_append(output, "}\n");
    f2c_transform_indent(output, depth + 2);
    f2c_buffer_append(output, "break;\n");
    f2c_transform_indent(output, depth + 1);
    f2c_buffer_append(output, "}\n");
    f2c_transform_indent(output, depth);
    f2c_buffer_append(output, "}\n");
}
