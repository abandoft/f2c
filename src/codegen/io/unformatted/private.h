#ifndef F2C_CODEGEN_IO_UNFORMATTED_PRIVATE_H
#define F2C_CODEGEN_IO_UNFORMATTED_PRIVATE_H

#include "codegen/io/private.h"

void f2c_io_emit_unformatted_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                    const char *value, int input, const char *stream,
                                    const char *status, int depth);
void f2c_io_emit_unformatted_derived_scalar(Context *context, Unit *unit, F2cDerivedType *derived,
                                            const char *value, int input, const char *stream,
                                            const char *unit_number, const char *status, int depth);
int f2c_io_emit_unformatted_array(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                  const char *stream, const char *unit_number, const char *status,
                                  int depth);

#endif
