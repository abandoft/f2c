#include "semantic/validation/private.h"

int f2c_validation_intrinsic_subroutine(Context *context, Unit *unit, F2cStatement *statement) {
    const F2cIntrinsicSpecification *specification;
    const Symbol *declared_callee;
    if (statement == NULL || statement->kind != F2C_STMT_CALL || statement->name == NULL)
        return 0;

    specification = f2c_find_intrinsic_specification(statement->name);
    if (specification == NULL ||
        specification->descriptor.procedure_kind != F2C_INTRINSIC_PROCEDURE_SUBROUTINE)
        return 0;

    declared_callee = f2c_find_symbol(unit, statement->name);
    if (declared_callee != NULL && declared_callee->external_declared)
        return 0;

    statement->intrinsic = specification->descriptor.id;
    switch (statement->intrinsic) {
    case F2C_INTRINSIC_MVBITS:
        f2c_validation_mvbits(context, unit, statement);
        break;
    case F2C_INTRINSIC_RANDOM_NUMBER:
    case F2C_INTRINSIC_RANDOM_SEED:
        f2c_validation_random_intrinsic(context, statement);
        break;
    case F2C_INTRINSIC_CPU_TIME:
    case F2C_INTRINSIC_DATE_AND_TIME:
    case F2C_INTRINSIC_SYSTEM_CLOCK:
        f2c_validation_time_intrinsic(context, statement);
        break;
    default:
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "intrinsic subroutine '%s' has no semantic validator", statement->name);
        break;
    }
    return 1;
}
