#include "semantic/validation/private.h"

static int assumed_shape(const Symbol *dummy) {
    size_t dimension;
    for (dimension = 0U; dimension < dummy->rank; ++dimension)
        if (dummy->dimensions[dimension].kind == F2C_DIMENSION_ASSUMED_SHAPE)
            return 1;
    return 0;
}

static unsigned int object_qualifiers(const F2cExpr *actual) {
    unsigned int qualifiers;
    if (actual == NULL)
        return F2C_STORAGE_UNQUALIFIED;
    switch (actual->kind) {
    case F2C_EXPR_NAME:
    case F2C_EXPR_ARRAY_REFERENCE:
        return actual->storage_qualifiers | f2c_symbol_storage_qualifiers(actual->symbol);
    case F2C_EXPR_COMPONENT:
        qualifiers = actual->storage_qualifiers | f2c_symbol_storage_qualifiers(actual->symbol);
        return qualifiers | (actual->child_count != 0U ? object_qualifiers(actual->children[0])
                                                       : F2C_STORAGE_UNQUALIFIED);
    case F2C_EXPR_SUBSTRING:
        return object_qualifiers(f2c_substring_parent(actual));
    default:
        return F2C_STORAGE_UNQUALIFIED;
    }
}

void f2c_validation_actual_storage(Context *context, const Unit *definition, const Symbol *dummy,
                                   const F2cExpr *actual) {
    const char *procedure =
        definition->fortran_name != NULL ? definition->fortran_name : definition->name;
    const unsigned int dummy_qualifiers = f2c_symbol_storage_qualifiers(dummy);
    const int pointer = actual->symbol != NULL && actual->symbol->pointer &&
                        (actual->kind == F2C_EXPR_NAME ||
                         (actual->kind == F2C_EXPR_COMPONENT && actual->child_count == 1U));
    const int simply_contiguous = f2c_expression_is_simply_contiguous(actual);
    if (dummy->pointer && dummy->contiguous && actual->rank != 0U && !simply_contiguous) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual->span, 1,
                                 "actual for CONTIGUOUS pointer dummy '%s' of '%s' must be "
                                 "simply contiguous (C1541)",
                                 dummy->name, procedure);
    }
    if (dummy_qualifiers == F2C_STORAGE_UNQUALIFIED || dummy->value)
        return;
    if (!definition->elemental && f2c_expression_has_vector_subscript(actual)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual->span, 1,
                                 "vector-subscript actual cannot associate with ASYNCHRONOUS "
                                 "or VOLATILE dummy '%s' of '%s'",
                                 dummy->name, procedure);
        return;
    }
    if (actual->rank == 0U || object_qualifiers(actual) == F2C_STORAGE_UNQUALIFIED)
        return;
    if (pointer) {
        if (!actual->symbol->contiguous && !dummy->pointer &&
            (!assumed_shape(dummy) || dummy->contiguous)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual->span, 1,
                                     "qualified array pointer actual for dummy '%s' of '%s' "
                                     "requires a pointer or non-CONTIGUOUS assumed-shape dummy "
                                     "(C1540)",
                                     dummy->name, procedure);
        }
    } else if (!simply_contiguous && (!assumed_shape(dummy) || dummy->contiguous)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual->span, 1,
                                 "non-simply-contiguous ASYNCHRONOUS or VOLATILE actual for "
                                 "dummy '%s' of '%s' requires a non-CONTIGUOUS assumed-shape "
                                 "dummy (C1539)",
                                 dummy->name, procedure);
    }
}
