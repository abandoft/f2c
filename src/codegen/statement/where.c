#include "codegen/statement/private.h"

#include "codegen/array/private.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    while (depth-- > 0)
        f2c_buffer_append(output, "    ");
}

static size_t statement_id(const Unit *unit, const F2cStatement *statement) {
    return f2c_statement_unit_index(unit, statement);
}

static int numeric_type(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
           type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
}

static char **ordinal_names(size_t identifier, size_t rank) {
    char **names = rank != 0U ? (char **)calloc(rank, sizeof(*names)) : NULL;
    size_t dimension;
    if (rank != 0U && names == NULL)
        return NULL;
    for (dimension = 0U; dimension < rank; ++dimension) {
        Buffer name = {0};
        f2c_buffer_printf(&name, "f2c_where_index_%zu_%zu", identifier, dimension + 1U);
        names[dimension] = f2c_buffer_take(&name);
        if (names[dimension] == NULL) {
            while (dimension != 0U)
                free(names[--dimension]);
            free(names);
            return NULL;
        }
    }
    return names;
}

static void free_names(char **names, size_t count) {
    if (names == NULL)
        return;
    while (count != 0U)
        free(names[--count]);
    free(names);
}

static void emit_indices(Buffer *output, size_t identifier, size_t rank, int depth) {
    size_t dimension;
    indent(output, depth);
    f2c_buffer_printf(output, "size_t f2c_where_remaining_%zu = f2c_where_linear_%zu;\n",
                      identifier, identifier);
    for (dimension = 0U; dimension < rank; ++dimension) {
        indent(output, depth);
        f2c_buffer_printf(output,
                          "const size_t f2c_where_index_%zu_%zu = "
                          "f2c_where_remaining_%zu %% f2c_where_extent_%zu_%zu;\n",
                          identifier, dimension + 1U, identifier, identifier, dimension + 1U);
        indent(output, depth);
        f2c_buffer_printf(output, "(void)f2c_where_index_%zu_%zu;\n", identifier, dimension + 1U);
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_where_remaining_%zu /= f2c_where_extent_%zu_%zu;\n",
                          identifier, identifier, dimension + 1U);
    }
}

static void emit_loop_begin(Buffer *output, size_t identifier, size_t rank, int depth) {
    indent(output, depth);
    f2c_buffer_printf(output,
                      "for (size_t f2c_where_linear_%zu = 0U; f2c_where_linear_%zu < "
                      "f2c_where_count_%zu; ++f2c_where_linear_%zu) {\n",
                      identifier, identifier, identifier, identifier);
    emit_indices(output, identifier, rank, depth + 1);
}

static void emit_loop_end(Buffer *output, int depth) {
    indent(output, depth);
    f2c_buffer_append(output, "}\n");
}

static int expression_extents(Context *context, Unit *unit, const F2cStatement *statement,
                              const F2cExpr *expression, char **extents) {
    size_t dimension;
    if (expression == NULL || expression->rank == 0U || expression->rank > F2C_MAX_RANK)
        return 0;
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        extents[dimension] = f2c_array_expression_extent(unit, expression, dimension);
        if (extents[dimension] == NULL) {
            f2c_diagnostic(context, statement->line, 1,
                           "cannot lower dynamic extent %zu of WHERE array expression",
                           dimension + 1U);
            return 0;
        }
    }
    return 1;
}

static void free_extents(char **extents, size_t rank) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension)
        free(extents[dimension]);
}

static void emit_shape_checks(Buffer *output, char *const *extents, size_t rank, size_t identifier,
                              int depth) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension) {
        indent(output, depth);
        f2c_buffer_printf(output,
                          "if (((int64_t)(%s) > 0 ? (size_t)(%s) : 0U) != "
                          "f2c_where_extent_%zu_%zu) abort();\n",
                          extents[dimension], extents[dimension], identifier, dimension + 1U);
    }
}

static int emit_element(Unit *unit, const F2cExpr *expression, size_t rank,
                        const char *const *ordinals, F2cExpr **element, char **code) {
    int supported = 0;
    *element = f2c_array_element_expression(unit, expression, rank, ordinals);
    if (*element == NULL)
        return 0;
    *code = f2c_emit_expression_ast(unit, *element, &supported);
    if (!supported || *code == NULL) {
        free(*code);
        *code = NULL;
        f2c_expr_free(*element);
        *element = NULL;
        return 0;
    }
    return 1;
}

static void rollback_output(Context *context, size_t start) {
    context->output.length = start;
    if (context->output.data != NULL)
        context->output.data[start] = '\0';
}

int f2c_emit_where_begin(Context *context, Unit *unit, const F2cStatement *statement, int *depth) {
    const size_t output_start = context->output.length;
    const size_t identifier = statement_id(unit, statement);
    const F2cStatement *parent = statement->construct_owner;
    const size_t rank = statement->expression != NULL ? statement->expression->rank : 0U;
    const int nested = parent != NULL && parent->kind == F2C_STMT_WHERE;
    const size_t parent_id = nested ? statement_id(unit, parent) : 0U;
    char *extents[F2C_MAX_RANK] = {0};
    char **ordinals = NULL;
    F2cExpr *prepared_mask = NULL;
    F2cExpr *mask_element = NULL;
    char *mask = NULL;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    size_t temporary = 0U;
    size_t dimension;
    prepared_mask = f2c_array_clone_expression(statement->expression);
    if (prepared_mask == NULL ||
        !f2c_array_hoist_scalar_subexpressions(unit, prepared_mask, identifier, "where_mask",
                                               &temporary, &prelude, *depth + 1, 1) ||
        !f2c_array_materialize_constructors(context, unit, prepared_mask, identifier, "mask",
                                            &temporary, &prelude, &cleanup, *depth + 1) ||
        !expression_extents(context, unit, statement, prepared_mask, extents))
        goto failed;
    ordinals = ordinal_names(identifier, rank);
    if (ordinals == NULL || !emit_element(unit, prepared_mask, rank, (const char *const *)ordinals,
                                          &mask_element, &mask))
        goto failed;
    indent(&context->output, *depth);
    f2c_buffer_append(&context->output, "{\n");
    ++*depth;
    f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
    indent(&context->output, *depth);
    f2c_buffer_printf(&context->output, "size_t f2c_where_count_%zu = 1U;\n", identifier);
    for (dimension = 0U; dimension < rank; ++dimension) {
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output,
                          "const int64_t f2c_where_raw_extent_%zu_%zu = (int64_t)(%s);\n",
                          identifier, dimension + 1U, extents[dimension]);
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_where_extent_%zu_%zu = "
                          "f2c_where_raw_extent_%zu_%zu > 0 ? "
                          "(size_t)f2c_where_raw_extent_%zu_%zu : 0U;\n",
                          identifier, dimension + 1U, identifier, dimension + 1U, identifier,
                          dimension + 1U);
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_where_extent_%zu_%zu != 0U && f2c_where_count_%zu > "
                          "SIZE_MAX / f2c_where_extent_%zu_%zu) abort();\n",
                          identifier, dimension + 1U, identifier, identifier, dimension + 1U);
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "f2c_where_count_%zu *= f2c_where_extent_%zu_%zu;\n",
                          identifier, identifier, dimension + 1U);
    }
    if (nested) {
        for (dimension = 0U; dimension < rank; ++dimension) {
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_where_extent_%zu_%zu != f2c_where_extent_%zu_%zu) "
                              "abort();\n",
                              identifier, dimension + 1U, parent_id, dimension + 1U);
        }
    }
    indent(&context->output, *depth);
    f2c_buffer_printf(&context->output,
                      "bool *f2c_where_active_%zu = (bool *)calloc("
                      "f2c_where_count_%zu == 0U ? 1U : f2c_where_count_%zu, sizeof(bool));\n",
                      identifier, identifier, identifier);
    indent(&context->output, *depth);
    f2c_buffer_printf(&context->output,
                      "bool *f2c_where_matched_%zu = (bool *)calloc("
                      "f2c_where_count_%zu == 0U ? 1U : f2c_where_count_%zu, sizeof(bool));\n",
                      identifier, identifier, identifier);
    indent(&context->output, *depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_active_%zu == NULL || f2c_where_matched_%zu == NULL) "
                      "abort();\n",
                      identifier, identifier);
    emit_loop_begin(&context->output, identifier, rank, *depth);
    indent(&context->output, *depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_where_mask_%zu = (bool)(%s);\n", identifier,
                      mask);
    indent(&context->output, *depth + 1);
    if (nested)
        f2c_buffer_printf(&context->output,
                          "f2c_where_active_%zu[f2c_where_linear_%zu] = "
                          "f2c_where_active_%zu[f2c_where_linear_%zu] && "
                          "f2c_where_mask_%zu;\n",
                          identifier, identifier, parent_id, identifier, identifier);
    else
        f2c_buffer_printf(&context->output,
                          "f2c_where_active_%zu[f2c_where_linear_%zu] = "
                          "f2c_where_mask_%zu;\n",
                          identifier, identifier, identifier);
    indent(&context->output, *depth + 1);
    f2c_buffer_printf(&context->output,
                      "f2c_where_matched_%zu[f2c_where_linear_%zu] = "
                      "f2c_where_active_%zu[f2c_where_linear_%zu];\n",
                      identifier, identifier, identifier, identifier);
    emit_loop_end(&context->output, *depth);
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    free(mask);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_mask);
    f2c_expr_free(mask_element);
    free_names(ordinals, rank);
    free_extents(extents, rank);
    return 1;

failed:
    rollback_output(context, output_start);
    free(mask);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_mask);
    f2c_expr_free(mask_element);
    free_names(ordinals, rank);
    free_extents(extents, rank);
    f2c_diagnostic(context, statement->line, 1,
                   "cannot lower WHERE mask to a scalar element expression");
    return 0;
}

int f2c_emit_elsewhere(Context *context, Unit *unit, const F2cStatement *statement, int depth) {
    const size_t output_start = context->output.length;
    const F2cStatement *owner = statement->construct_owner;
    const F2cStatement *parent;
    size_t identifier;
    size_t parent_id = 0U;
    size_t rank;
    char *extents[F2C_MAX_RANK] = {0};
    char **ordinals = NULL;
    F2cExpr *prepared_mask = NULL;
    F2cExpr *mask_element = NULL;
    char *mask = NULL;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    size_t temporary = 0U;
    if (owner == NULL || owner->kind != F2C_STMT_WHERE || owner->expression == NULL)
        return 0;
    identifier = statement_id(unit, owner);
    rank = owner->expression->rank;
    parent = owner->construct_owner;
    if (parent != NULL && parent->kind == F2C_STMT_WHERE)
        parent_id = statement_id(unit, parent);
    if (statement->expression != NULL) {
        const size_t branch_id = statement_id(unit, statement);
        prepared_mask = f2c_array_clone_expression(statement->expression);
        if (prepared_mask == NULL ||
            !f2c_array_hoist_scalar_subexpressions(unit, prepared_mask, branch_id,
                                                   "where_branch_mask", &temporary, &prelude, depth,
                                                   1) ||
            !f2c_array_materialize_constructors(context, unit, prepared_mask, branch_id,
                                                "branch_mask", &temporary, &prelude, &cleanup,
                                                depth) ||
            !expression_extents(context, unit, statement, prepared_mask, extents))
            goto failed;
        ordinals = ordinal_names(identifier, rank);
        if (ordinals == NULL || !emit_element(unit, prepared_mask, rank,
                                              (const char *const *)ordinals, &mask_element, &mask))
            goto failed;
        f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
        emit_shape_checks(&context->output, extents, rank, identifier, depth);
    }
    emit_loop_begin(&context->output, identifier, rank, depth);
    if (mask != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const bool f2c_where_mask_%zu = (bool)(%s);\n",
                          identifier, mask);
    }
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_where_selected_%zu = ", identifier);
    if (parent != NULL && parent->kind == F2C_STMT_WHERE)
        f2c_buffer_printf(&context->output, "f2c_where_active_%zu[f2c_where_linear_%zu] && ",
                          parent_id, identifier);
    f2c_buffer_printf(&context->output, "!f2c_where_matched_%zu[f2c_where_linear_%zu]", identifier,
                      identifier);
    if (mask != NULL)
        f2c_buffer_printf(&context->output, " && f2c_where_mask_%zu", identifier);
    f2c_buffer_append(&context->output, ";\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "f2c_where_active_%zu[f2c_where_linear_%zu] = "
                      "f2c_where_selected_%zu;\n",
                      identifier, identifier, identifier);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "f2c_where_matched_%zu[f2c_where_linear_%zu] = "
                      "f2c_where_matched_%zu[f2c_where_linear_%zu] || "
                      "f2c_where_selected_%zu;\n",
                      identifier, identifier, identifier, identifier, identifier);
    emit_loop_end(&context->output, depth);
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    free(mask);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_mask);
    f2c_expr_free(mask_element);
    free_names(ordinals, rank);
    free_extents(extents, rank);
    return 1;

failed:
    rollback_output(context, output_start);
    free(mask);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_mask);
    f2c_expr_free(mask_element);
    free_names(ordinals, rank);
    free_extents(extents, rank);
    f2c_diagnostic(context, statement->line, 1,
                   "cannot lower ELSEWHERE mask to a scalar element expression");
    return 0;
}

int f2c_emit_where_end(Context *context, Unit *unit, const F2cStatement *statement, int *depth) {
    const F2cStatement *owner =
        statement->kind == F2C_STMT_WHERE ? statement : statement->construct_owner;
    size_t identifier;
    if (owner == NULL || owner->kind != F2C_STMT_WHERE)
        return 0;
    identifier = statement_id(unit, owner);
    indent(&context->output, *depth);
    f2c_buffer_printf(&context->output, "free(f2c_where_active_%zu);\n", identifier);
    indent(&context->output, *depth);
    f2c_buffer_printf(&context->output, "free(f2c_where_matched_%zu);\n", identifier);
    if (*depth > 1)
        --*depth;
    indent(&context->output, *depth);
    f2c_buffer_append(&context->output, "}\n");
    return 1;
}

static int emit_numeric_assignment(Context *context, Unit *unit, const F2cStatement *statement,
                                   const F2cExpr *left_element, const F2cExpr *right_element,
                                   const char *left, const char *right, size_t identifier,
                                   size_t rank, int depth) {
    const char *c_type = f2c_expression_c_type(left_element);
    char *value = NULL;
    if (numeric_type(left_element->type) && numeric_type(right_element->type) &&
        left_element->type != right_element->type)
        value = f2c_emit_numeric_conversion(right, right_element->type, left_element->type);
    else
        value = f2c_strdup(right);
    if (value == NULL)
        return 0;
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_count_%zu > SIZE_MAX / sizeof(%s)) abort();\n", identifier,
                      c_type);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_where_values_%zu = f2c_where_count_%zu == 0U ? NULL : "
                      "(%s *)malloc(f2c_where_count_%zu * sizeof(*f2c_where_values_%zu));\n",
                      c_type, identifier, identifier, c_type, identifier, identifier);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_count_%zu != 0U && f2c_where_values_%zu == NULL) abort();\n",
                      identifier, identifier);
    emit_loop_begin(&context->output, identifier, rank, depth);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_active_%zu[f2c_where_linear_%zu]) "
                      "f2c_where_values_%zu[f2c_where_linear_%zu] = %s;\n",
                      identifier, identifier, identifier, identifier, value);
    emit_loop_end(&context->output, depth);
    emit_loop_begin(&context->output, identifier, rank, depth);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_active_%zu[f2c_where_linear_%zu]) (%s) = "
                      "f2c_where_values_%zu[f2c_where_linear_%zu];\n",
                      identifier, identifier, left, identifier, identifier);
    emit_loop_end(&context->output, depth);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "free(f2c_where_values_%zu);\n", identifier);
    free(value);
    (void)unit;
    (void)statement;
    return 1;
}

static int emit_character_assignment(Context *context, Unit *unit, const F2cStatement *statement,
                                     const F2cExpr *left_element, const F2cExpr *right_element,
                                     const char *left, const char *right, size_t identifier,
                                     size_t rank, int depth) {
    char *length = f2c_character_length_expression(unit, left_element);
    if (length == NULL)
        return 0;
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_where_length_%zu = (size_t)(%s);\n",
                      identifier, length);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_length_%zu != 0U && f2c_where_count_%zu > "
                      "SIZE_MAX / f2c_where_length_%zu) abort();\n",
                      identifier, identifier, identifier);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "char *f2c_where_values_%zu = (char *)malloc("
                      "f2c_where_count_%zu * f2c_where_length_%zu == 0U ? 1U : "
                      "f2c_where_count_%zu * f2c_where_length_%zu);\n",
                      identifier, identifier, identifier, identifier, identifier);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (f2c_where_values_%zu == NULL) abort();\n", identifier);
    emit_loop_begin(&context->output, identifier, rank, depth);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "if (f2c_where_active_%zu[f2c_where_linear_%zu]) {\n",
                      identifier, identifier);
    {
        Buffer pointer = {0};
        Buffer target_length = {0};
        f2c_buffer_printf(&pointer,
                          "f2c_where_values_%zu + f2c_where_linear_%zu * "
                          "f2c_where_length_%zu",
                          identifier, identifier, identifier);
        f2c_buffer_printf(&target_length, "f2c_where_length_%zu", identifier);
        if (!f2c_emit_character_storage_assignment(context, unit, pointer.data, target_length.data,
                                                   right_element, right, depth + 2)) {
            free(pointer.data);
            free(target_length.data);
            free(length);
            return 0;
        }
        free(pointer.data);
        free(target_length.data);
    }
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    emit_loop_end(&context->output, depth);
    emit_loop_begin(&context->output, identifier, rank, depth);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_active_%zu[f2c_where_linear_%zu] && "
                      "f2c_where_length_%zu != 0U) memmove(&(%s), "
                      "f2c_where_values_%zu + f2c_where_linear_%zu * "
                      "f2c_where_length_%zu, f2c_where_length_%zu);\n",
                      identifier, identifier, identifier, left, identifier, identifier, identifier,
                      identifier);
    emit_loop_end(&context->output, depth);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "free(f2c_where_values_%zu);\n", identifier);
    free(length);
    (void)statement;
    return 1;
}

static int emit_derived_assignment(Context *context, const F2cStatement *statement,
                                   const F2cExpr *left_element, const char *left, const char *right,
                                   size_t identifier, size_t rank, int depth) {
    const char *name =
        left_element->derived_type != NULL ? left_element->derived_type->c_name : NULL;
    if (name == NULL)
        return 0;
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_count_%zu > SIZE_MAX / sizeof(%s)) abort();\n", identifier,
                      name);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_where_values_%zu = (%s *)calloc("
                      "f2c_where_count_%zu == 0U ? 1U : f2c_where_count_%zu, sizeof(%s));\n",
                      name, identifier, name, identifier, identifier, name);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (f2c_where_values_%zu == NULL) abort();\n", identifier);
    emit_loop_begin(&context->output, identifier, rank, depth);
    if (statement->right->rank == 0U && (statement->right->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR ||
                                         statement->right->kind == F2C_EXPR_CALL)) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (f2c_where_active_%zu[f2c_where_linear_%zu]) {\n",
                          identifier, identifier);
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output, "%s f2c_where_element_%zu = %s;\n", name, identifier,
                          right);
        if (statement->right->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output, "f2c_initialize_%s(&f2c_where_element_%zu);\n",
                              name, identifier);
        }
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "f2c_clone_%s(&f2c_where_values_%zu[f2c_where_linear_%zu], "
                          "&f2c_where_element_%zu);\n",
                          name, identifier, identifier, identifier);
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_where_element_%zu);\n", name,
                          identifier);
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_where_active_%zu[f2c_where_linear_%zu]) "
                          "f2c_clone_%s(&f2c_where_values_%zu[f2c_where_linear_%zu], &(%s));\n",
                          identifier, identifier, name, identifier, identifier, right);
    }
    emit_loop_end(&context->output, depth);
    emit_loop_begin(&context->output, identifier, rank, depth);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_active_%zu[f2c_where_linear_%zu]) "
                      "f2c_copy_%s(&(%s), &f2c_where_values_%zu[f2c_where_linear_%zu]);\n",
                      identifier, identifier, name, left, identifier, identifier);
    emit_loop_end(&context->output, depth);
    emit_loop_begin(&context->output, identifier, rank, depth);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_where_active_%zu[f2c_where_linear_%zu]) "
                      "f2c_destroy_%s(&f2c_where_values_%zu[f2c_where_linear_%zu]);\n",
                      identifier, identifier, name, identifier, identifier);
    emit_loop_end(&context->output, depth);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "free(f2c_where_values_%zu);\n", identifier);
    return 1;
}

int f2c_emit_where_assignment(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth) {
    const size_t output_start = context->output.length;
    const F2cStatement *owner = statement->construct_owner;
    size_t identifier;
    size_t rank;
    char *left_extents[F2C_MAX_RANK] = {0};
    char *right_extents[F2C_MAX_RANK] = {0};
    char **ordinals = NULL;
    F2cExpr *prepared_left = NULL;
    F2cExpr *prepared_right = NULL;
    F2cExpr *left_element = NULL;
    F2cExpr *right_element = NULL;
    char *left = NULL;
    char *right = NULL;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    size_t temporary = 0U;
    int emitted = 0;
    if (owner == NULL || owner->kind != F2C_STMT_WHERE || owner->expression == NULL ||
        statement->left == NULL || statement->right == NULL)
        return 0;
    identifier = statement_id(unit, owner);
    rank = owner->expression->rank;
    prepared_left = f2c_array_clone_expression(statement->left);
    prepared_right = f2c_array_clone_expression(statement->right);
    if (prepared_left == NULL || prepared_right == NULL ||
        !f2c_array_hoist_scalar_subexpressions(unit, prepared_left, identifier, "where_left",
                                               &temporary, &prelude, depth + 1, 1) ||
        !f2c_array_hoist_scalar_subexpressions(unit, prepared_right, identifier, "where_right",
                                               &temporary, &prelude, depth + 1, 1) ||
        !f2c_array_materialize_constructors(context, unit, prepared_left, identifier, "left",
                                            &temporary, &prelude, &cleanup, depth + 1) ||
        !f2c_array_materialize_constructors(context, unit, prepared_right, identifier, "right",
                                            &temporary, &prelude, &cleanup, depth + 1) ||
        !expression_extents(context, unit, statement, prepared_left, left_extents))
        goto failed;
    if (statement->right->rank != 0U &&
        !expression_extents(context, unit, statement, prepared_right, right_extents))
        goto failed;
    ordinals = ordinal_names(identifier, rank);
    if (ordinals == NULL ||
        !emit_element(unit, prepared_left, rank, (const char *const *)ordinals, &left_element,
                      &left) ||
        !emit_element(unit, prepared_right, rank, (const char *const *)ordinals, &right_element,
                      &right))
        goto failed;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
    emit_shape_checks(&context->output, left_extents, rank, identifier, depth);
    if (statement->right->rank != 0U)
        emit_shape_checks(&context->output, right_extents, rank, identifier, depth);
    if (left_element->type == TYPE_CHARACTER && right_element->type == TYPE_CHARACTER) {
        emitted = emit_character_assignment(context, unit, statement, left_element, right_element,
                                            left, right, identifier, rank, depth);
    } else if (left_element->type == TYPE_DERIVED && right_element->type == TYPE_DERIVED &&
               left_element->derived_type == right_element->derived_type) {
        emitted = emit_derived_assignment(context, statement, left_element, left, right, identifier,
                                          rank, depth);
    } else if (left_element->type != TYPE_CHARACTER && left_element->type != TYPE_DERIVED) {
        emitted = emit_numeric_assignment(context, unit, statement, left_element, right_element,
                                          left, right, identifier, rank, depth);
    }
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    --depth;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    if (!emitted)
        goto failed;
    free(left);
    free(right);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_left);
    f2c_expr_free(prepared_right);
    f2c_expr_free(left_element);
    f2c_expr_free(right_element);
    free_names(ordinals, rank);
    free_extents(left_extents, rank);
    if (statement->right->rank != 0U)
        free_extents(right_extents, rank);
    return 1;

failed:
    rollback_output(context, output_start);
    free(left);
    free(right);
    free(prelude.data);
    free(cleanup.data);
    f2c_expr_free(prepared_left);
    f2c_expr_free(prepared_right);
    f2c_expr_free(left_element);
    f2c_expr_free(right_element);
    free_names(ordinals, rank);
    free_extents(left_extents, rank);
    if (statement->right->rank != 0U)
        free_extents(right_extents, rank);
    f2c_diagnostic(context, statement->line, 1,
                   "cannot lower masked assignment to scalar element operations");
    return 0;
}
