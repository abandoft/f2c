#ifndef F2C_IR_CALL_H
#define F2C_IR_CALL_H

#include "ir/expression.h"

/* Semantic binding orders explicit actuals by dummy position. Bound calls own
 * a binding designator at child zero; PASS inserts its owner at the declared
 * dummy position without adding a second AST child. */
size_t f2c_call_parameter_count(const F2cExpr *call);
const F2cExpr *f2c_call_passed_object(const F2cExpr *call);
const F2cExpr *f2c_call_parameter_actual(const F2cExpr *call, size_t parameter);
size_t f2c_call_child_parameter(const F2cExpr *call, size_t child);

#endif
