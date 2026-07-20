#include "semantic/semantic.h"

#include <stdlib.h>
#include <string.h>

int f2c_symbol_resize_external_parameters(Symbol *symbol, size_t count) {
    Type *types = NULL;
    int *kinds = NULL;
    size_t *ranks = NULL;
    F2cIntent *intents = NULL;
    int *optional = NULL;
    int *allocatable = NULL;
    int *pointer = NULL;
    int *contiguous = NULL;
    int *descriptor = NULL;
    F2cDerivedType **derived_types = NULL;
    int *polymorphic = NULL;
    Symbol **procedures = NULL;
    int *constant = NULL;
    size_t copy_count;
    if (symbol == NULL)
        return 0;
    if (count == symbol->external_parameter_capacity)
        return 1;
    if (count != 0U) {
        if (count > SIZE_MAX / sizeof(*types) || count > SIZE_MAX / sizeof(*kinds) ||
            count > SIZE_MAX / sizeof(*ranks) || count > SIZE_MAX / sizeof(*intents) ||
            count > SIZE_MAX / sizeof(*optional) || count > SIZE_MAX / sizeof(*allocatable) ||
            count > SIZE_MAX / sizeof(*pointer) || count > SIZE_MAX / sizeof(*contiguous) ||
            count > SIZE_MAX / sizeof(*descriptor) || count > SIZE_MAX / sizeof(*derived_types) ||
            count > SIZE_MAX / sizeof(*polymorphic) || count > SIZE_MAX / sizeof(*procedures) ||
            count > SIZE_MAX / sizeof(*constant))
            return 0;
        types = (Type *)calloc(count, sizeof(*types));
        kinds = (int *)calloc(count, sizeof(*kinds));
        ranks = (size_t *)calloc(count, sizeof(*ranks));
        intents = (F2cIntent *)calloc(count, sizeof(*intents));
        optional = (int *)calloc(count, sizeof(*optional));
        allocatable = (int *)calloc(count, sizeof(*allocatable));
        pointer = (int *)calloc(count, sizeof(*pointer));
        contiguous = (int *)calloc(count, sizeof(*contiguous));
        descriptor = (int *)calloc(count, sizeof(*descriptor));
        derived_types = (F2cDerivedType **)calloc(count, sizeof(*derived_types));
        polymorphic = (int *)calloc(count, sizeof(*polymorphic));
        procedures = (Symbol **)calloc(count, sizeof(*procedures));
        constant = (int *)calloc(count, sizeof(*constant));
        if (types == NULL || kinds == NULL || ranks == NULL || intents == NULL ||
            optional == NULL || allocatable == NULL || pointer == NULL || contiguous == NULL ||
            descriptor == NULL || derived_types == NULL || polymorphic == NULL ||
            procedures == NULL || constant == NULL)
            goto failed;
        copy_count =
            symbol->external_parameter_count < count ? symbol->external_parameter_count : count;
        if (copy_count != 0U) {
            memcpy(types, symbol->external_parameter_types, copy_count * sizeof(*types));
            memcpy(kinds, symbol->external_parameter_kinds, copy_count * sizeof(*kinds));
            memcpy(ranks, symbol->external_parameter_ranks, copy_count * sizeof(*ranks));
            memcpy(intents, symbol->external_parameter_intents, copy_count * sizeof(*intents));
            memcpy(optional, symbol->external_parameter_optional, copy_count * sizeof(*optional));
            memcpy(allocatable, symbol->external_parameter_allocatable,
                   copy_count * sizeof(*allocatable));
            memcpy(pointer, symbol->external_parameter_pointer, copy_count * sizeof(*pointer));
            memcpy(contiguous, symbol->external_parameter_contiguous,
                   copy_count * sizeof(*contiguous));
            memcpy(descriptor, symbol->external_parameter_descriptor,
                   copy_count * sizeof(*descriptor));
            memcpy(derived_types, symbol->external_parameter_derived_types,
                   copy_count * sizeof(*derived_types));
            memcpy(polymorphic, symbol->external_parameter_polymorphic,
                   copy_count * sizeof(*polymorphic));
            memcpy(procedures, symbol->external_parameter_procedures,
                   copy_count * sizeof(*procedures));
            memcpy(constant, symbol->external_parameter_const, copy_count * sizeof(*constant));
        }
    }
    free(symbol->external_parameter_types);
    free(symbol->external_parameter_kinds);
    free(symbol->external_parameter_ranks);
    free(symbol->external_parameter_intents);
    free(symbol->external_parameter_optional);
    free(symbol->external_parameter_allocatable);
    free(symbol->external_parameter_pointer);
    free(symbol->external_parameter_contiguous);
    free(symbol->external_parameter_descriptor);
    free(symbol->external_parameter_derived_types);
    free(symbol->external_parameter_polymorphic);
    free(symbol->external_parameter_procedures);
    free(symbol->external_parameter_const);
    symbol->external_parameter_types = types;
    symbol->external_parameter_kinds = kinds;
    symbol->external_parameter_ranks = ranks;
    symbol->external_parameter_intents = intents;
    symbol->external_parameter_optional = optional;
    symbol->external_parameter_allocatable = allocatable;
    symbol->external_parameter_pointer = pointer;
    symbol->external_parameter_contiguous = contiguous;
    symbol->external_parameter_descriptor = descriptor;
    symbol->external_parameter_derived_types = derived_types;
    symbol->external_parameter_polymorphic = polymorphic;
    symbol->external_parameter_procedures = procedures;
    symbol->external_parameter_const = constant;
    symbol->external_parameter_capacity = count;
    if (symbol->external_parameter_count > count)
        symbol->external_parameter_count = count;
    return 1;

failed:
    free(types);
    free(kinds);
    free(ranks);
    free(intents);
    free(optional);
    free(allocatable);
    free(pointer);
    free(contiguous);
    free(descriptor);
    free(derived_types);
    free(polymorphic);
    free(procedures);
    free(constant);
    return 0;
}
