#include "codegen/expression/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>

static const F2cExpr *actual_value(const F2cExpr *actual) {
    return actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U
               ? actual->children[0]
               : actual;
}

static char *emit_descriptor_actual(Unit *unit, const F2cExpr *actual, int *supported) {
    Buffer result = {0};
    char *character_length = NULL;
    F2cDescriptorView view = {0};
    Symbol *symbol;
    size_t dimension;
    actual = actual_value(actual);
    if (actual != NULL && actual->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return f2c_strdup("NULL");
    if (actual == NULL || actual->symbol == NULL || !f2c_descriptor_view(unit, actual, &view)) {
        *supported = 0;
        return NULL;
    }
    symbol = actual->symbol;
    if (actual->type == TYPE_CHARACTER)
        character_length = f2c_character_length_expression(unit, actual);
    f2c_buffer_printf(&result,
                      "(&(f2c_descriptor){.data = f2c_implicit_mutable_actual(%s), "
                      ".element_size = sizeof(%s), .rank = %zuU, .lower = {",
                      view.data, f2c_symbol_c_type(symbol), view.rank);
    for (dimension = 0U; dimension < view.rank; ++dimension)
        f2c_buffer_printf(&result, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ",
                          view.lower[dimension]);
    f2c_buffer_append(&result, "}, .extent = {");
    for (dimension = 0U; dimension < view.rank; ++dimension)
        f2c_buffer_printf(&result, "%sf2c_descriptor_extent((size_t)(%s))",
                          dimension == 0U ? "" : ", ", view.extent[dimension]);
    f2c_buffer_append(&result, "}, .stride = {");
    for (dimension = 0U; dimension < view.rank; ++dimension)
        f2c_buffer_printf(&result, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ",
                          view.stride[dimension]);
    f2c_buffer_printf(&result, "}, .character_length = (size_t)(%s)})",
                      character_length != NULL ? character_length : "0U");
    free(character_length);
    f2c_descriptor_view_free(&view);
    return f2c_buffer_take(&result);
}

char *f2c_expression_descriptor_actual(Buffer *setup, Buffer *cleanup, Unit *unit,
                                       const F2cExpr *actual, F2cIntent intent, int *supported) {
    Buffer result = {0};
    char *source;
    size_t identifier;
    int character;
    int copy_in;
    int copy_out;
    actual = actual_value(actual);
    if (actual == NULL || !actual->has_contiguous_temporary)
        return emit_descriptor_actual(unit, actual, supported);
    if (actual->type == TYPE_DERIVED || actual->rank == 0U) {
        *supported = 0;
        return NULL;
    }
    source = emit_descriptor_actual(unit, actual, supported);
    if (!*supported || source == NULL) {
        free(source);
        return NULL;
    }
    identifier = actual->contiguous_temporary_index;
    character = actual->type == TYPE_CHARACTER;
    copy_in = intent != F2C_INTENT_OUT;
    copy_out = intent == F2C_INTENT_OUT || intent == F2C_INTENT_INOUT ||
               (intent == F2C_INTENT_UNSPECIFIED && actual->definable);
    f2c_buffer_printf(setup,
                      "f2c_contiguous_source_%zu = *(%s), "
                      "f2c_descriptor_prepare_contiguous(&f2c_contiguous_actual_%zu, "
                      "&f2c_contiguous_source_%zu, %s, %s), ",
                      identifier, source, identifier, identifier, character ? "true" : "false",
                      copy_in ? "true" : "false");
    f2c_buffer_printf(cleanup,
                      "f2c_descriptor_finish_contiguous(&f2c_contiguous_source_%zu, "
                      "&f2c_contiguous_actual_%zu, %s, %s), ",
                      identifier, identifier, character ? "true" : "false",
                      copy_out ? "true" : "false");
    f2c_buffer_printf(&result, "&f2c_contiguous_actual_%zu", identifier);
    free(source);
    return f2c_buffer_take(&result);
}

char *f2c_expression_wrap_contiguous_call(const F2cExpr *expression, int allocatable_result,
                                          Buffer *setup, Buffer *cleanup, char *call,
                                          int *supported) {
    Buffer result = {0};
    if (setup->data == NULL)
        return call;
    if (call == NULL || expression->rank != 0U || expression->type == TYPE_UNKNOWN ||
        expression->type == TYPE_DERIVED || allocatable_result ||
        expression->temporary_index == SIZE_MAX) {
        free(call);
        free(setup->data);
        free(cleanup->data);
        setup->data = NULL;
        cleanup->data = NULL;
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "(%s", setup->data);
    if (expression->type == TYPE_CHARACTER)
        f2c_buffer_printf(&result, "(void)(%s), %sf2c_character_result_%zu)", call,
                          cleanup->data != NULL ? cleanup->data : "", expression->temporary_index);
    else
        f2c_buffer_printf(&result,
                          "f2c_expression_result_%zu = (%s), %s"
                          "f2c_expression_result_%zu)",
                          expression->temporary_index, call,
                          cleanup->data != NULL ? cleanup->data : "", expression->temporary_index);
    free(call);
    free(setup->data);
    free(cleanup->data);
    setup->data = NULL;
    cleanup->data = NULL;
    return f2c_buffer_take(&result);
}
