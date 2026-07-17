#ifndef F2C_CODEGEN_ARRAY_PRIVATE_H
#define F2C_CODEGEN_ARRAY_PRIVATE_H

#include "ast/internal.h"

void f2c_array_indent(Buffer *output, int depth);
char *f2c_array_emit_expression(Unit *unit, const F2cExpr *expression);
F2cExpr *f2c_array_clone_expression(const F2cExpr *expression);
F2cExpr *f2c_array_element_expression(Unit *unit, const F2cExpr *expression, size_t rank,
                                      const char *const *ordinals);
char *f2c_array_expression_extent(Unit *unit, const F2cExpr *expression, size_t dimension);
int f2c_array_materialize_constructors(Context *context, Unit *unit, F2cExpr *expression,
                                       size_t identifier, const char *role, size_t *temporary,
                                       Buffer *prelude, Buffer *cleanup, int depth);
int f2c_array_emit_elemental_assignment(Context *context, Unit *unit, Symbol *target,
                                        const F2cExpr *right, size_t line, int depth);
int f2c_array_emit_elemental_call(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth);
int f2c_array_emit_numeric_constructor(Context *context, Unit *unit, Symbol *left_symbol,
                                       const F2cExpr *constructor, const char *element_count,
                                       int depth);
int f2c_array_emit_numeric_constructor_temporary(Context *context, Unit *unit,
                                                 const F2cExpr *constructor, const char *storage,
                                                 const char *count, const char *capacity,
                                                 Buffer *output, int depth);
int f2c_array_emit_character_constructor_temporary(Context *context, Unit *unit,
                                                   const F2cExpr *constructor, const char *storage,
                                                   const char *count, const char *capacity,
                                                   const char *character_length,
                                                   const char *character_length_set, Buffer *output,
                                                   int depth);
int f2c_array_emit_derived_constructor_temporary(Context *context, Unit *unit,
                                                 const F2cExpr *constructor, const char *storage,
                                                 const char *count, const char *capacity,
                                                 Buffer *output, int depth);
int f2c_array_emit_constructor_values(Context *context, Unit *unit, Symbol *target,
                                      const F2cExpr *constructor, const char *storage,
                                      const char *count, const char *capacity,
                                      const char *character_length,
                                      const char *character_length_set, int character, int dynamic,
                                      int infer_character_length, int depth);
int f2c_array_emit_allocatable_numeric_constructor(Context *context, Unit *unit, Symbol *target,
                                                   const F2cExpr *constructor, int depth);
int f2c_array_emit_allocatable_character_constructor(Context *context, Unit *unit, Symbol *target,
                                                     const F2cExpr *constructor, int depth);
int f2c_array_emit_whole_character_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                              const F2cExpr *right, Symbol *right_symbol,
                                              const char *element_count, int depth);

#endif
