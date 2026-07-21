#include "codegen/expression/private.h"

#include "codegen/descriptor/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int emit_array_indices(Unit *unit, const F2cExpr *expression, char ***indices_out,
                              Type **types_out, int *supported) {
    char **indices = expression->child_count != 0U
                         ? (char **)calloc(expression->child_count, sizeof(*indices))
                         : NULL;
    Type *types = expression->child_count != 0U
                      ? (Type *)malloc(expression->child_count * sizeof(*types))
                      : NULL;
    size_t i;
    if (expression->symbol == NULL ||
        (expression->child_count != 0U && (indices == NULL || types == NULL))) {
        free(indices);
        free(types);
        *supported = 0;
        return 0;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        if (expression->children[i]->kind == F2C_EXPR_ARRAY_SECTION) {
            const F2cExpr *section = expression->children[i];
            if (section->child_count != 3U) {
                f2c_expression_free_arguments(indices, types, expression->child_count);
                *supported = 0;
                return 0;
            }
            if (section->children[0]->kind == F2C_EXPR_INVALID)
                indices[i] = i < expression->symbol->rank
                                 ? f2c_symbol_dimension_lower(unit, expression->symbol, i)
                                 : f2c_strdup("1");
            else
                indices[i] = f2c_expression_emit(unit, section->children[0], supported);
            types[i] = TYPE_INTEGER;
        } else {
            indices[i] = f2c_expression_emit(unit, expression->children[i], supported);
            types[i] = expression->children[i]->type;
        }
        if (!*supported || indices[i] == NULL) {
            f2c_expression_free_arguments(indices, types, expression->child_count);
            *supported = 0;
            return 0;
        }
    }
    *indices_out = indices;
    *types_out = types;
    return 1;
}

char *f2c_expression_emit_array_reference(Unit *unit, const F2cExpr *expression, int *supported) {
    char **indices = NULL;
    Type *types = NULL;
    char *result;
    if (!emit_array_indices(unit, expression, &indices, &types, supported))
        return NULL;
    result = f2c_emit_array_reference(unit, expression->symbol, indices, expression->child_count);
    f2c_expression_free_arguments(indices, types, expression->child_count);
    if (result == NULL)
        *supported = 0;
    return result;
}

char *f2c_emit_unaligned_designator_address(Unit *unit, const F2cExpr *expression, int *supported) {
    char **indices = NULL;
    Type *types = NULL;
    char *result;
    if (supported == NULL)
        return NULL;
    *supported = 1;
    if (expression == NULL || expression->symbol == NULL ||
        !expression->symbol->equivalence_unaligned ||
        (expression->kind != F2C_EXPR_NAME && expression->kind != F2C_EXPR_ARRAY_REFERENCE)) {
        *supported = 0;
        return NULL;
    }
    if (expression->kind == F2C_EXPR_NAME)
        return f2c_emit_unaligned_address(unit, expression->symbol, NULL, 0U);
    if (!emit_array_indices(unit, expression, &indices, &types, supported))
        return NULL;
    result = f2c_emit_unaligned_address(unit, expression->symbol, indices, expression->child_count);
    f2c_expression_free_arguments(indices, types, expression->child_count);
    if (result == NULL)
        *supported = 0;
    return result;
}

static char *emit_substring(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *selector;
    const F2cExpr *lower_expression;
    const F2cExpr *upper_expression;
    char *lower;
    char *upper = NULL;
    char *declared_length = NULL;
    Buffer result = {0};
    if (expression->symbol == NULL || expression->child_count != 1U) {
        *supported = 0;
        return NULL;
    }
    selector = expression->children[0];
    if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
        if (selector->child_count < 2U) {
            *supported = 0;
            return NULL;
        }
        lower_expression =
            selector->children[0]->kind == F2C_EXPR_INVALID ? NULL : selector->children[0];
        upper_expression = selector->children[1];
    } else {
        lower_expression = selector;
        upper_expression = NULL;
    }
    lower = lower_expression != NULL ? f2c_expression_emit(unit, lower_expression, supported)
                                     : f2c_strdup("1");
    if (!*supported || lower == NULL)
        return NULL;
    if (upper_expression != NULL && upper_expression->kind != F2C_EXPR_INVALID)
        upper = f2c_expression_emit(unit, upper_expression, supported);
    if (upper == NULL && selector->kind == F2C_EXPR_ARRAY_SECTION)
        upper = f2c_symbol_character_length(unit, expression->symbol);
    else if (upper == NULL)
        upper = f2c_strdup(lower);
    declared_length = f2c_symbol_character_length(unit, expression->symbol);
    if (!*supported || upper == NULL || declared_length == NULL) {
        free(lower);
        free(upper);
        free(declared_length);
        return NULL;
    }
    if (selector->kind == F2C_EXPR_ARRAY_SECTION)
        f2c_buffer_printf(&result,
                          "(&%s[f2c_substring_offset((size_t)(%s), (int64_t)(%s), "
                          "(int64_t)(%s))])",
                          f2c_symbol_c_name(unit, expression->symbol), declared_length, lower,
                          upper);
    else
        f2c_buffer_printf(&result,
                          "%s[f2c_substring_offset((size_t)(%s), (int64_t)(%s), "
                          "(int64_t)(%s))]",
                          f2c_symbol_c_name(unit, expression->symbol), declared_length, lower,
                          upper);
    free(lower);
    free(upper);
    free(declared_length);
    return f2c_buffer_take(&result);
}

static char *emit_array_constructor(Unit *unit, const F2cExpr *expression, int *supported) {
    char **elements = NULL;
    Type *types = NULL;
    Buffer result = {0};
    size_t i;
    if (!f2c_expression_children(unit, expression, &elements, &types)) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "(%s[%zu]){", f2c_expression_c_type(expression),
                      expression->child_count);
    for (i = 0U; i < expression->child_count; ++i)
        f2c_buffer_printf(&result, "%s%s", i == 0U ? "" : ", ", elements[i]);
    f2c_buffer_append(&result, "}");
    f2c_expression_free_arguments(elements, types, expression->child_count);
    return f2c_buffer_take(&result);
}

static char *emit_structure_constructor(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    unsigned char *assigned;
    size_t next_positional = 0U;
    size_t argument;
    if (expression->derived_type == NULL || expression->derived_type->c_name == NULL) {
        *supported = 0;
        return NULL;
    }
    assigned = expression->derived_type->component_count != 0U
                   ? (unsigned char *)calloc(expression->derived_type->component_count, 1U)
                   : NULL;
    if (expression->derived_type->component_count != 0U && assigned == NULL) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result,
                      "((%s){.f2c_type_tag = F2C_TYPE_ID_%s, .f2c_dynamic_size = sizeof(%s)",
                      expression->derived_type->c_name, expression->derived_type->c_name,
                      expression->derived_type->c_name);
    for (argument = 0U; argument < expression->child_count; ++argument) {
        const F2cExpr *actual = expression->children[argument];
        const F2cExpr *value = actual;
        size_t component = SIZE_MAX;
        char *code;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U) {
            size_t index;
            value = actual->children[0];
            for (index = 0U; index < expression->derived_type->component_count; ++index) {
                if (actual->text != NULL &&
                    strcmp(actual->text, expression->derived_type->components[index].name) == 0) {
                    component = index;
                    break;
                }
            }
        } else {
            while (next_positional < expression->derived_type->component_count &&
                   assigned[next_positional])
                ++next_positional;
            component = next_positional++;
        }
        if (component >= expression->derived_type->component_count || assigned[component]) {
            *supported = 0;
            break;
        }
        assigned[component] = 1U;
        code = f2c_expression_emit(unit, value, supported);
        if (!*supported || code == NULL) {
            free(code);
            break;
        }
        f2c_buffer_printf(&result, ", .%s = %s",
                          f2c_symbol_c_name(unit, &expression->derived_type->components[component]),
                          code);
        free(code);
    }
    f2c_buffer_append(&result, "})");
    free(assigned);
    if (!*supported) {
        free(result.data);
        return NULL;
    }
    return f2c_buffer_take(&result);
}

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    char *left;
    char *right;
    Type result_type;
    if (expression == NULL) {
        *supported = 0;
        return NULL;
    }
    if (expression->lowered_c != NULL)
        return f2c_strdup(expression->lowered_c);
    if (expression->resolved_procedure != NULL &&
        (expression->kind == F2C_EXPR_UNARY || expression->kind == F2C_EXPR_BINARY))
        return f2c_expression_call(unit, expression, supported);
    switch (expression->kind) {
    case F2C_EXPR_INTEGER_LITERAL:
        if (expression->text != NULL &&
            ((tolower((unsigned char)expression->text[0]) == 'b' ||
              tolower((unsigned char)expression->text[0]) == 'o' ||
              tolower((unsigned char)expression->text[0]) == 'z' ||
              tolower((unsigned char)expression->text[0]) == 'x') &&
             (expression->text[1] == '\'' || expression->text[1] == '"'))) {
            char *boz = f2c_expression_boz_literal(expression->text);
            if (boz == NULL)
                *supported = 0;
            return boz;
        }
        return f2c_expression_integer_literal(expression);
    case F2C_EXPR_REAL_LITERAL:
        return f2c_expression_real_literal(expression);
    case F2C_EXPR_STRING_LITERAL:
        return f2c_expression_string_literal(expression->text);
    case F2C_EXPR_LOGICAL_LITERAL:
        return f2c_strdup(strcmp(expression->text, ".true.") == 0 ? "true" : "false");
    case F2C_EXPR_NAME:
        return f2c_expression_name(unit, expression, supported);
    case F2C_EXPR_COMPONENT:
        if (expression->child_count < 1U || expression->symbol == NULL) {
            *supported = 0;
            return NULL;
        }
        left = f2c_expression_emit(unit, expression->children[0], supported);
        if (!*supported || left == NULL)
            return NULL;
        if (expression->children[0]->kind == F2C_EXPR_NAME &&
            expression->children[0]->symbol != NULL &&
            expression->children[0]->symbol->polymorphic &&
            expression->children[0]->derived_type != NULL &&
            expression->children[0]->symbol->derived_type !=
                expression->children[0]->derived_type) {
            Buffer cast = {0};
            f2c_buffer_printf(&cast, "(*((%s *)&(%s)))",
                              expression->children[0]->derived_type->c_name, left);
            free(left);
            left = f2c_buffer_take(&cast);
        }
        if (expression->child_count > 1U && expression->symbol->rank != 0U) {
            char *indices[F2C_MAX_RANK] = {0};
            char *designator = NULL;
            size_t selector;
            if (expression->child_count != expression->symbol->rank + 1U) {
                free(left);
                *supported = 0;
                return NULL;
            }
            for (selector = 0U; selector < expression->symbol->rank; ++selector) {
                const F2cExpr *index = expression->children[selector + 1U];
                if (index == NULL || index->kind == F2C_EXPR_ARRAY_SECTION || index->rank != 0U) {
                    *supported = 0;
                    break;
                }
                indices[selector] = f2c_expression_emit(unit, index, supported);
                if (!*supported || indices[selector] == NULL)
                    break;
            }
            if (*supported)
                designator = f2c_descriptor_element_designator(unit, expression, indices,
                                                               expression->symbol->rank);
            for (selector = 0U; selector < expression->symbol->rank; ++selector)
                free(indices[selector]);
            free(left);
            if (!*supported || designator == NULL) {
                free(designator);
                *supported = 0;
                return NULL;
            }
            return designator;
        } else if (expression->symbol->pointer && expression->symbol->rank == 0U) {
            f2c_buffer_append(&result, "(*(");
            f2c_expression_append_component(&result, left, expression->children[0]->derived_type,
                                            expression->symbol);
            f2c_buffer_append(&result, "))");
        } else {
            f2c_expression_append_component(&result, left, expression->children[0]->derived_type,
                                            expression->symbol);
        }
        free(left);
        return f2c_buffer_take(&result);
    case F2C_EXPR_UNARY:
        left = f2c_expression_emit(unit, expression->children[0], supported);
        if (!*supported || left == NULL)
            return NULL;
        if ((expression->type == TYPE_COMPLEX || expression->type == TYPE_DOUBLE_COMPLEX) &&
            strcmp(expression->text, "-") == 0) {
            f2c_buffer_printf(&result, "%s(%s)",
                              expression->type == TYPE_COMPLEX ? "f2c_cneg" : "f2c_zneg", left);
        } else {
            f2c_buffer_printf(&result, "(%s(%s))",
                              strcmp(expression->text, ".not.") == 0 ? "!" : expression->text,
                              left);
        }
        free(left);
        return f2c_buffer_take(&result);
    case F2C_EXPR_BINARY:
        left = f2c_expression_emit(unit, expression->children[0], supported);
        right = *supported ? f2c_expression_emit(unit, expression->children[1], supported) : NULL;
        if (!*supported || left == NULL || right == NULL) {
            free(left);
            free(right);
            return NULL;
        }
        {
            char *binary = f2c_emit_character_concatenation(unit, expression, left, right);
            if (binary == NULL)
                binary =
                    f2c_emit_character_comparison(unit, expression->children[0], left,
                                                  expression->text, expression->children[1], right);
            if (binary == NULL)
                binary =
                    f2c_emit_binary(unit, left, expression->children[0]->type, expression->text,
                                    right, expression->children[1]->type, &result_type);
            free(left);
            free(right);
            return binary;
        }
    case F2C_EXPR_CALL:
        return f2c_expression_call(unit, expression, supported);
    case F2C_EXPR_ARRAY_REFERENCE:
        return f2c_expression_emit_array_reference(unit, expression, supported);
    case F2C_EXPR_SUBSTRING:
        return emit_substring(unit, expression, supported);
    case F2C_EXPR_COMPLEX_LITERAL:
        left = f2c_expression_emit(unit, expression->children[0], supported);
        right = *supported ? f2c_expression_emit(unit, expression->children[1], supported) : NULL;
        if (!*supported || left == NULL || right == NULL) {
            free(left);
            free(right);
            return NULL;
        }
        f2c_buffer_printf(&result, "%s((%s)(%s), (%s)(%s))",
                          expression->type == TYPE_DOUBLE_COMPLEX ? "f2c_make_z" : "f2c_make_c",
                          expression->type == TYPE_DOUBLE_COMPLEX ? "double" : "float", left,
                          expression->type == TYPE_DOUBLE_COMPLEX ? "double" : "float", right);
        free(left);
        free(right);
        return f2c_buffer_take(&result);
    case F2C_EXPR_ARRAY_CONSTRUCTOR:
        return emit_array_constructor(unit, expression, supported);
    case F2C_EXPR_STRUCTURE_CONSTRUCTOR:
        return emit_structure_constructor(unit, expression, supported);
    case F2C_EXPR_IMPLIED_DO:
        *supported = 0;
        return NULL;
    case F2C_EXPR_KEYWORD_ARGUMENT:
        if (expression->child_count != 1U) {
            *supported = 0;
            return NULL;
        }
        return f2c_expression_emit(unit, expression->children[0], supported);
    case F2C_EXPR_ABSENT_ARGUMENT:
        return f2c_strdup("NULL");
    case F2C_EXPR_ARRAY_SECTION:
    case F2C_EXPR_INVALID:
    default:
        *supported = 0;
        return NULL;
    }
}

char *f2c_emit_pointer_designator(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    char *base;
    if (supported != NULL)
        *supported = 1;
    if (expression == NULL || expression->symbol == NULL || !expression->symbol->pointer) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    if (expression->kind == F2C_EXPR_NAME)
        return f2c_strdup(f2c_symbol_c_name(unit, expression->symbol));
    if (expression->kind != F2C_EXPR_COMPONENT || expression->child_count != 1U) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    base = f2c_expression_emit(unit, expression->children[0], supported);
    if (base == NULL || (supported != NULL && !*supported)) {
        free(base);
        return NULL;
    }
    f2c_expression_append_component(&result, base, expression->children[0]->derived_type,
                                    expression->symbol);
    free(base);
    return f2c_buffer_take(&result);
}

char *f2c_emit_expression_ast(Unit *unit, const F2cExpr *expression, int *supported) {
    int local_supported = 1;
    char *result = f2c_expression_emit(unit, expression, &local_supported);
    if (supported != NULL)
        *supported = local_supported;
    if (!local_supported) {
        free(result);
        return NULL;
    }
    return result;
}

char *f2c_emit_typed_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 1;
    char *result;
    if (expression == NULL || expression->parse_error_offset != SIZE_MAX)
        return NULL;
    result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}
