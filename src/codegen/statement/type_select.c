#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int index;
    for (index = 0; index < depth; ++index)
        f2c_buffer_append(output, "    ");
}

static const F2cStatement *enclosing_select_type(const F2cStatement *statement,
                                                 int *has_prior_guard) {
    const F2cStatement *select = statement->construct_owner;
    const F2cStatement *candidate;
    *has_prior_guard = 0;
    if (select == NULL || select->kind != F2C_STMT_SELECT_TYPE)
        return NULL;
    for (candidate = select + 1; candidate < statement; ++candidate) {
        if (candidate->kind == F2C_STMT_TYPE_GUARD && candidate->construct_owner == select)
            *has_prior_guard = 1;
    }
    return select;
}

static int derived_extends(const F2cDerivedType *candidate, const F2cDerivedType *ancestor) {
    while (candidate != NULL) {
        if (candidate == ancestor)
            return 1;
        candidate = candidate->parent;
    }
    return 0;
}

static void emit_class_guard_ids(Buffer *output, Unit *unit, const F2cDerivedType *guard,
                                 const char *tag) {
    size_t index;
    int emitted = 0;
    for (index = 0U; index < unit->derived_type_count; ++index) {
        F2cDerivedType *candidate = &unit->derived_types[index];
        if (!derived_extends(candidate, guard))
            continue;
        f2c_buffer_printf(output, "%s%s == F2C_TYPE_ID_%s", emitted ? " || " : "", tag,
                          candidate->c_name);
        emitted = 1;
    }
    for (index = 0U; index < unit->imported_derived_type_count; ++index) {
        F2cDerivedType *candidate = unit->imported_derived_types[index].type;
        if (!derived_extends(candidate, guard))
            continue;
        f2c_buffer_printf(output, "%s%s == F2C_TYPE_ID_%s", emitted ? " || " : "", tag,
                          candidate->c_name);
        emitted = 1;
    }
    if (!emitted)
        f2c_buffer_append(output, "false");
}

static char *emit_selector(Context *context, Unit *unit, const F2cExpr *expression, size_t line) {
    int supported = 0;
    char *selector = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported || selector == NULL) {
        free(selector);
        f2c_diagnostic(context, line, 1,
                       "code generation does not support this typed SELECT TYPE selector");
        return NULL;
    }
    return selector;
}

int f2c_emit_type_guard(Context *context, Unit *unit, const F2cStatement *statement, size_t line,
                        int *depth) {
    int prior;
    const F2cStatement *select = enclosing_select_type(statement, &prior);
    char *selector;
    Buffer tag = {0};
    if (select == NULL || select->expression == NULL) {
        f2c_diagnostic(context, line, 1, "TYPE/CLASS guard must be enclosed by SELECT TYPE");
        return 0;
    }
    selector = emit_selector(context, unit, select->expression, line);
    if (selector == NULL)
        return 0;
    if (prior && *depth > 1)
        --*depth;
    indent(&context->output, *depth);
    if (prior)
        f2c_buffer_append(&context->output, "} else ");
    if (statement->name != NULL && strcmp(statement->name, "default") == 0) {
        f2c_buffer_append(&context->output, "{\n");
    } else if (statement->guard_type != NULL) {
        f2c_buffer_printf(&tag, "(%s).f2c_type_tag", selector);
        f2c_buffer_append(&context->output, "if (");
        if (statement->name != NULL && strcmp(statement->name, "class") == 0)
            emit_class_guard_ids(&context->output, unit, statement->guard_type, tag.data);
        else
            f2c_buffer_printf(&context->output, "%s == F2C_TYPE_ID_%s", tag.data,
                              statement->guard_type->c_name);
        f2c_buffer_append(&context->output, ") {\n");
    } else {
        free(tag.data);
        free(selector);
        f2c_diagnostic(context, line, 1, "TYPE/CLASS guard names an unknown derived type");
        return 0;
    }
    ++*depth;
    free(tag.data);
    free(selector);
    return 1;
}
