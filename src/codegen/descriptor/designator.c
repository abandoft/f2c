#include "codegen/descriptor/private.h"

#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

char *f2c_descriptor_section_address(Unit *unit, const F2cExpr *expression, const char *element,
                                     const F2cDescriptorView *view) {
    Buffer address = {0};
    F2cStorageReference storage = f2c_ir_storage_reference(expression);
    storage.object_qualifiers = F2C_STORAGE_UNQUALIFIED;
    char *base = f2c_storage_read_property(unit, &storage, F2C_OBJECT_DATA, 0U);
    if (element == NULL || base == NULL || view->rank == 0U) {
        free(base);
        return NULL;
    }
    f2c_buffer_append(&address, "(");
    for (size_t dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(&address, "%s(size_t)(%s) == 0U", dimension == 0U ? "" : " || ",
                          view->extent[dimension]);
    /* An empty section has no first element. Preserve target identity without
     * checking an unselected subscript or forming an out-of-range address. */
    f2c_buffer_printf(&address, " ? (%s) : (&%s))", base, element);
    free(base);
    return f2c_buffer_take(&address);
}

static int component_designator_supported(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_COMPONENT &&
           expression->child_count != 0U && expression->children[0] != NULL &&
           expression->children[0]->rank == 0U && expression->symbol != NULL;
}

size_t f2c_descriptor_selector_offset(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_COMPONENT ? 1U : 0U;
}

const F2cExpr *f2c_descriptor_selector(const F2cExpr *expression, size_t dimension) {
    const size_t offset = f2c_descriptor_selector_offset(expression);
    if (expression == NULL || offset + dimension >= expression->child_count)
        return NULL;
    return expression->children[offset + dimension];
}

char *f2c_descriptor_storage_designator(Unit *unit, const F2cExpr *expression) {
    Buffer result = {0};
    char *base;
    int supported = 0;
    if (expression == NULL || expression->symbol == NULL)
        return NULL;
    if (expression->kind == F2C_EXPR_NAME || expression->kind == F2C_EXPR_ARRAY_REFERENCE)
        return f2c_strdup(f2c_symbol_c_name(unit, expression->symbol));
    if (!component_designator_supported(expression))
        return NULL;
    base = f2c_expression_storage_designator(unit, expression->children[0], &supported);
    if (!supported || base == NULL) {
        free(base);
        return NULL;
    }
    f2c_expression_append_component(&result, base, expression->children[0]->derived_type,
                                    expression->symbol);
    free(base);
    return f2c_buffer_take(&result);
}

static char *metadata_designator(Unit *unit, const F2cExpr *expression,
                                 F2cObjectStateProperty field, size_t dimension) {
    const F2cStorageReference reference = f2c_ir_storage_reference(expression);
    return f2c_storage_read_property(unit, &reference, field, dimension);
}

char *f2c_descriptor_dimension_lower(Unit *unit, const F2cExpr *expression, size_t dimension) {
    const Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (component_designator_supported(expression) && (symbol->pointer || symbol->allocatable))
        return metadata_designator(unit, expression, F2C_OBJECT_LOWER, dimension);
    return f2c_symbol_dimension_lower(unit, symbol, dimension);
}

char *f2c_descriptor_dimension_extent(Unit *unit, const F2cExpr *expression, size_t dimension) {
    const Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (component_designator_supported(expression) && (symbol->pointer || symbol->allocatable))
        return metadata_designator(unit, expression, F2C_OBJECT_EXTENT, dimension);
    return f2c_symbol_dimension_extent(unit, symbol, dimension);
}

char *f2c_descriptor_dimension_upper(Unit *unit, const F2cExpr *expression, size_t dimension) {
    const Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    Buffer result = {0};
    char *lower;
    char *extent;
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (!component_designator_supported(expression) || (!symbol->pointer && !symbol->allocatable))
        return f2c_symbol_dimension_upper(unit, symbol, dimension);
    lower = f2c_descriptor_dimension_lower(unit, expression, dimension);
    extent = f2c_descriptor_dimension_extent(unit, expression, dimension);
    if (lower == NULL || extent == NULL) {
        free(lower);
        free(extent);
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s) + (int64_t)(%s) - 1)", lower, extent);
    free(lower);
    free(extent);
    return f2c_buffer_take(&result);
}

static char *contiguous_stride(Unit *unit, const F2cExpr *expression, size_t dimension) {
    char *stride = f2c_descriptor_expression_record_size(unit, expression);
    if (stride != NULL) {
        Buffer checked = {0};
        f2c_buffer_printf(&checked, "f2c_descriptor_record_stride((size_t)(%s))", stride);
        free(stride);
        stride = f2c_buffer_take(&checked);
    }
    size_t prior;
    for (prior = 0U; stride != NULL && prior < dimension; ++prior) {
        Buffer next = {0};
        char *extent = f2c_descriptor_dimension_extent(unit, expression, prior);
        if (extent == NULL) {
            free(stride);
            return NULL;
        }
        f2c_buffer_printf(&next, "f2c_descriptor_stride_extent((ptrdiff_t)(%s), (size_t)(%s))",
                          stride, extent);
        free(stride);
        free(extent);
        stride = f2c_buffer_take(&next);
    }
    return stride;
}

char *f2c_descriptor_expression_byte_stride(Unit *unit, const F2cExpr *expression,
                                            size_t dimension) {
    const Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    if (component_designator_supported(expression) && symbol->pointer)
        return metadata_designator(unit, expression, F2C_OBJECT_BYTE_STRIDE, dimension);
    if (component_designator_supported(expression))
        return contiguous_stride(unit, expression, dimension);
    return f2c_descriptor_source_byte_stride(unit, symbol, dimension);
}

static char *component_offset(Unit *unit, const F2cExpr *expression, char **indices, size_t count) {
    const Symbol *symbol = expression->symbol;
    Buffer result = {0};
    size_t dimension;
    if (symbol->pointer) {
        f2c_buffer_printf(&result, "f2c_array_descriptor_byte_offset(%zuU, (const int64_t[]){",
                          count);
        for (dimension = 0U; dimension < count; ++dimension)
            f2c_buffer_printf(&result, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ",
                              indices[dimension]);
        f2c_buffer_append(&result, "}, (const int64_t[]){");
        for (dimension = 0U; dimension < count; ++dimension) {
            char *lower = f2c_descriptor_dimension_lower(unit, expression, dimension);
            f2c_buffer_printf(&result, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ",
                              lower != NULL ? lower : "1");
            free(lower);
        }
        f2c_buffer_append(&result, "}, (const size_t[]){");
        for (dimension = 0U; dimension < count; ++dimension) {
            char *extent = f2c_descriptor_dimension_extent(unit, expression, dimension);
            f2c_buffer_printf(&result, "%s(size_t)(%s)", dimension == 0U ? "" : ", ",
                              extent != NULL ? extent : "0");
            free(extent);
        }
        f2c_buffer_append(&result, "}, (const ptrdiff_t[]){");
        for (dimension = 0U; dimension < count; ++dimension) {
            char *stride = f2c_descriptor_expression_byte_stride(unit, expression, dimension);
            f2c_buffer_printf(&result, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ",
                              stride != NULL ? stride : "0");
            free(stride);
        }
        f2c_buffer_append(&result, "})");
        return f2c_buffer_take(&result);
    }
    for (dimension = 0U; dimension < count; ++dimension) {
        char *lower = f2c_descriptor_dimension_lower(unit, expression, dimension);
        if (dimension != 0U) {
            size_t prior;
            f2c_buffer_append(&result, " + (");
            for (prior = 0U; prior < dimension; ++prior) {
                char *extent = f2c_descriptor_dimension_extent(unit, expression, prior);
                f2c_buffer_printf(&result, "%s(int64_t)(%s)", prior == 0U ? "" : " * ",
                                  extent != NULL ? extent : "0");
                free(extent);
            }
            f2c_buffer_append(&result, ") * ");
        }
        f2c_buffer_printf(&result, "(((int64_t)(%s)) - (int64_t)(%s))", indices[dimension],
                          lower != NULL ? lower : "1");
        free(lower);
    }
    return f2c_buffer_take(&result);
}

char *f2c_descriptor_element_designator(Unit *unit, const F2cExpr *expression, char **indices,
                                        size_t count) {
    const Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    Buffer result = {0};
    char *storage;
    char *offset;
    char *character_length = NULL;
    if (symbol == NULL || count != symbol->rank || indices == NULL)
        return NULL;
    if (!component_designator_supported(expression))
        return f2c_emit_array_reference(unit, expression->symbol, indices, count);
    F2cStorageReference reference = f2c_ir_storage_reference(expression);
    /* Taking an address does not perform a scoped access. */
    reference.object_qualifiers = F2C_STORAGE_UNQUALIFIED;
    storage = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
    offset = component_offset(unit, expression, indices, count);
    if (symbol->type == TYPE_CHARACTER) {
        if (symbol->deferred_character) {
            character_length =
                f2c_storage_read_property(unit, &reference, F2C_OBJECT_CHARACTER_LENGTH, 0U);
        } else {
            character_length = f2c_symbol_character_length(unit, symbol);
        }
    }
    if (storage == NULL || offset == NULL ||
        (symbol->type == TYPE_CHARACTER && character_length == NULL)) {
        free(storage);
        free(offset);
        free(character_length);
        return NULL;
    }
    if (!symbol->pointer) {
        char *size = f2c_descriptor_expression_record_size(unit, expression);
        if (size == NULL) {
            free(storage);
            free(offset);
            free(character_length);
            return NULL;
        }
        f2c_buffer_printf(&result, "f2c_descriptor_stride_extent((ptrdiff_t)(%s), (size_t)(%s))",
                          offset, size);
        free(size);
        free(offset);
        offset = f2c_buffer_take(&result);
    }
    char *element = f2c_storage_byte_element(&reference, storage, offset);
    free(storage);
    free(offset);
    free(character_length);
    return element;
}
