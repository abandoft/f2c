#ifndef F2C_SEMANTIC_SEMANTIC_H
#define F2C_SEMANTIC_SEMANTIC_H

#include "internal/context.h"
#include "semantic/intrinsic.h"

void f2c_validate_unit_expressions(Context *context, Unit *unit);
void f2c_validate_project_storage(Context *context);
int f2c_finalize_host_association(Context *context);
void f2c_resolve_equivalence_storage(Context *context, Unit *unit);
void f2c_resolve_derived_semantics(Context *context);
int f2c_symbol_resize_external_parameters(Symbol *symbol, size_t count);
int f2c_set_external_parameter_signature(Symbol *symbol, size_t parameter, const Symbol *dummy);
int f2c_symbol_character_length_constant(const Symbol *symbol, int64_t *length);
int f2c_character_length_signatures_match(const Symbol *left, const Symbol *right);
int f2c_validation_procedure_signatures_compatible(const Symbol *expected, const Symbol *actual,
                                                   unsigned int depth);
int f2c_symbol_uses_descriptor(const Symbol *symbol);
int f2c_symbol_is_assumed_size(const Symbol *symbol);
int f2c_expression_is_whole_assumed_size(const F2cExpr *expression);
int f2c_unit_has_descriptor_result(const Unit *unit);
int f2c_procedure_has_descriptor_result(const Symbol *procedure);
int f2c_expression_has_allocatable_result(const F2cExpr *expression);
int f2c_expression_has_descriptor_result(const F2cExpr *expression);
int f2c_host_function_result_symbol(const Unit *unit, const Symbol *symbol);
const Symbol *f2c_host_capture_actual(Unit *caller, const Unit *procedure, size_t capture,
                                      const Symbol **formal);
int f2c_host_capture_is_local_descriptor(const Unit *caller, const Symbol *actual);
int f2c_host_capture_needs_descriptor_lifecycle(const Symbol *actual);
size_t f2c_host_capture_local_descriptor_count(Unit *caller, const Unit *procedure);
int f2c_host_capture_has_descriptor_lifecycle(Unit *caller, const Unit *procedure);
int f2c_expression_is_character_temporary(const F2cExpr *expression);
int f2c_expression_is_derived_actual_temporary(const F2cExpr *expression);
int f2c_expression_has_materialized_call_result(const F2cExpr *expression);
int f2c_expression_has_materialized_descriptor_result(const F2cExpr *expression);
int f2c_expression_has_materialized_derived_result(const F2cExpr *expression);
const F2cExpr *f2c_expression_ordered_binary_operand(const F2cExpr *expression);
int f2c_statement_is_function_definition(const Unit *unit, size_t statement);
int f2c_plan_expression_lifetimes(Context *context, Unit *unit);
int f2c_analyze_temporary_lifetimes(Context *context, Unit *unit);
int f2c_relocate_statement_function_temporaries(F2cExpr *expression, size_t *next);
int f2c_evaluate_integer_constant(Unit *unit, const F2cExpr *expression, int64_t *value);
int f2c_evaluate_real_constant(Unit *unit, const F2cExpr *expression, double *value);
int f2c_evaluate_complex_constant(Unit *unit, const F2cExpr *expression, double *real,
                                  double *imaginary);
int f2c_evaluate_integer_syntax(Unit *unit, F2cTokenRange syntax, int64_t *value);
int f2c_expression_is_initialization_constant(const F2cExpr *expression);
int f2c_integer_iteration_count(int64_t first, int64_t last, int64_t step, uint64_t *count);
size_t f2c_character_literal_length(const char *text);
char *f2c_character_literal_bytes(const char *text, size_t *length);
int f2c_evaluate_character_constant(Unit *unit, const F2cExpr *expression, char **value,
                                    size_t *length);

#endif
