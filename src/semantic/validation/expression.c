#include "semantic/validation/private.h"

#include "ast/declaration/designator.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void validate_present_intrinsic(Context *context, Unit *unit, size_t line,
                                       const char *statement_text, F2cExpr *expression) {
    F2cExpr *argument;
    Symbol *symbol;
    if (expression->child_count != 1U) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_column(statement_text, expression), 1,
                          "PRESENT requires exactly one OPTIONAL dummy argument");
        return;
    }
    argument = expression->children[0];
    symbol = argument != NULL && argument->kind == F2C_EXPR_NAME ? argument->symbol : NULL;
    if (symbol == NULL || !symbol->argument || !symbol->optional) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "PRESENT argument must be an OPTIONAL dummy argument");
    }
    (void)unit;
}

static void validate_allocated_intrinsic(Context *context, size_t line, const char *statement_text,
                                         F2cExpr *expression) {
    F2cExpr *argument = expression->child_count == 1U ? expression->children[0] : NULL;
    Symbol *symbol = argument != NULL && (argument->kind == F2C_EXPR_NAME ||
                                          argument->kind == F2C_EXPR_COMPONENT)
                         ? argument->symbol
                         : NULL;
    if (symbol == NULL || !symbol->allocatable) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "ALLOCATED requires a whole ALLOCATABLE object");
    }
}

static void validate_associated_intrinsic(Context *context, size_t line, const char *statement_text,
                                          F2cExpr *expression) {
    F2cExpr *pointer = expression->child_count >= 1U ? expression->children[0] : NULL;
    F2cExpr *target = expression->child_count >= 2U ? expression->children[1] : NULL;
    Symbol *pointer_symbol =
        pointer != NULL && (pointer->kind == F2C_EXPR_NAME || pointer->kind == F2C_EXPR_COMPONENT)
            ? pointer->symbol
            : NULL;
    Symbol *target_symbol = target != NULL && target->kind == F2C_EXPR_NAME ? target->symbol : NULL;
    if (pointer_symbol == NULL ||
        (!pointer_symbol->pointer && !pointer_symbol->procedure_pointer)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, pointer), 1,
                          "ASSOCIATED first argument must be a whole POINTER object");
        return;
    }
    if (pointer_symbol->procedure_pointer) {
        if (pointer == NULL ||
            (pointer->kind != F2C_EXPR_NAME && pointer->kind != F2C_EXPR_COMPONENT)) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, pointer), 1,
                              "ASSOCIATED procedure pointer must be a whole object");
        } else if (target != NULL && (target_symbol == NULL || !target_symbol->external ||
                                      !f2c_validation_procedure_signatures_compatible(
                                          pointer_symbol, target_symbol, 0U))) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, target), 1,
                              "ASSOCIATED target must be a procedure with a compatible explicit "
                              "interface");
        }
        return;
    }
    if (target != NULL &&
        (target_symbol == NULL || (!target_symbol->target && !target_symbol->pointer) ||
         target_symbol->type != pointer_symbol->type ||
         target_symbol->kind != pointer_symbol->kind ||
         target_symbol->rank != pointer_symbol->rank)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, target), 1,
                          "ASSOCIATED target must be a compatible TARGET or POINTER object");
    }
}

static void validate_intrinsic_arity(Context *context, size_t line, const char *statement_text,
                                     const F2cExpr *expression) {
    const F2cIntrinsicSignature *signature =
        expression != NULL ? f2c_find_intrinsic(expression->text) : NULL;
    if (signature == NULL || (expression->child_count >= signature->minimum_arguments &&
                              expression->child_count <= signature->maximum_arguments))
        return;
    if (signature->minimum_arguments == signature->maximum_arguments) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, expression), 1,
            "%s requires exactly %zu argument%s", signature->name, signature->minimum_arguments,
            signature->minimum_arguments == 1U ? "" : "s");
    } else {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, expression), 1,
                          "%s requires between %zu and %zu arguments", signature->name,
                          signature->minimum_arguments, signature->maximum_arguments);
    }
}

static const F2cExpr *inquiry_argument(const F2cExpr *call, const char *keyword, size_t position) {
    size_t positional = 0U;
    size_t argument;
    if (call == NULL)
        return NULL;
    for (argument = 0U; argument < call->child_count; ++argument) {
        const F2cExpr *actual = call->children[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return actual->child_count == 1U ? actual->children[0] : NULL;
        } else if (positional++ == position) {
            return actual;
        }
    }
    return NULL;
}

static void validate_matrix_intrinsic(Context *context, size_t line, const char *statement_text,
                                      const F2cExpr *expression) {
    const int transpose = expression->text != NULL && strcmp(expression->text, "transpose") == 0;
    const F2cExpr *left = inquiry_argument(expression, transpose ? "matrix" : "matrix_a", 0U);
    const F2cExpr *right = transpose ? NULL : inquiry_argument(expression, "matrix_b", 1U);
    size_t argument;
    for (argument = 0U; argument < expression->child_count; ++argument) {
        const F2cExpr *actual = expression->children[argument];
        if (actual == NULL || actual->kind != F2C_EXPR_KEYWORD_ARGUMENT || actual->text == NULL)
            continue;
        if ((transpose && strcmp(actual->text, "matrix") != 0) ||
            (!transpose && strcmp(actual->text, "matrix_a") != 0 &&
             strcmp(actual->text, "matrix_b") != 0))
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, actual), 1,
                              "%s has no argument named '%s'", expression->text, actual->text);
    }
    if (transpose) {
        if (left == NULL || left->rank != 2U)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, left), 1,
                              "TRANSPOSE requires a rank-two array argument");
        return;
    }
    if (left == NULL || left->rank < 1U || left->rank > 2U)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, left), 1,
                          "MATMUL MATRIX_A must have rank one or two");
    if (right == NULL || right->rank < 1U || right->rank > 2U)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, right), 1,
                          "MATMUL MATRIX_B must have rank one or two");
    if (left == NULL || right == NULL)
        return;
    if (left->rank == 1U && right->rank == 1U)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, expression), 1,
                          "MATMUL does not accept two rank-one operands; use DOT_PRODUCT");
    if (!((left->type == TYPE_LOGICAL && right->type == TYPE_LOGICAL) ||
          (f2c_type_is_numeric(left->type) && f2c_type_is_numeric(right->type))))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, expression), 1,
                          "MATMUL operands must both be numeric or both be LOGICAL");
    if (left->rank >= 1U && left->rank <= 2U && right->rank >= 1U && right->rank <= 2U &&
        left->shape.dimensions[left->rank - 1U].extent_known &&
        right->shape.dimensions[0].extent_known &&
        left->shape.dimensions[left->rank - 1U].extent != right->shape.dimensions[0].extent)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, expression), 1,
                          "MATMUL inner extents are not conformable (%llu and %llu)",
                          (unsigned long long)left->shape.dimensions[left->rank - 1U].extent,
                          (unsigned long long)right->shape.dimensions[0].extent);
}

static void validate_array_inquiry(Context *context, Unit *unit, size_t line,
                                   const char *statement_text, const F2cExpr *expression) {
    const int shape = expression->text != NULL && strcmp(expression->text, "shape") == 0;
    const F2cExpr *array = inquiry_argument(expression, shape ? "source" : "array", 0U);
    const F2cExpr *dimension = shape ? NULL : inquiry_argument(expression, "dim", 1U);
    const F2cExpr *kind = inquiry_argument(expression, "kind", shape ? 1U : 2U);
    int64_t value;
    size_t argument;
    for (argument = 0U; argument < expression->child_count; ++argument) {
        const F2cExpr *actual = expression->children[argument];
        if (actual == NULL || actual->kind != F2C_EXPR_KEYWORD_ARGUMENT || actual->text == NULL)
            continue;
        if (strcmp(actual->text, shape ? "source" : "array") != 0 &&
            (!shape && strcmp(actual->text, "dim") != 0) && strcmp(actual->text, "kind") != 0)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, actual), 1,
                              "%s has no argument named '%s'", expression->text, actual->text);
    }
    if (array == NULL || array->rank == 0U)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, array), 1,
                          "%s requires a non-scalar array argument", expression->text);
    if (dimension != NULL && (dimension->type != TYPE_INTEGER || dimension->rank != 0U)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, dimension), 1,
                          "DIM in %s must be a scalar INTEGER expression", expression->text);
    } else if (dimension != NULL && array != NULL &&
               f2c_evaluate_integer_constant(unit, dimension, &value) &&
               (value < 1 || (uint64_t)value > array->rank)) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, dimension), 1,
            "DIM in %s must be between 1 and array rank %zu", expression->text, array->rank);
    }
    if (kind != NULL && (kind->type != TYPE_INTEGER || kind->rank != 0U ||
                         !f2c_evaluate_integer_constant(unit, kind, &value) ||
                         (value != 1 && value != 2 && value != 4 && value != 8)))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, kind), 1,
                          "KIND in %s must be a supported scalar INTEGER constant (1, 2, 4, or 8)",
                          expression->text);
}

static void validate_substring_semantics(Context *context, Unit *unit, size_t line,
                                         const char *statement_text, const F2cExpr *expression) {
    const F2cExpr *selector;
    const F2cExpr *lower = NULL;
    const F2cExpr *upper = NULL;
    const F2cExpr *stride = NULL;
    int64_t lower_value = 1;
    int64_t upper_value = 0;
    int64_t length_value = 0;
    int lower_known = 1;
    int upper_known = 0;
    int length_known = 0;
    if (expression == NULL || expression->kind != F2C_EXPR_SUBSTRING)
        return;
    if (expression->symbol == NULL || expression->child_count != 1U) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, expression), 1,
                          "malformed CHARACTER substring designator");
        return;
    }
    selector = expression->children[0];
    if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
        if (selector->child_count != 3U) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, selector), 1,
                              "malformed CHARACTER substring range");
            return;
        }
        if (selector->children[0]->kind != F2C_EXPR_INVALID)
            lower = selector->children[0];
        if (selector->children[1]->kind != F2C_EXPR_INVALID)
            upper = selector->children[1];
        if (selector->children[2]->kind != F2C_EXPR_INVALID)
            stride = selector->children[2];
    } else {
        lower = selector;
        upper = selector;
    }
    if (stride != NULL) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, stride), 1,
                          "CHARACTER substring range cannot have a stride");
    }
    if (lower != NULL && (lower->type != TYPE_INTEGER || lower->rank != 0U)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, lower), 1,
                          "CHARACTER substring lower bound must be a scalar INTEGER");
    }
    if (upper != NULL && (upper->type != TYPE_INTEGER || upper->rank != 0U)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, upper), 1,
                          "CHARACTER substring upper bound must be a scalar INTEGER");
    }
    length_known = expression->symbol->character_length_expression != NULL
                       ? f2c_evaluate_integer_constant(
                             unit, expression->symbol->character_length_expression, &length_value)
                   : expression->symbol->character_length_syntax.count != 0U
                       ? f2c_evaluate_integer_syntax(
                             unit, expression->symbol->character_length_syntax, &length_value)
                   : expression->symbol->character_length == NULL ||
                           strcmp(expression->symbol->character_length, "1") == 0
                       ? (length_value = 1, 1)
                       : 0;
    if (lower != NULL)
        lower_known = f2c_evaluate_integer_constant(unit, lower, &lower_value);
    if (upper != NULL) {
        upper_known = f2c_evaluate_integer_constant(unit, upper, &upper_value);
    } else if (length_known) {
        upper_value = length_value;
        upper_known = 1;
    }
    if (lower_known && lower_value < 1) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text,
                                                                 lower != NULL ? lower : selector),
                          1, "CHARACTER substring lower bound must be at least one");
    }
    if (upper_known && length_known && upper_value > length_value) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text,
                                                                 upper != NULL ? upper : selector),
                          1, "CHARACTER substring upper bound exceeds declared length %lld",
                          (long long)length_value);
    }
    if (lower_known && upper_known &&
        (upper_value == INT64_MAX ? lower_value > upper_value : lower_value > upper_value + 1)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, selector), 1,
                          "CHARACTER substring lower bound may exceed the upper bound by at most "
                          "one");
    }
}

static void validate_structure_constructor(Context *context, size_t line,
                                           const char *statement_text, F2cExpr *expression) {
    unsigned char *assigned;
    size_t next_positional = 0U;
    size_t argument;
    int saw_keyword = 0;
    if (expression == NULL || expression->derived_type == NULL)
        return;
    assigned = expression->derived_type->component_count != 0U
                   ? (unsigned char *)calloc(expression->derived_type->component_count, 1U)
                   : NULL;
    if (expression->derived_type->component_count != 0U && assigned == NULL) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, expression), 1,
                          "out of memory validating structure constructor");
        return;
    }
    for (argument = 0U; argument < expression->child_count; ++argument) {
        F2cExpr *actual = expression->children[argument];
        F2cExpr *value = actual;
        size_t component = SIZE_MAX;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U) {
            size_t index;
            saw_keyword = 1;
            value = actual->children[0];
            for (index = 0U; index < expression->derived_type->component_count; ++index) {
                if (actual->text != NULL &&
                    strcmp(actual->text, expression->derived_type->components[index].name) == 0) {
                    component = index;
                    break;
                }
            }
            if (component == SIZE_MAX) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "unknown component '%s' in constructor for type '%s'",
                                  actual->text != NULL ? actual->text : "<unknown>",
                                  expression->derived_type->name);
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "positional component follows a keyword component in "
                                  "constructor for type '%s'",
                                  expression->derived_type->name);
            }
            while (next_positional < expression->derived_type->component_count &&
                   assigned[next_positional])
                ++next_positional;
            component = next_positional++;
            if (component >= expression->derived_type->component_count) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "too many components in constructor for type '%s'",
                                  expression->derived_type->name);
                continue;
            }
        }
        if (assigned[component]) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, actual), 1,
                              "component '%s' is specified more than once in constructor for "
                              "type '%s'",
                              expression->derived_type->components[component].name,
                              expression->derived_type->name);
            continue;
        }
        assigned[component] = 1U;
        {
            const Symbol *declared = &expression->derived_type->components[component];
            if (declared->pointer || declared->allocatable || declared->rank != 0U ||
                declared->type == TYPE_CHARACTER) {
                f2c_diagnostic_at(
                    context, line, f2c_validation_expression_start_column(statement_text, actual),
                    1,
                    "constructor component '%s' uses pointer, allocatable, array, or CHARACTER "
                    "semantics that are not yet supported",
                    declared->name);
            } else if (value != NULL &&
                       !f2c_validation_type_compatible(declared->type, value->type)) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, value), 1,
                                  "constructor value for component '%s' has an incompatible type",
                                  declared->name);
            } else if (declared->type == TYPE_DERIVED && value != NULL &&
                       declared->derived_type != value->derived_type) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, value), 1,
                                  "constructor value for component '%s' has an incompatible "
                                  "derived type",
                                  declared->name);
            }
        }
    }
    for (argument = 0U; argument < expression->derived_type->component_count; ++argument) {
        if (!assigned[argument] &&
            expression->derived_type->components[argument].initializer == NULL) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, expression), 1,
                              "constructor for type '%s' does not initialize component '%s'",
                              expression->derived_type->name,
                              expression->derived_type->components[argument].name);
        }
    }
    free(assigned);
}

static void apply_function_result(F2cExpr *expression, Unit *definition) {
    Symbol *result;
    const F2cExpr *array_argument = NULL;
    size_t argument;
    if (expression == NULL || definition == NULL || definition->kind != UNIT_FUNCTION)
        return;
    result = definition->result_name != NULL ? f2c_find_symbol(definition, definition->result_name)
                                             : NULL;
    expression->type = definition->return_type;
    expression->type_kind = definition->return_kind;
    expression->derived_type = result != NULL ? result->derived_type : NULL;
    expression->rank = result != NULL ? result->rank : 0U;
    if (result != NULL)
        expression->shape = result->shape;
    if (!definition->elemental)
        return;
    for (argument = 0U; argument < expression->child_count; ++argument) {
        const F2cExpr *value = f2c_validation_actual_value(expression->children[argument]);
        if (value != NULL && value->kind != F2C_EXPR_ABSENT_ARGUMENT && value->rank != 0U) {
            array_argument = value;
            break;
        }
    }
    if (array_argument != NULL) {
        expression->rank = array_argument->rank;
        expression->shape = array_argument->shape;
        expression->shape.kind = F2C_SHAPE_EXPRESSION;
    } else {
        expression->rank = 0U;
        memset(&expression->shape, 0, sizeof(expression->shape));
        expression->shape.kind = F2C_SHAPE_SCALAR;
    }
}

static void bind_specific_expression(Unit *unit, F2cExpr *expression, Unit *definition) {
    char *resolved;
    Symbol *resolved_symbol;
    if (unit == NULL || expression == NULL || definition == NULL || definition->name == NULL ||
        definition->interface_abstract ||
        (expression->text != NULL && strcmp(expression->text, definition->name) == 0))
        return;
    resolved = f2c_strdup(definition->name);
    if (resolved == NULL)
        return;
    resolved_symbol = f2c_find_symbol(unit, definition->name);
    free(expression->text);
    expression->text = resolved;
    if (resolved_symbol != NULL) {
        expression->symbol = resolved_symbol;
    } else if (expression->symbol != NULL &&
               strcmp(f2c_symbol_c_name(unit, expression->symbol), definition->name) != 0) {
        expression->symbol = NULL;
    }
}

static int intrinsic_operator_key(const char *key) {
    static const char *const keys[] = {
        "operator(+)",    "operator(-)",     "operator(*)",      "operator(/)",
        "operator(**)",   "operator(//)",    "operator(==)",     "operator(/=)",
        "operator(<)",    "operator(<=)",    "operator(>)",      "operator(>=)",
        "operator(.eq.)", "operator(.ne.)",  "operator(.lt.)",   "operator(.le.)",
        "operator(.gt.)", "operator(.ge.)",  "operator(.not.)",  "operator(.and.)",
        "operator(.or.)", "operator(.eqv.)", "operator(.neqv.)",
    };
    size_t index;
    for (index = 0U; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        if (strcmp(key, keys[index]) == 0)
            return 1;
    }
    return 0;
}

static int resolve_operator(Context *context, Unit *unit, size_t line, const char *statement_text,
                            F2cExpr *expression) {
    char *generic_name;
    Unit *definition;
    int handled = 0;
    int required;
    if (expression == NULL || expression->text == NULL ||
        (expression->kind != F2C_EXPR_UNARY && expression->kind != F2C_EXPR_BINARY))
        return 0;
    generic_name = f2c_generic_operator_key(expression->text);
    if (generic_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &expression->span, 1,
                                 "out of memory resolving operator generic");
        return 1;
    }
    required = !intrinsic_operator_key(generic_name);
    definition = f2c_validation_generic_specific(context, unit, line, generic_name,
                                                 &expression->span, expression->children,
                                                 expression->child_count, 0, required, &handled);
    if (definition != NULL) {
        definition = f2c_validation_procedure_call(
            context, unit, line, statement_text, generic_name, &expression->span,
            &expression->children, NULL, &expression->child_count, 0);
        expression->child_capacity = expression->child_count;
        expression->resolved_procedure = definition;
        apply_function_result(expression, definition);
        bind_specific_expression(unit, expression, definition);
    }
    free(generic_name);
    return handled;
}

void f2c_validation_expression_calls(Context *context, Unit *unit, size_t line,
                                     const char *statement_text, F2cExpr *expression) {
    size_t i;
    int operator_handled;
    if (expression == NULL)
        return;
    for (i = 0U; i < expression->child_count; ++i)
        f2c_validation_expression_calls(context, unit, line, statement_text,
                                        expression->children[i]);
    operator_handled = resolve_operator(context, unit, line, statement_text, expression);
    validate_substring_semantics(context, unit, line, statement_text, expression);
    if (expression->kind == F2C_EXPR_ARRAY_REFERENCE && expression->symbol != NULL) {
        for (i = 0U; i < expression->child_count; ++i) {
            F2cExpr *selector = expression->children[i];
            size_t part;
            if (selector == NULL)
                continue;
            if (selector->kind != F2C_EXPR_ARRAY_SECTION) {
                if (selector->type != TYPE_INTEGER || selector->rank > 1U) {
                    f2c_diagnostic_at(
                        context, line,
                        f2c_validation_expression_start_column(statement_text, selector), 1,
                        "array subscript must be a scalar INTEGER or rank-one "
                        "INTEGER vector");
                }
                continue;
            }
            for (part = 0U; part < selector->child_count; ++part) {
                F2cExpr *bound = selector->children[part];
                if (bound != NULL && bound->kind != F2C_EXPR_INVALID &&
                    (bound->type != TYPE_INTEGER || bound->rank != 0U)) {
                    f2c_diagnostic_at(context, line,
                                      f2c_validation_expression_start_column(statement_text, bound),
                                      1,
                                      "array section bound and stride must be scalar INTEGER "
                                      "expressions");
                }
            }
        }
    }
    if (expression->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR)
        validate_structure_constructor(context, line, statement_text, expression);
    if (!operator_handled && expression->kind == F2C_EXPR_BINARY && expression->child_count == 2U) {
        size_t dimension;
        if (f2c_validation_shapes_mismatch(expression->children[0], expression->children[1],
                                           &dimension)) {
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, expression),
                1, "nonconformable array operands in dimension %zu: extent %llu and %llu",
                dimension + 1U,
                (unsigned long long)expression->children[0]->shape.dimensions[dimension].extent,
                (unsigned long long)expression->children[1]->shape.dimensions[dimension].extent);
        }
    }
    if (expression->kind == F2C_EXPR_CALL && expression->symbol != NULL &&
        expression->symbol->statement_function) {
        Symbol *function = expression->symbol;
        expression->type = function->type;
        expression->type_kind =
            function->kind != 0 ? function->kind : f2c_default_kind(function->type);
        expression->rank = 0U;
        expression->shape.rank = 0U;
        expression->shape.kind = F2C_SHAPE_SCALAR;
        if (expression->child_count != function->statement_function_argument_count) {
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, expression),
                1, "statement function '%s' expects %zu arguments but has %zu", function->name,
                function->statement_function_argument_count, expression->child_count);
        }
        for (i = 0U; i < expression->child_count; ++i) {
            if (expression->children[i] != NULL &&
                expression->children[i]->kind == F2C_EXPR_KEYWORD_ARGUMENT)
                f2c_diagnostic_at(
                    context, line,
                    f2c_validation_expression_start_column(statement_text, expression->children[i]),
                    1, "statement functions do not accept keyword arguments");
        }
    } else if (expression->kind == F2C_EXPR_CALL && expression->text != NULL &&
               f2c_is_intrinsic_name(expression->text)) {
        const F2cIntrinsicSignature *signature = f2c_find_intrinsic(expression->text);
        validate_intrinsic_arity(context, line, statement_text, expression);
        f2c_validation_bit_intrinsic(context, unit, line, statement_text, expression);
        f2c_validation_character_intrinsic(context, unit, line, statement_text, expression);
        f2c_validation_numeric_model_intrinsic(context, line, statement_text, expression);
        f2c_validation_real_representation_intrinsic(context, line, statement_text, expression);
        if (strcmp(expression->text, "size") == 0 || strcmp(expression->text, "shape") == 0 ||
            strcmp(expression->text, "lbound") == 0 || strcmp(expression->text, "ubound") == 0)
            validate_array_inquiry(context, unit, line, statement_text, expression);
        if (strcmp(expression->text, "transpose") == 0 || strcmp(expression->text, "matmul") == 0)
            validate_matrix_intrinsic(context, line, statement_text, expression);
        if (signature != NULL && signature->rank_rule == F2C_INTRINSIC_RANK_ELEMENTAL) {
            const F2cExpr *array_argument = NULL;
            for (i = 0U; i < expression->child_count; ++i) {
                F2cExpr *argument = expression->children[i];
                size_t dimension;
                if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                    argument->child_count == 1U)
                    argument = argument->children[0];
                if (argument == NULL || argument->rank == 0U)
                    continue;
                if (array_argument == NULL) {
                    array_argument = argument;
                } else if (array_argument->rank != argument->rank) {
                    f2c_diagnostic_at(
                        context, line,
                        f2c_validation_expression_start_column(statement_text, argument), 1,
                        "elemental intrinsic '%s' has nonconformable argument ranks "
                        "%zu and %zu",
                        expression->text, array_argument->rank, argument->rank);
                } else if (f2c_validation_shapes_mismatch(array_argument, argument, &dimension)) {
                    f2c_diagnostic_at(
                        context, line,
                        f2c_validation_expression_start_column(statement_text, argument), 1,
                        "elemental intrinsic '%s' has nonconformable extent in dimension %zu",
                        expression->text, dimension + 1U);
                }
            }
        }
        if (strcmp(expression->text, "present") == 0)
            validate_present_intrinsic(context, unit, line, statement_text, expression);
        else if (strcmp(expression->text, "allocated") == 0)
            validate_allocated_intrinsic(context, line, statement_text, expression);
        else if (strcmp(expression->text, "associated") == 0)
            validate_associated_intrinsic(context, line, statement_text, expression);
    } else if (expression->kind == F2C_EXPR_CALL && expression->symbol != NULL &&
               expression->symbol->type_bound) {
        const Symbol *binding = expression->symbol;
        const size_t explicit_count =
            expression->child_count != 0U ? expression->child_count - 1U : 0U;
        const size_t expected_count =
            binding->external_parameter_count -
            ((!binding->type_bound_nopass && binding->external_parameter_count != 0U) ? 1U : 0U);
        if (explicit_count != expected_count)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "type-bound function '%s' expects %zu explicit arguments but has "
                              "%zu",
                              expression->text, expected_count, explicit_count);
    } else if (expression->kind == F2C_EXPR_CALL && !f2c_is_intrinsic_name(expression->text)) {
        Unit *definition = f2c_validation_procedure_call(
            context, unit, line, statement_text, expression->text, &expression->span,
            &expression->children, NULL, &expression->child_count, 0);
        expression->child_capacity = expression->child_count;
        expression->resolved_procedure = definition;
        apply_function_result(expression, definition);
        bind_specific_expression(unit, expression, definition);
    }
}

void f2c_validation_io_item_calls(Context *context, Unit *unit, size_t line,
                                  const char *statement_text, F2cIoItem *item) {
    size_t i;
    f2c_validation_expression_calls(context, unit, line, statement_text, item->expression);
    f2c_validation_expression_calls(context, unit, line, statement_text, item->iterator);
    f2c_validation_expression_calls(context, unit, line, statement_text, item->initial);
    f2c_validation_expression_calls(context, unit, line, statement_text, item->limit);
    f2c_validation_expression_calls(context, unit, line, statement_text, item->step);
    for (i = 0U; i < item->child_count; ++i)
        f2c_validation_io_item_calls(context, unit, line, statement_text, &item->children[i]);
}
