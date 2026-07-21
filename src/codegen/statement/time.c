#include "codegen/statement/private.h"

#include "codegen/descriptor/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int index;
    for (index = 0; index < depth; ++index)
        f2c_buffer_append(output, "    ");
}

static const F2cExpr *actual_value(const F2cExpr *actual) {
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        actual = actual->children[0];
    return actual != NULL && actual->kind != F2C_EXPR_ABSENT_ARGUMENT ? actual : NULL;
}

static const F2cExpr *intrinsic_actual(const F2cStatement *statement, const char *keyword,
                                       size_t position) {
    size_t positional = 0U;
    size_t argument;
    for (argument = 0U; statement != NULL && argument < statement->item_count; ++argument) {
        const F2cExpr *actual = statement->arguments[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return actual_value(actual);
        } else if (positional++ == position) {
            return actual_value(actual);
        }
    }
    return NULL;
}

static int resolved_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static const char *integer_maximum(int kind) {
    switch (kind) {
    case 1:
        return "INT8_MAX";
    case 2:
        return "INT16_MAX";
    case 4:
        return "INT32_MAX";
    case 8:
        return "INT64_MAX";
    default:
        return NULL;
    }
}

static int emit_numeric_output(Buffer *output, Unit *unit, const F2cExpr *target, const char *value,
                               int depth) {
    const Symbol *symbol = target != NULL ? target->symbol : NULL;
    int supported = 0;
    char *code;
    if (target == NULL || value == NULL)
        return 1;
    if (symbol != NULL && symbol->equivalence_unaligned) {
        const char *suffix = f2c_unaligned_access_suffix(symbol);
        char *address = f2c_emit_unaligned_designator_address(unit, target, &supported);
        if (!supported || suffix == NULL || address == NULL) {
            free(address);
            return 0;
        }
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_unaligned_store_%s(%s, (%s)(%s));\n", suffix, address,
                          f2c_expression_c_type(target), value);
        free(address);
        return 1;
    }
    code = f2c_emit_expression_ast(unit, target, &supported);
    if (!supported || code == NULL) {
        free(code);
        return 0;
    }
    indent(output, depth);
    f2c_buffer_printf(output, "(%s) = (%s)(%s);\n", code, f2c_expression_c_type(target), value);
    free(code);
    return 1;
}

static int emit_character_actual(Unit *unit, const F2cExpr *actual, char **pointer, char **length) {
    int supported = 0;
    *pointer = NULL;
    *length = NULL;
    if (actual == NULL)
        return 1;
    *pointer = f2c_emit_expression_ast(unit, actual, &supported);
    *length = f2c_character_length_expression(unit, actual);
    if (!supported || *pointer == NULL || *length == NULL) {
        free(*pointer);
        free(*length);
        *pointer = NULL;
        *length = NULL;
        return 0;
    }
    return 1;
}

static int emit_date_and_time(Buffer *output, Unit *unit, const F2cStatement *statement,
                              int depth) {
    const F2cExpr *date = intrinsic_actual(statement, "date", 0U);
    const F2cExpr *clock_time = intrinsic_actual(statement, "time", 1U);
    const F2cExpr *zone = intrinsic_actual(statement, "zone", 2U);
    const F2cExpr *values = intrinsic_actual(statement, "values", 3U);
    const size_t identifier = f2c_statement_unit_index(unit, statement);
    char *date_pointer = NULL;
    char *date_length = NULL;
    char *time_pointer = NULL;
    char *time_length = NULL;
    char *zone_pointer = NULL;
    char *zone_length = NULL;
    F2cDescriptorView view = {0};
    Buffer prelude = {0};
    Buffer cleanup = {0};
    int result = 0;
    if (!emit_character_actual(unit, date, &date_pointer, &date_length) ||
        !emit_character_actual(unit, clock_time, &time_pointer, &time_length) ||
        !emit_character_actual(unit, zone, &zone_pointer, &zone_length))
        goto cleanup;
    if (values != NULL &&
        !f2c_descriptor_materialize_view(&prelude, &cleanup, unit, values, F2C_INTENT_INOUT, 8U,
                                         identifier, depth + 1, &view))
        goto cleanup;
    indent(output, depth);
    f2c_buffer_append(output, "{\n");
    f2c_buffer_append(output, prelude.data != NULL ? prelude.data : "");
    if (values != NULL) {
        indent(output, depth + 1);
        f2c_buffer_printf(output, "int64_t f2c_date_values_%zu[8] = {0};\n", identifier);
        indent(output, depth + 1);
        f2c_buffer_printf(output, "if ((size_t)(%s) < 8U) abort();\n", view.extent[0]);
    }
    indent(output, depth + 1);
    f2c_buffer_printf(
        output,
        "f2c_date_and_time(%s, (size_t)(%s), %s, (size_t)(%s), %s, "
        "(size_t)(%s), ",
        date_pointer != NULL ? date_pointer : "NULL", date_length != NULL ? date_length : "0U",
        time_pointer != NULL ? time_pointer : "NULL", time_length != NULL ? time_length : "0U",
        zone_pointer != NULL ? zone_pointer : "NULL", zone_length != NULL ? zone_length : "0U");
    if (values != NULL)
        f2c_buffer_printf(output, "f2c_date_values_%zu);\n", identifier);
    else
        f2c_buffer_append(output, "NULL);\n");
    if (values != NULL) {
        indent(output, depth + 1);
        f2c_buffer_printf(output,
                          "for (size_t f2c_date_index_%zu = 0U; f2c_date_index_%zu < 8U; "
                          "++f2c_date_index_%zu) %s[f2c_date_index_%zu] = "
                          "(%s)f2c_date_values_%zu[f2c_date_index_%zu];\n",
                          identifier, identifier, identifier, view.data, identifier,
                          f2c_expression_c_type(values), identifier, identifier);
    }
    f2c_buffer_append(output, cleanup.data != NULL ? cleanup.data : "");
    indent(output, depth);
    f2c_buffer_append(output, "}\n");
    result = 1;

cleanup:
    free(date_pointer);
    free(date_length);
    free(time_pointer);
    free(time_length);
    free(zone_pointer);
    free(zone_length);
    free(prelude.data);
    free(cleanup.data);
    f2c_descriptor_view_free(&view);
    return result;
}

static int emit_system_clock(Buffer *output, Unit *unit, const F2cStatement *statement, int depth) {
    const F2cExpr *count = intrinsic_actual(statement, "count", 0U);
    const F2cExpr *count_rate = intrinsic_actual(statement, "count_rate", 1U);
    const F2cExpr *count_max = intrinsic_actual(statement, "count_max", 2U);
    const int count_kind =
        count != NULL ? resolved_kind(count) : (count_max != NULL ? resolved_kind(count_max) : 4);
    const int rate =
        count_rate != NULL && count_rate->type == TYPE_INTEGER && resolved_kind(count_rate) == 1
            ? 100
            : 1000;
    const char *maximum = integer_maximum(count_kind);
    const size_t identifier = f2c_statement_unit_index(unit, statement);
    char value[160];
    if (maximum == NULL)
        return 0;
    indent(output, depth);
    f2c_buffer_append(output, "{\n");
    if (count != NULL) {
        indent(output, depth + 1);
        f2c_buffer_printf(output,
                          "const int64_t f2c_system_count_%zu = "
                          "f2c_system_clock_count(UINT64_C(%d), (uint64_t)%s);\n",
                          identifier, rate, maximum);
        (void)snprintf(value, sizeof(value),
                       "f2c_system_count_%zu < 0 ? -(%s) : f2c_system_count_%zu", identifier,
                       maximum, identifier);
        if (!emit_numeric_output(output, unit, count, value, depth + 1))
            return 0;
    }
    if (count_rate != NULL) {
        (void)snprintf(value, sizeof(value), "%d", rate);
        if (!emit_numeric_output(output, unit, count_rate, value, depth + 1))
            return 0;
    }
    if (count_max != NULL && !emit_numeric_output(output, unit, count_max, maximum, depth + 1))
        return 0;
    indent(output, depth);
    f2c_buffer_append(output, "}\n");
    return 1;
}

static int emit_cpu_time(Buffer *output, Unit *unit, const F2cStatement *statement, int depth) {
    const F2cExpr *time = intrinsic_actual(statement, "time", 0U);
    return time != NULL && emit_numeric_output(output, unit, time, "f2c_cpu_time()", depth);
}

int f2c_emit_time_statement(Context *context, Unit *unit, const F2cStatement *statement,
                            int depth) {
    if (context == NULL || unit == NULL || statement == NULL || statement->kind != F2C_STMT_CALL)
        return 0;
    if (statement->intrinsic == F2C_INTRINSIC_DATE_AND_TIME)
        return emit_date_and_time(&context->output, unit, statement, depth);
    if (statement->intrinsic == F2C_INTRINSIC_SYSTEM_CLOCK)
        return emit_system_clock(&context->output, unit, statement, depth);
    if (statement->intrinsic == F2C_INTRINSIC_CPU_TIME)
        return emit_cpu_time(&context->output, unit, statement, depth);
    return 0;
}
