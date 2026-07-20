#ifndef F2C_CODEGEN_IO_PRIVATE_H
#define F2C_CODEGEN_IO_PRIVATE_H

#include "internal/f2c.h"

void f2c_io_indent(Buffer *output, int depth);
char *f2c_io_emit_item_expression(Unit *unit, const F2cIoItem *item);
char *f2c_io_emit_required_expression(Unit *unit, const F2cExpr *expression);
int f2c_io_begin_unaligned_input(Context *context, Unit *unit, const F2cIoItem *item, int depth,
                                 F2cIoItem *lowered_item, F2cExpr *lowered_expression);
void f2c_io_end_unaligned_input(Context *context, const Symbol *symbol, int depth);
char *f2c_io_c_string_literal(const char *text, size_t length);
void f2c_io_emit_format_state_support(Context *context);
void f2c_io_emit_format_program_support(Context *context);
void f2c_io_emit_format_text_parser_support(Context *context);
void f2c_io_emit_format_real_support(Context *context);
int f2c_io_emit_format_program(Context *context, const F2cFormat *format, const char *name,
                               int depth);
F2cTypeBinding *f2c_io_defined_binding(F2cDerivedType *derived, F2cDefinedIoKind kind);
int f2c_io_emit_defined_io_call(Context *context, const char *value, F2cDerivedType *derived,
                                F2cDefinedIoKind kind, const char *unit_number, const char *iotype,
                                const char *v_list, const char *v_list_count, const char *status,
                                int depth);
void f2c_io_emit_item(Context *context, Unit *unit, const char *file, const F2cIoItem *item,
                      int input, const char *status, int record_input,
                      F2cDefinedIoKind defined_kind, const char *unit_number, const char *iotype,
                      int depth);
const F2cIoControl *f2c_io_control(const F2cStatement *statement, F2cIoControlKind kind,
                                   size_t positional);
void f2c_io_emit_namelist_value(Context *context, Unit *unit, const char *file,
                                const Symbol *symbol, const char *value,
                                const char *character_length_override, int input, int depth);
int f2c_io_emit_namelist(Context *context, Unit *unit, const char *file,
                         const F2cNamelistGroup *group, int input, const char *unit_number,
                         int depth);
void f2c_io_emit_formatted_item(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                const char *unit_number, int depth);
int f2c_io_emit_formatted_transfer(Context *context, Unit *unit, const F2cStatement *statement,
                                   const F2cIoControl *format_control, const char *file,
                                   const char *unit_number, int input,
                                   const char *advance_expression, const char *size_target,
                                   const char *status_target, int depth);
int f2c_io_emit_unformatted_item(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                 const char *stream, const char *unit_number, const char *status,
                                 int depth);

#endif
