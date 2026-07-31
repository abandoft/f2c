#include "codegen/unit/private.h"

static void emit_expression_temporary(F2cExpr *expression, void *state) {
    Buffer *output = (Buffer *)state;
    const F2cExpr *ordered_operand = f2c_expression_ordered_binary_operand(expression);
    size_t descriptor;
    for (descriptor = 0U; descriptor < expression->host_descriptor_temporary_count; ++descriptor) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_host_descriptor_%zu = {0};\n",
                          expression->host_descriptor_temporary_begin + descriptor);
    }
    if (expression->has_contiguous_temporary) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_contiguous_source_%zu = {0};\n",
                          expression->contiguous_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_contiguous_actual_%zu = {0};\n",
                          expression->contiguous_temporary_index);
    }
    if (f2c_expression_is_character_temporary(expression)) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "char *f2c_character_result_%zu = NULL;\n",
                          expression->temporary_index);
    }
    if (f2c_expression_is_derived_actual_temporary(expression) &&
        expression->temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "%s f2c_derived_actual_%zu = {0};\n",
                          expression->derived_type->c_name, expression->temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "bool f2c_derived_actual_live_%zu = false;\n",
                          expression->temporary_index);
    }
    if (f2c_expression_has_materialized_call_result(expression) &&
        expression->temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "%s f2c_expression_result_%zu = {0};\n",
                          f2c_expression_c_type(expression), expression->temporary_index);
    }
    if (f2c_expression_has_materialized_descriptor_result(expression) &&
        expression->temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_expression_descriptor_result_%zu = {0};\n",
                          expression->temporary_index);
    }
    if (f2c_expression_has_materialized_derived_result(expression) &&
        expression->statement_temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "%s f2c_derived_result_%zu = {0};\n",
                          expression->derived_type->c_name, expression->statement_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "bool f2c_derived_result_live_%zu = false;\n",
                          expression->statement_temporary_index);
    }
    if (ordered_operand != NULL && expression->ordered_temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        if (ordered_operand->type == TYPE_CHARACTER)
            f2c_buffer_printf(output, "char *f2c_ordered_value_%zu = NULL;\n",
                              expression->ordered_temporary_index);
        else
            f2c_buffer_printf(output, "%s f2c_ordered_value_%zu = {0};\n",
                              f2c_expression_c_type(ordered_operand),
                              expression->ordered_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)f2c_ordered_value_%zu;\n",
                          expression->ordered_temporary_index);
    }
    if (expression->ordered_argument_temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        if (expression->type == TYPE_CHARACTER)
            f2c_buffer_printf(output, "char *f2c_ordered_argument_%zu = NULL;\n",
                              expression->ordered_argument_temporary_index);
        else
            f2c_buffer_printf(output, "%s f2c_ordered_argument_%zu = {0};\n",
                              f2c_expression_c_type(expression),
                              expression->ordered_argument_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)f2c_ordered_argument_%zu;\n",
                          expression->ordered_argument_temporary_index);
    }
}

void f2c_unit_emit_expression_temporaries(Buffer *output, Unit *unit) {
    size_t statement;
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement], emit_expression_temporary,
                                            output);
}
