#include "codegen/descriptor/private.h"

#include "codegen/array/private.h"
#include "codegen/value/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static char *temporary_name(size_t identifier, const char *suffix, size_t dimension) {
    Buffer result = {0};
    f2c_buffer_printf(&result, "f2c_call_actual_%zu_%s", identifier, suffix);
    if (dimension != 0U)
        f2c_buffer_printf(&result, "_%zu", dimension);
    return f2c_buffer_take(&result);
}

static char *contiguous_stride(const F2cDescriptorView *view, size_t dimension) {
    char *stride = f2c_strdup("1");
    size_t prior;
    for (prior = 0U; stride != NULL && prior < dimension; ++prior) {
        Buffer next = {0};
        f2c_buffer_printf(&next, "f2c_descriptor_stride_extent((ptrdiff_t)(%s), (size_t)(%s))",
                          stride, view->extent[prior]);
        free(stride);
        stride = f2c_buffer_take(&next);
    }
    return stride;
}

static int initialize_shape(Buffer *prelude, Unit *unit, const F2cExpr *expression,
                            size_t identifier, int depth, F2cDescriptorView *view) {
    size_t dimension;
    view->rank = expression->rank;
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        char *extent = f2c_array_expression_extent(unit, expression, dimension);
        view->lower[dimension] = f2c_strdup("1");
        view->extent[dimension] = temporary_name(identifier, "extent", dimension + 1U);
        if (extent == NULL || view->lower[dimension] == NULL || view->extent[dimension] == NULL) {
            free(extent);
            return 0;
        }
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "const size_t %s = (size_t)(%s);\n", view->extent[dimension],
                          extent);
        free(extent);
        view->stride[dimension] = contiguous_stride(view, dimension);
        if (view->stride[dimension] == NULL)
            return 0;
    }
    return 1;
}

static char *emit_element(Unit *unit, const F2cExpr *expression, size_t identifier,
                          F2cExpr **element) {
    const char *ordinals[F2C_MAX_RANK] = {0};
    char ordinal_names[F2C_MAX_RANK][80];
    char *result;
    int supported = 0;
    size_t dimension;
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        (void)snprintf(ordinal_names[dimension], sizeof(ordinal_names[dimension]),
                       "f2c_call_actual_%zu_ordinal_%zu", identifier, dimension + 1U);
        ordinals[dimension] = ordinal_names[dimension];
    }
    *element = f2c_array_element_expression(unit, expression, expression->rank, ordinals);
    result = *element != NULL ? f2c_emit_expression_ast(unit, *element, &supported) : NULL;
    if (!supported) {
        free(result);
        result = NULL;
    }
    return result;
}

static void append_ordinal_decode(Buffer *output, const F2cDescriptorView *view, size_t identifier,
                                  const char *index_suffix) {
    size_t dimension;
    f2c_buffer_printf(output,
                      "size_t f2c_call_actual_%zu_ordinal = "
                      "f2c_call_actual_%zu_%s; ",
                      identifier, identifier, index_suffix);
    for (dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(output,
                          "size_t f2c_call_actual_%zu_ordinal_%zu = "
                          "f2c_call_actual_%zu_ordinal %% %s; "
                          "f2c_call_actual_%zu_ordinal /= %s; ",
                          identifier, dimension + 1U, identifier, view->extent[dimension],
                          identifier, view->extent[dimension]);
}

static int append_copy_in(Buffer *prelude, Unit *unit, const F2cExpr *expression,
                          const F2cExpr *element, const char *element_code, const char *storage,
                          const char *count, const char *character_length, size_t identifier,
                          int depth, const F2cDescriptorView *view) {
    indent(prelude, depth);
    f2c_buffer_printf(prelude,
                      "for (size_t f2c_call_actual_%zu_index = 0U; "
                      "f2c_call_actual_%zu_index < %s; ++f2c_call_actual_%zu_index) { ",
                      identifier, identifier, count, identifier);
    append_ordinal_decode(prelude, view, identifier, "index");
    f2c_buffer_append(prelude, "\n");
    if (expression->type == TYPE_CHARACTER) {
        indent(prelude, depth + 1);
        f2c_buffer_printf(prelude,
                          "if (%s != 0U) memmove(%s + f2c_call_actual_%zu_index * %s, %s(%s), "
                          "%s);\n",
                          character_length, storage, identifier, character_length,
                          element->definable ? "&" : "", element_code, character_length);
    } else if (expression->type == TYPE_DERIVED) {
        Buffer destination = {0};
        f2c_buffer_printf(&destination, "%s[f2c_call_actual_%zu_index]", storage, identifier);
        if (destination.data == NULL ||
            !f2c_emit_derived_clone_expression(prelude, unit, element, destination.data,
                                               "descriptor", identifier, depth + 1)) {
            free(destination.data);
            return 0;
        }
        free(destination.data);
    } else {
        indent(prelude, depth + 1);
        f2c_buffer_printf(prelude, "%s[f2c_call_actual_%zu_index] = (%s);\n", storage, identifier,
                          element_code);
    }
    indent(prelude, depth);
    f2c_buffer_append(prelude, "}\n");
    return 1;
}

static int append_copy_out(Buffer *cleanup, Unit *unit, const F2cExpr *expression,
                           const F2cExpr *element, const char *element_code, const char *storage,
                           const char *count, const char *character_length, size_t identifier,
                           int depth, const F2cDescriptorView *view) {
    char *unaligned_address = NULL;
    indent(cleanup, depth);
    f2c_buffer_printf(cleanup,
                      "for (size_t f2c_call_actual_%zu_index = 0U; "
                      "f2c_call_actual_%zu_index < %s; ++f2c_call_actual_%zu_index) { ",
                      identifier, identifier, count, identifier);
    append_ordinal_decode(cleanup, view, identifier, "index");
    if (expression->type == TYPE_CHARACTER) {
        f2c_buffer_printf(cleanup,
                          "if (%s != 0U) memmove(&(%s), %s + "
                          "f2c_call_actual_%zu_index * %s, %s); }\n",
                          character_length, element_code, storage, identifier, character_length,
                          character_length);
    } else if (expression->type == TYPE_DERIVED) {
        f2c_buffer_printf(cleanup,
                          "f2c_destroy_%s(&(%s)); f2c_clone_%s(&(%s), "
                          "&%s[f2c_call_actual_%zu_index]); }\n",
                          expression->derived_type->c_name, element_code,
                          expression->derived_type->c_name, element_code, storage, identifier);
    } else if (element != NULL && element->symbol != NULL &&
               element->symbol->equivalence_unaligned) {
        const char *suffix = f2c_unaligned_access_suffix(element->symbol);
        int supported = 1;
        unaligned_address = f2c_emit_unaligned_designator_address(unit, element, &supported);
        if (!supported || suffix == NULL || unaligned_address == NULL) {
            free(unaligned_address);
            return 0;
        }
        f2c_buffer_printf(cleanup, "f2c_unaligned_store_%s(%s, %s[f2c_call_actual_%zu_index]); }\n",
                          suffix, unaligned_address, storage, identifier);
    } else {
        f2c_buffer_printf(cleanup, "(%s) = %s[f2c_call_actual_%zu_index]; }\n", element_code,
                          storage, identifier);
    }
    free(unaligned_address);
    return 1;
}

int f2c_descriptor_materialize_view(Buffer *prelude, Buffer *cleanup, Unit *unit,
                                    const F2cExpr *expression, F2cIntent intent, size_t identifier,
                                    int depth, F2cDescriptorView *view) {
    F2cExpr *element = NULL;
    char *element_code = NULL;
    char *count = NULL;
    char *character_length = NULL;
    const char *c_type;
    int copy_in;
    int copy_out;
    size_t dimension;
    if (prelude == NULL || cleanup == NULL || unit == NULL || expression == NULL || view == NULL ||
        expression->rank == 0U || expression->rank > F2C_MAX_RANK ||
        expression->type == TYPE_UNKNOWN ||
        (expression->type == TYPE_DERIVED && expression->derived_type == NULL) ||
        (expression->type == TYPE_CHARACTER && expression->type_kind != 0 &&
         f2c_default_kind(TYPE_CHARACTER) != expression->type_kind))
        return 0;
    memset(view, 0, sizeof(*view));
    if (!initialize_shape(prelude, unit, expression, identifier, depth, view))
        goto failed;
    element_code = emit_element(unit, expression, identifier, &element);
    copy_in = intent != F2C_INTENT_OUT;
    copy_out = intent == F2C_INTENT_OUT || intent == F2C_INTENT_INOUT ||
               (intent == F2C_INTENT_UNSPECIFIED && expression->definable);
    if (element_code == NULL || (copy_out && !element->definable))
        goto failed;
    view->data = temporary_name(identifier, "values", 0U);
    count = temporary_name(identifier, "count", 0U);
    if (view->data == NULL || count == NULL)
        goto failed;
    indent(prelude, depth);
    f2c_buffer_printf(prelude, "const size_t %s = f2c_inquiry_size(%zuU, (const size_t[]){", count,
                      view->rank);
    for (dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(prelude, "%s%s", dimension == 0U ? "" : ", ", view->extent[dimension]);
    f2c_buffer_append(prelude, "});\n");
    c_type = f2c_expression_c_type(expression);
    if (expression->type == TYPE_CHARACTER) {
        character_length = f2c_character_length_expression(unit, expression);
        if (character_length == NULL)
            goto failed;
        indent(prelude, depth);
        f2c_buffer_printf(prelude,
                          "const size_t f2c_call_actual_%zu_character_length = "
                          "(size_t)(%s);\n",
                          identifier, character_length);
        free(character_length);
        character_length = temporary_name(identifier, "character_length", 0U);
        if (character_length == NULL)
            goto failed;
        view->character_length = f2c_strdup(character_length);
        if (view->character_length == NULL)
            goto failed;
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s != 0U && %s > SIZE_MAX / %s) abort();\n",
                          character_length, count, character_length);
        indent(prelude, depth);
        f2c_buffer_printf(prelude,
                          "char *%s = (char *)malloc(%s == 0U || %s == 0U ? 1U : "
                          "%s * %s);\n",
                          view->data, count, character_length, count, character_length);
    } else if (expression->type == TYPE_DERIVED) {
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s > SIZE_MAX / sizeof(%s)) abort();\n", count, c_type);
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "%s *%s = (%s *)calloc(%s == 0U ? 1U : %s, sizeof(%s));\n",
                          c_type, view->data, c_type, count, count, c_type);
    } else if (!copy_in) {
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s > SIZE_MAX / sizeof(%s)) abort();\n", count, c_type);
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "%s *%s = (%s *)calloc(%s == 0U ? 1U : %s, sizeof(%s));\n",
                          c_type, view->data, c_type, count, count, c_type);
    } else {
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "if (%s > SIZE_MAX / sizeof(%s)) abort();\n", count, c_type);
        indent(prelude, depth);
        f2c_buffer_printf(prelude,
                          "%s *%s = (%s *)malloc(%s == 0U ? sizeof(%s) : "
                          "%s * sizeof(%s));\n",
                          c_type, view->data, c_type, count, c_type, count, c_type);
    }
    indent(prelude, depth);
    f2c_buffer_printf(prelude, "if (%s == NULL) abort();\n", view->data);
    if (copy_in && !append_copy_in(prelude, unit, expression, element, element_code, view->data,
                                   count, character_length, identifier, depth, view))
        goto failed;
    if (copy_out && !append_copy_out(cleanup, unit, expression, element, element_code, view->data,
                                     count, character_length, identifier, depth, view))
        goto failed;
    if (expression->type == TYPE_DERIVED) {
        indent(cleanup, depth);
        f2c_buffer_printf(cleanup, "f2c_destroy_array_%s(%s, %s, %zuU);\n",
                          expression->derived_type->c_name, view->data, count, view->rank);
    }
    indent(cleanup, depth);
    f2c_buffer_printf(cleanup, "free(%s);\n", view->data);
    free(element_code);
    f2c_expr_free(element);
    free(count);
    free(character_length);
    return 1;

failed:
    free(element_code);
    f2c_expr_free(element);
    free(count);
    free(character_length);
    f2c_descriptor_view_free(view);
    return 0;
}
