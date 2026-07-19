#include "codegen/statement/private.h"

#include "codegen/array/private.h"

#include <stdio.h>
#include <stdlib.h>

static const char *const argument_names[] = {"from", "frompos", "len", "to", "topos"};

static const char *kind_suffix(int kind) {
    switch (kind) {
    case 1:
        return "i8";
    case 2:
        return "i16";
    case 4:
        return "i32";
    case 8:
        return "i64";
    default:
        return NULL;
    }
}

static const F2cExpr *actual(const F2cStatement *statement, size_t position) {
    return statement != NULL ? f2c_intrinsic_argument(statement->arguments, statement->item_count,
                                                      argument_names[position], position)
                             : NULL;
}

static void free_code(char **code, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index)
        free(code[index]);
}

static int emit_scalar(Context *context, Unit *unit, const F2cStatement *statement, int depth,
                       int kind, const char *suffix) {
    const char *integer_type = f2c_c_type_kind(TYPE_INTEGER, kind);
    char *code[5] = {NULL, NULL, NULL, NULL, NULL};
    size_t argument;
    int supported = 1;
    for (argument = 0U; argument < 5U; ++argument) {
        const F2cExpr *value = actual(statement, argument);
        if (value == NULL || value->rank != 0U)
            goto unsupported;
        code[argument] = f2c_emit_expression_ast(unit, value, &supported);
        if (!supported || code[argument] == NULL)
            goto unsupported;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(
        &context->output,
        "f2c_mvbits_%s((%s)(%s), (int64_t)(%s), (int64_t)(%s), &(%s), (int64_t)(%s));\n", suffix,
        integer_type, code[0], code[1], code[2], code[3], code[4]);
    free_code(code, 5U);
    return 1;

unsupported:
    free_code(code, 5U);
    return 0;
}

static void free_prepared(F2cExpr **prepared, F2cExpr **elements, char **element_code,
                          char **scalar_code, char **extents, char *actual_extents[][F2C_MAX_RANK],
                          size_t rank) {
    size_t argument;
    size_t dimension;
    for (argument = 0U; argument < 5U; ++argument) {
        f2c_expr_free(prepared[argument]);
        f2c_expr_free(elements[argument]);
        free(element_code[argument]);
        free(scalar_code[argument]);
        for (dimension = 0U; dimension < rank; ++dimension)
            free(actual_extents[argument][dimension]);
    }
    for (dimension = 0U; dimension < rank; ++dimension)
        free(extents[dimension]);
}

static void emit_loop_begin(Buffer *output, size_t identifier, size_t rank, int *depth) {
    size_t dimension;
    for (dimension = rank; dimension != 0U; --dimension) {
        const size_t current = dimension - 1U;
        f2c_array_indent(output, *depth);
        f2c_buffer_printf(output,
                          "for (size_t f2c_mvbits_ordinal_%zu_%zu = 0U; "
                          "f2c_mvbits_ordinal_%zu_%zu < f2c_mvbits_extent_%zu_%zu; "
                          "++f2c_mvbits_ordinal_%zu_%zu) {\n",
                          identifier, current, identifier, current, identifier, current, identifier,
                          current);
        ++*depth;
    }
}

static void emit_loop_end(Buffer *output, size_t rank, int *depth) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension) {
        --*depth;
        f2c_array_indent(output, *depth);
        f2c_buffer_append(output, "}\n");
    }
}

static void emit_value_reference(Buffer *output, size_t identifier, size_t argument,
                                 const F2cExpr *value) {
    if (value->rank == 0U)
        f2c_buffer_printf(output, "f2c_mvbits_scalar_%zu_%zu", identifier, argument);
    else
        f2c_buffer_printf(output, "f2c_mvbits_snapshot_%zu_%zu[f2c_mvbits_linear_%zu]", identifier,
                          argument, identifier);
}

static int emit_array(Context *context, Unit *unit, const F2cStatement *statement, int depth,
                      int kind, const char *suffix) {
    const size_t output_start = context->output.length;
    const size_t identifier = f2c_statement_unit_index(unit, statement);
    const char *integer_type = f2c_c_type_kind(TYPE_INTEGER, kind);
    F2cExpr *prepared[5] = {NULL, NULL, NULL, NULL, NULL};
    F2cExpr *elements[5] = {NULL, NULL, NULL, NULL, NULL};
    char *element_code[5] = {NULL, NULL, NULL, NULL, NULL};
    char *scalar_code[5] = {NULL, NULL, NULL, NULL, NULL};
    char *extents[F2C_MAX_RANK] = {NULL};
    char *actual_extents[5][F2C_MAX_RANK] = {{NULL}};
    char ordinal_storage[F2C_MAX_RANK][64];
    const char *ordinals[F2C_MAX_RANK] = {NULL};
    const F2cExpr *target = actual(statement, 3U);
    const size_t rank = target != NULL ? target->rank : 0U;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    size_t temporary = 0U;
    size_t argument;
    size_t dimension;
    int emitted_depth;
    int supported = 1;
    if (rank == 0U || rank > F2C_MAX_RANK)
        return 0;
    for (dimension = 0U; dimension < rank; ++dimension) {
        (void)snprintf(ordinal_storage[dimension], sizeof(ordinal_storage[dimension]),
                       "f2c_mvbits_ordinal_%zu_%zu", identifier, dimension);
        ordinals[dimension] = ordinal_storage[dimension];
    }
    for (argument = 0U; argument < 5U; ++argument) {
        const F2cExpr *value = actual(statement, argument);
        if (value == NULL || (value->rank != 0U && value->rank != rank))
            goto unsupported;
        prepared[argument] = f2c_array_clone_expression(value);
        if (prepared[argument] == NULL ||
            !f2c_array_hoist_scalar_subexpressions(unit, prepared[argument], identifier, "mvbits",
                                                   &temporary, &prelude, depth + 1, 1) ||
            !f2c_array_materialize_constructors(context, unit, prepared[argument], identifier,
                                                "mvbits", &temporary, &prelude, &cleanup,
                                                depth + 1))
            goto unsupported;
    }
    for (dimension = 0U; dimension < rank; ++dimension) {
        extents[dimension] = f2c_array_expression_extent(unit, prepared[3], dimension);
        if (extents[dimension] == NULL)
            goto unsupported;
    }
    for (argument = 0U; argument < 5U; ++argument) {
        if (prepared[argument]->rank == 0U) {
            if (argument == 3U)
                goto unsupported;
            scalar_code[argument] = f2c_emit_expression_ast(unit, prepared[argument], &supported);
            if (!supported || scalar_code[argument] == NULL)
                goto unsupported;
            continue;
        }
        for (dimension = 0U; dimension < rank; ++dimension) {
            actual_extents[argument][dimension] =
                f2c_array_expression_extent(unit, prepared[argument], dimension);
            if (actual_extents[argument][dimension] == NULL)
                goto unsupported;
        }
        elements[argument] = f2c_array_element_expression(unit, prepared[argument], rank, ordinals);
        if (elements[argument] == NULL)
            goto unsupported;
        element_code[argument] = f2c_emit_expression_ast(unit, elements[argument], &supported);
        if (!supported || element_code[argument] == NULL)
            goto unsupported;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emitted_depth = depth + 1;
    f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
    for (dimension = 0U; dimension < rank; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_mvbits_extent_%zu_%zu = (size_t)(%s);\n", identifier,
                          dimension, extents[dimension]);
    }
    for (argument = 0U; argument < 5U; ++argument) {
        if (prepared[argument]->rank == 0U || argument == 3U)
            continue;
        for (dimension = 0U; dimension < rank; ++dimension) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != f2c_mvbits_extent_%zu_%zu) abort();\n",
                              actual_extents[argument][dimension], identifier, dimension);
        }
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "size_t f2c_mvbits_count_%zu = 1U;\n", identifier);
    for (dimension = 0U; dimension < rank; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_mvbits_extent_%zu_%zu != 0U && f2c_mvbits_count_%zu > "
                          "SIZE_MAX / f2c_mvbits_extent_%zu_%zu) abort();\n",
                          identifier, dimension, identifier, identifier, dimension);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_mvbits_count_%zu *= f2c_mvbits_extent_%zu_%zu;\n",
                          identifier, identifier, dimension);
    }
    for (argument = 0U; argument < 5U; ++argument) {
        if (argument == 3U)
            continue;
        if (prepared[argument]->rank == 0U) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output, "const %s f2c_mvbits_scalar_%zu_%zu = (%s)(%s);\n",
                              f2c_expression_c_type(prepared[argument]), identifier, argument,
                              f2c_expression_c_type(prepared[argument]), scalar_code[argument]);
        } else {
            const char *type = f2c_expression_c_type(prepared[argument]);
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_mvbits_count_%zu > SIZE_MAX / sizeof(%s)) abort();\n",
                              identifier, type);
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(
                &context->output,
                "%s *f2c_mvbits_snapshot_%zu_%zu = (%s *)malloc("
                "(f2c_mvbits_count_%zu == 0U ? 1U : f2c_mvbits_count_%zu) * sizeof(%s));\n",
                type, identifier, argument, type, identifier, identifier, type);
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_mvbits_snapshot_%zu_%zu == NULL) abort();\n", identifier,
                              argument);
        }
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_mvbits_count_%zu > SIZE_MAX / sizeof(%s *)) abort();\n", identifier,
                      integer_type);
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output,
                      "%s **f2c_mvbits_targets_%zu = (%s **)malloc("
                      "(f2c_mvbits_count_%zu == 0U ? 1U : f2c_mvbits_count_%zu) * sizeof(%s *));\n",
                      integer_type, identifier, integer_type, identifier, identifier, integer_type);
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "if (f2c_mvbits_targets_%zu == NULL) abort();\n",
                      identifier);
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "size_t f2c_mvbits_linear_%zu = 0U;\n", identifier);
    emit_loop_begin(&context->output, identifier, rank, &emitted_depth);
    for (argument = 0U; argument < 5U; ++argument) {
        if (argument == 3U || prepared[argument]->rank == 0U)
            continue;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "f2c_mvbits_snapshot_%zu_%zu[f2c_mvbits_linear_%zu] = (%s)(%s);\n",
                          identifier, argument, identifier,
                          f2c_expression_c_type(prepared[argument]), element_code[argument]);
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "f2c_mvbits_targets_%zu[f2c_mvbits_linear_%zu] = &(%s);\n",
                      identifier, identifier, element_code[3]);
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "++f2c_mvbits_linear_%zu;\n", identifier);
    emit_loop_end(&context->output, rank, &emitted_depth);
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(
        &context->output,
        "for (f2c_mvbits_linear_%zu = 0U; f2c_mvbits_linear_%zu < f2c_mvbits_count_%zu; "
        "++f2c_mvbits_linear_%zu) {\n",
        identifier, identifier, identifier, identifier);
    ++emitted_depth;
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "f2c_mvbits_%s((%s)(", suffix, integer_type);
    emit_value_reference(&context->output, identifier, 0U, prepared[0]);
    f2c_buffer_append(&context->output, "), (int64_t)(");
    emit_value_reference(&context->output, identifier, 1U, prepared[1]);
    f2c_buffer_append(&context->output, "), (int64_t)(");
    emit_value_reference(&context->output, identifier, 2U, prepared[2]);
    f2c_buffer_printf(&context->output,
                      "), f2c_mvbits_targets_%zu[f2c_mvbits_linear_%zu], "
                      "(int64_t)(",
                      identifier, identifier);
    emit_value_reference(&context->output, identifier, 4U, prepared[4]);
    f2c_buffer_append(&context->output, "));\n");
    --emitted_depth;
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "}\n");
    for (argument = 0U; argument < 5U; ++argument) {
        if (argument == 3U || prepared[argument]->rank == 0U)
            continue;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "free(f2c_mvbits_snapshot_%zu_%zu);\n", identifier,
                          argument);
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_printf(&context->output, "free(f2c_mvbits_targets_%zu);\n", identifier);
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    --emitted_depth;
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "}\n");
    free_prepared(prepared, elements, element_code, scalar_code, extents, actual_extents, rank);
    free(prelude.data);
    free(cleanup.data);
    return 1;

unsupported:
    context->output.length = output_start;
    if (context->output.data != NULL)
        context->output.data[output_start] = '\0';
    free_prepared(prepared, elements, element_code, scalar_code, extents, actual_extents, rank);
    free(prelude.data);
    free(cleanup.data);
    return 0;
}

int f2c_emit_mvbits_statement(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth) {
    const F2cExpr *from = actual(statement, 0U);
    const F2cExpr *to = actual(statement, 3U);
    const int kind =
        from != NULL && from->type_kind != 0 ? from->type_kind : f2c_default_kind(TYPE_INTEGER);
    const char *suffix = kind_suffix(kind);
    if (context == NULL || unit == NULL || statement == NULL || from == NULL || to == NULL ||
        suffix == NULL)
        return 0;
    return to->rank == 0U ? emit_scalar(context, unit, statement, depth, kind, suffix)
                          : emit_array(context, unit, statement, depth, kind, suffix);
}
