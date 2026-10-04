#include "semantic/intrinsic/extremum.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t keyword_index(const char *name) {
    size_t number = 0U;
    const unsigned char *digit;
    if (name == NULL || name[0] != 'a' || name[1] < '1' || name[1] > '9')
        return SIZE_MAX;
    for (digit = (const unsigned char *)name + 1U; *digit != '\0'; ++digit) {
        const size_t value = (size_t)(*digit - (unsigned char)'0');
        if (*digit < (unsigned char)'0' || *digit > (unsigned char)'9' ||
            number > (SIZE_MAX - value) / 10U)
            return SIZE_MAX;
        number = number * 10U + value;
    }
    return number - 1U;
}

static int compare_actuals(const void *left, const void *right) {
    const F2cExtremumActual *a = (const F2cExtremumActual *)left;
    const F2cExtremumActual *b = (const F2cExtremumActual *)right;
    if (a->index != b->index)
        return a->index < b->index ? -1 : 1;
    return a->source_order < b->source_order ? -1 : a->source_order > b->source_order ? 1 : 0;
}

static int fail_binding(F2cExtremumBinding *binding, F2cExtremumBindingError error,
                        const F2cExpr *offending, size_t index) {
    binding->error = error;
    binding->offending = offending;
    binding->error_index = index;
    return 0;
}

int f2c_extremum_bind(const F2cExpr *expression, F2cExtremumBinding *binding) {
    size_t positional = 0U;
    int saw_keyword = 0;
    if (binding == NULL)
        return 0;
    memset(binding, 0, sizeof(*binding));
    binding->values = binding->inline_values;
    if (expression == NULL || expression->children == NULL)
        return fail_binding(binding, F2C_EXTREMUM_BINDING_MISSING, expression, 0U);
    if (expression->child_count > F2C_EXTREMUM_INLINE_ARGUMENTS) {
        if (expression->child_count > SIZE_MAX / sizeof(*binding->values))
            return fail_binding(binding, F2C_EXTREMUM_BINDING_ALLOCATION, expression, 0U);
        binding->values =
            (F2cExtremumActual *)malloc(expression->child_count * sizeof(*binding->values));
        if (binding->values == NULL)
            return fail_binding(binding, F2C_EXTREMUM_BINDING_ALLOCATION, expression, 0U);
    }
    for (size_t i = 0U; i < expression->child_count; ++i) {
        const F2cExpr *actual = expression->children[i];
        const F2cExpr *value = actual;
        size_t index;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            saw_keyword = 1;
            index = keyword_index(actual->text);
            if (index == SIZE_MAX)
                return fail_binding(binding, F2C_EXTREMUM_BINDING_UNKNOWN_NAME, actual, 0U);
            value = actual->child_count == 1U ? actual->children[0] : NULL;
        } else {
            if (saw_keyword)
                return fail_binding(binding, F2C_EXTREMUM_BINDING_POSITIONAL_AFTER_KEYWORD, actual,
                                    0U);
            index = positional++;
        }
        if (value == NULL || value->kind == F2C_EXPR_ABSENT_ARGUMENT)
            return fail_binding(binding, F2C_EXTREMUM_BINDING_INVALID_VALUE, actual, index);
        binding->values[binding->count++] = (F2cExtremumActual){value, actual, index, i};
    }
    if (saw_keyword)
        qsort(binding->values, binding->count, sizeof(*binding->values), compare_actuals);
    for (size_t i = 1U; i < binding->count; ++i)
        if (binding->values[i - 1U].index == binding->values[i].index)
            return fail_binding(binding, F2C_EXTREMUM_BINDING_DUPLICATE, binding->values[i].actual,
                                binding->values[i].index);
    if (binding->count == 0U || binding->values[0].index != 0U)
        return fail_binding(binding, F2C_EXTREMUM_BINDING_MISSING, expression, 0U);
    if (binding->count < 2U || binding->values[1].index != 1U)
        return fail_binding(binding, F2C_EXTREMUM_BINDING_MISSING, expression, 1U);
    return 1;
}

void f2c_extremum_binding_clear(F2cExtremumBinding *binding) {
    if (binding == NULL)
        return;
    if (binding->values != binding->inline_values)
        free(binding->values);
    memset(binding, 0, sizeof(*binding));
}
