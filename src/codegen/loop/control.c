#include "codegen/loop/control.h"
#include "codegen/names.h"

#include <stdio.h>
#include <stdlib.h>

char *f2c_loop_local_prefix(Unit *unit, const char *preferred, size_t identifier) {
    static const char *const roles[] = {"start", "limit", "step",  "remaining", "active",
                                        "count", "index", "value", "final",     "safe"};
    enum { ROLE_COUNT = sizeof(roles) / sizeof(roles[0]) };
    char members[ROLE_COUNT][64];
    const char *suffixes[ROLE_COUNT];
    for (size_t index = 0U; index < ROLE_COUNT; ++index) {
        const int length =
            snprintf(members[index], sizeof(members[index]), "%s_%zu", roles[index], identifier);
        if (length < 0 || (size_t)length >= sizeof(members[index]))
            return NULL;
        suffixes[index] = members[index];
    }
    return f2c_codegen_local_family(unit, preferred, suffixes, ROLE_COUNT);
}

static void indent(Buffer *output, int depth) {
    while (depth-- > 0)
        f2c_buffer_append(output, "    ");
}

const char *f2c_integer_loop_suffix(int kind) {
    switch (kind != 0 ? kind : f2c_default_kind(TYPE_INTEGER)) {
    case 1:
        return "I8";
    case 2:
        return "I16";
    case 4:
        return "I32";
    case 8:
        return "I64";
    default:
        return NULL;
    }
}

void f2c_loop_emit_parameter(Buffer *output, const char *prefix, size_t identifier,
                             const char *role, int kind, const F2cExpr *source, const char *code,
                             int depth) {
    indent(output, depth);
    f2c_buffer_printf(output, "const %s %s_%s_%zu = F2C_LOOP_%s_%s(%s);\n",
                      f2c_c_type_kind(TYPE_INTEGER, kind), prefix, role, identifier,
                      source->type == TYPE_INTEGER ? "INTEGER" : "REAL",
                      f2c_integer_loop_suffix(kind), code);
}

void f2c_loop_emit_state(Buffer *output, const char *prefix, size_t identifier, const char *status,
                         int formatted_status, int depth) {
    indent(output, depth);
    f2c_buffer_printf(output, "uint64_t %s_remaining_%zu;\n", prefix, identifier);
    indent(output, depth);
    f2c_buffer_printf(output,
                      "int %s_active_%zu = F2C_LOOP_BEGIN(%s_start_%zu, %s_limit_%zu, "
                      "%s_step_%zu, &%s_remaining_%zu);\n",
                      prefix, identifier, prefix, identifier, prefix, identifier, prefix,
                      identifier, prefix, identifier);
    indent(output, depth);
    if (status != NULL)
        f2c_buffer_printf(output, "if (%s_active_%zu < 0) %s = %s;\n", prefix, identifier, status,
                          formatted_status ? "0" : "F2C_IO_STATUS_OVERFLOW");
    else
        f2c_buffer_printf(output, "if (%s_active_%zu < 0) abort();\n", prefix, identifier);
}

void f2c_loop_emit_real_state(Buffer *output, const F2cExpr *variable, const char *prefix,
                              size_t identifier, int depth) {
    const int kind =
        variable->type_kind != 0 ? variable->type_kind : f2c_default_kind(variable->type);
    const char *suffix = kind == 4 ? "R4" : kind == 8 ? "R8" : "EXTENDED";
    indent(output, depth);
    f2c_buffer_printf(output, "uint64_t %s_remaining_%zu;\n", prefix, identifier);
    indent(output, depth);
    f2c_buffer_printf(output,
                      "int %s_active_%zu = F2C_REAL_LOOP_%s(%s_start_%zu, %s_limit_%zu, "
                      "%s_step_%zu, &%s_remaining_%zu);\n",
                      prefix, identifier, suffix, prefix, identifier, prefix, identifier, prefix,
                      identifier, prefix, identifier);
    indent(output, depth);
    f2c_buffer_printf(output, "if (%s_active_%zu < 0) abort();\n", prefix, identifier);
}

char *f2c_loop_store_expression(Unit *unit, const F2cExpr *variable, const char *value) {
    Buffer output = {0};
    int supported = 0;
    char *target;
    if (variable == NULL || value == NULL)
        return NULL;
    if (variable->symbol != NULL && variable->symbol->equivalence_unaligned) {
        const char *suffix = f2c_unaligned_access_suffix(variable->symbol);
        target = f2c_emit_unaligned_designator_address(unit, variable, &supported);
        if (suffix != NULL && target != NULL && supported)
            f2c_buffer_printf(&output, "f2c_unaligned_store_%s(%s, %s)", suffix, target, value);
    } else {
        target = f2c_emit_expression_ast(unit, variable, &supported);
        if (target != NULL && supported)
            f2c_buffer_printf(&output, "%s = (%s)", target, value);
    }
    free(target);
    if (output.failed) {
        free(output.data);
        return NULL;
    }
    return output.data;
}

char *f2c_loop_advance_expression(Unit *unit, const F2cExpr *variable, const char *prefix,
                                  size_t identifier) {
    const char *suffix = variable != NULL ? f2c_integer_loop_suffix(variable->type_kind) : NULL;
    Buffer value = {0};
    int supported = 0;
    char *read = f2c_emit_expression_ast(unit, variable, &supported);
    char *store = NULL;
    if (read != NULL && supported && variable != NULL) {
        if (variable->type == TYPE_INTEGER && suffix != NULL)
            f2c_buffer_printf(&value, "F2C_LOOP_%s(%s, %s_step_%zu)", suffix, read, prefix,
                              identifier);
        else if (variable->type == TYPE_REAL || variable->type == TYPE_DOUBLE)
            f2c_buffer_printf(&value, "(%s)(%s + %s_step_%zu)", f2c_expression_c_type(variable),
                              read, prefix, identifier);
        else
            value.failed = 1;
        if (!value.failed)
            store = f2c_loop_store_expression(unit, variable, value.data);
    }
    free(read);
    free(value.data);
    return store;
}

void f2c_loop_emit_header(Buffer *output, const char *prefix, size_t identifier,
                          const char *advance, const char *status_condition, int depth) {
    indent(output, depth);
    f2c_buffer_printf(output, "for (; %s_active_%zu > 0", prefix, identifier);
    if (status_condition != NULL)
        f2c_buffer_printf(output, " && (%s)", status_condition);
    f2c_buffer_printf(output,
                      "; %s_active_%zu = %s_remaining_%zu != 0U, "
                      "%s_remaining_%zu -= (uint64_t)%s_active_%zu, %s) {\n",
                      prefix, identifier, prefix, identifier, prefix, identifier, prefix,
                      identifier, advance);
}
