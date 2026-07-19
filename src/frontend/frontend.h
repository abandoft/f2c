#ifndef F2C_FRONTEND_FRONTEND_H
#define F2C_FRONTEND_FRONTEND_H

#include "ast/unit.h"
#include "internal/context.h"

int f2c_normalize_source(Context *context, const char *source, size_t length, F2cSourceForm form);

int f2c_interface_start_tokens(const Line *line);
int f2c_interface_end_tokens(const Line *line);
int f2c_abstract_interface_tokens(const Line *line);
int f2c_contains_tokens(const Line *line);
int f2c_derived_type_start_tokens(const Line *line);
int f2c_derived_type_end_tokens(const Line *line);

typedef enum F2cUnitEndMatchStatus {
    F2C_UNIT_END_NO_MATCH,
    F2C_UNIT_END_MATCHED,
    F2C_UNIT_END_MISMATCHED
} F2cUnitEndMatchStatus;

F2cUnitEndMatchStatus f2c_match_program_unit_end(Context *context, const Line *line,
                                                 const Unit *unit);
F2cUnitHeaderParseStatus f2c_parse_unit_header(Context *context, const Line *line, Unit *unit);
void f2c_parse_explicit_interfaces(Context *context, Unit *host);
Unit *f2c_find_interface_signature(Context *context, Unit *scope, const char *name,
                                   int include_abstract);
int f2c_discover_units(Context *context);
int f2c_discover_modules(Context *context);
Symbol *f2c_find_symbol(Unit *unit, const char *name);
Symbol *f2c_ensure_symbol(Unit *unit, const char *name);
F2cNamelistGroup *f2c_find_namelist(Unit *unit, const char *name);
F2cDerivedType *f2c_find_derived_type(Unit *unit, const char *name);
int f2c_line_in_derived_type(const Unit *unit, size_t line_index);
const char *f2c_symbol_c_name(Unit *unit, const Symbol *symbol);
int f2c_declaration_line(const char *line);
int f2c_declaration_tokens(const Line *line);
int f2c_unit_line_is_active(const Unit *unit, const Line *line);
void f2c_analyze_unit(Context *context, Unit *unit);
void f2c_analyze_module(Context *context, Unit *unit);
void f2c_parse_procedure_declaration(Context *context, Unit *unit, Line *source_line);
int f2c_copy_procedure_signature(Symbol *symbol, Unit *signature);
int f2c_copy_function_result_metadata(Symbol *symbol, Unit *signature);
void f2c_build_statement_ir(Context *context, Unit *unit);
void f2c_prepare_implicit_map(Context *context, Unit *unit);
void f2c_discover_implicit_symbols(Context *context, Unit *unit);
Type f2c_implicit_type_for_name(const Unit *unit, const char *name);
int f2c_implicit_kind_for_name(const Unit *unit, const char *name);
const char *f2c_implicit_character_length_for_name(const Unit *unit, const char *name);
F2cTokenRange f2c_implicit_character_length_syntax_for_name(const Unit *unit, const char *name);
void f2c_validate_implicit_external(Context *context, Unit *unit);
int f2c_build_procedure_registry(Context *context);
void f2c_import_module(Context *context, Unit *unit, Line *source_line);
void f2c_import_host_module(Context *context, Unit *unit);
int f2c_has_supported_module(const Context *context);
int f2c_supported_module_needs_complex(const Context *context);

#endif
