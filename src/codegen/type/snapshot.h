#ifndef F2C_CODEGEN_TYPE_SNAPSHOT_H
#define F2C_CODEGEN_TYPE_SNAPSHOT_H

#include "internal/context.h"

/* Compiler snapshots are not Fortran objects subject to language FINAL calls. */
void f2c_emit_snapshot_type_prototypes(Context *context);
void f2c_emit_snapshot_type_definitions(Context *context);

#endif
