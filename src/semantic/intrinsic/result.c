#include "semantic/intrinsic/result.h"

#include "internal/f2c.h"
#include "semantic/operator.h"

#include <limits.h>
#include <string.h>

const F2cExpr *f2c_intrinsic_argument_at(const F2cIntrinsicSignature *signature,
                                         F2cExpr *const *arguments, size_t count, size_t slot) {
    const F2cIntrinsicArgumentSchema *schema =
        signature != NULL ? f2c_intrinsic_argument_schema(signature->id) : NULL;
    if (schema != NULL && slot < schema->count)
        return f2c_intrinsic_argument(arguments, count, schema->names[slot], slot);
    size_t position = 0U;
    for (size_t index = 0U; index < count; ++index) {
        const F2cExpr *argument = arguments[index];
        if (argument != NULL && argument->kind != F2C_EXPR_KEYWORD_ARGUMENT && position++ == slot)
            return argument;
    }
    return NULL;
}

static Type absolute_type(Type type) {
    return type == TYPE_DOUBLE_COMPLEX ? TYPE_DOUBLE : type == TYPE_COMPLEX ? TYPE_REAL : type;
}

static F2cScalarType common_model(const F2cExpr *left, const F2cExpr *right) {
    F2cOperatorTyping typing;
    const F2cOperator operation =
        left != NULL && right != NULL && left->type == TYPE_LOGICAL && right->type == TYPE_LOGICAL
            ? F2C_OPERATOR_AND
            : F2C_OPERATOR_MULTIPLY;
    if (f2c_operator_typing(operation, 0, f2c_expression_scalar_type(left),
                            f2c_expression_scalar_type(right), &typing) != F2C_OPERATOR_VALID)
        return (F2cScalarType){TYPE_UNKNOWN, 0};
    return typing.result;
}

static const F2cExpr *optional_kind(const F2cIntrinsicSignature *signature,
                                    F2cExpr *const *arguments, size_t count) {
    const F2cIntrinsicArgumentSchema *schema = f2c_intrinsic_argument_schema(signature->id);
    if (schema == NULL)
        return NULL;
    for (size_t slot = 0U; slot < schema->count; ++slot)
        if (strcmp(schema->names[slot], "kind") == 0)
            return f2c_intrinsic_argument_at(signature, arguments, count, slot);
    return NULL;
}

F2cIntrinsicResult f2c_intrinsic_result(Unit *unit, const F2cIntrinsicSignature *signature,
                                        F2cExpr *const *arguments, size_t count) {
    F2cIntrinsicResult result = {{TYPE_UNKNOWN, 0}, NULL, NULL, 0U};
    if (signature == NULL)
        return result;
    const F2cExpr *primary = f2c_intrinsic_argument_at(signature, arguments, count, 0U);
    const F2cExpr *model = signature->type_rule == F2C_INTRINSIC_TYPE_MOLD
                               ? f2c_intrinsic_argument_at(signature, arguments, count, 1U)
                               : primary;
    const F2cScalarType source = f2c_expression_scalar_type(model);
    F2cScalarType common = {TYPE_UNKNOWN, 0};
    result.type_source = model;
    switch (signature->type_rule) {
    case F2C_INTRINSIC_TYPE_FIRST:
    case F2C_INTRINSIC_TYPE_MOLD:
        result.scalar.type = source.type;
        break;
    case F2C_INTRINSIC_TYPE_COMMON:
        common = common_model(primary, f2c_intrinsic_argument_at(signature, arguments, count, 1U));
        result.scalar.type = common.type;
        break;
    case F2C_INTRINSIC_TYPE_ABSOLUTE:
        result.scalar.type = absolute_type(source.type);
        break;
    case F2C_INTRINSIC_TYPE_DOUBLE:
        result.scalar.type = TYPE_DOUBLE;
        break;
    case F2C_INTRINSIC_TYPE_REAL:
        result.scalar.type = TYPE_REAL;
        break;
    case F2C_INTRINSIC_TYPE_INTEGER:
        result.scalar.type = TYPE_INTEGER;
        break;
    case F2C_INTRINSIC_TYPE_COMPLEX:
        result.scalar.type = TYPE_COMPLEX;
        break;
    case F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX:
        result.scalar.type = TYPE_DOUBLE_COMPLEX;
        break;
    case F2C_INTRINSIC_TYPE_CHARACTER:
        result.scalar.type = TYPE_CHARACTER;
        break;
    case F2C_INTRINSIC_TYPE_LOGICAL:
        result.scalar.type = TYPE_LOGICAL;
        break;
    }
    switch (signature->kind_rule) {
    case F2C_INTRINSIC_KIND_FIRST:
    case F2C_INTRINSIC_KIND_FIRST_OPTIONAL:
        result.scalar.kind = model != NULL ? source.kind : 0;
        break;
    case F2C_INTRINSIC_KIND_COMMON:
        result.scalar.kind = common.kind;
        break;
    case F2C_INTRINSIC_KIND_DEFAULT:
    case F2C_INTRINSIC_KIND_OPTIONAL:
        result.scalar.kind = f2c_default_kind(result.scalar.type);
        break;
    }
    if (signature->type_rule == F2C_INTRINSIC_TYPE_MOLD)
        result.scalar.kind = model != NULL ? source.kind : 0;
    if (signature->id == F2C_INTRINSIC_REAL &&
        signature->kind_rule == F2C_INTRINSIC_KIND_OPTIONAL && model != NULL &&
        (source.type == TYPE_COMPLEX || source.type == TYPE_DOUBLE_COMPLEX))
        result.scalar.kind = source.kind;
    if (signature->kind_rule == F2C_INTRINSIC_KIND_OPTIONAL ||
        signature->kind_rule == F2C_INTRINSIC_KIND_FIRST_OPTIONAL) {
        const F2cExpr *selector = optional_kind(signature, arguments, count);
        if (selector != NULL) {
            int64_t value;
            result.scalar.kind = selector->type == TYPE_INTEGER && selector->rank == 0U &&
                                         f2c_evaluate_integer_constant(unit, selector, &value) &&
                                         value > 0 && value <= INT_MAX
                                     ? (int)value
                                     : 0;
        }
    }
    if (result.scalar.type == TYPE_DERIVED)
        result.derived_type = model != NULL ? model->derived_type : NULL;
    if (result.scalar.kind != 0)
        result.scalar = f2c_scalar_type(result.scalar.type, result.scalar.kind);
    result.rank = f2c_intrinsic_result_rank(signature, arguments, count);
    return result;
}

void f2c_intrinsic_refresh_result(Unit *unit, F2cExpr *expression) {
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->text == NULL ||
        expression->resolved_procedure != NULL ||
        (expression->symbol != NULL &&
         (expression->symbol->external_declared || expression->symbol->statement_function ||
          expression->symbol->type_bound ||
          (expression->symbol->external && expression->symbol->intrinsic == NULL))))
        return;
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(expression->text);
    if (signature == NULL)
        return;
    const F2cIntrinsicResult result =
        f2c_intrinsic_result(unit, signature, expression->children, expression->child_count);
    expression->type = result.scalar.type;
    expression->type_kind = result.scalar.kind;
    expression->derived_type = result.derived_type;
    expression->rank = result.rank;
    expression->intrinsic = signature->id;
    f2c_intrinsic_refresh_shape(unit, expression, signature);
}
