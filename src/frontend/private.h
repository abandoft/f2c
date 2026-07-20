#ifndef F2C_FRONTEND_PRIVATE_H
#define F2C_FRONTEND_PRIVATE_H

#include "internal/f2c.h"

Symbol *f2c_ensure_symbol_impl(Unit *unit, const char *name);
int f2c_parse_dimensions_tokens(Context *context, Unit *unit, Symbol *symbol, const Line *line,
                                size_t open, size_t close);
void f2c_parse_namelist_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_common_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_optional_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_contiguous_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_derived_type_definitions(Context *context, Unit *unit);
void f2c_parse_external_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_dimension_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_parameter_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_save_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_parse_equivalence_declaration(Context *context, Unit *unit, Line *source_line);
void f2c_mark_call_targets(Unit *unit, const Line *line);
void f2c_mark_function_references(Unit *unit, const Line *line);
int f2c_mark_statement_function_symbols(Unit *unit, const Line *line);
void f2c_infer_external_signature(Unit *unit, Symbol *external, const Line *line);

#endif
