#ifndef F2C_CODEGEN_IO_CONTROL_PRIVATE_H
#define F2C_CODEGEN_IO_CONTROL_PRIVATE_H

#include "codegen/io/private.h"

typedef struct F2cEmittedCharacterControl {
    char *expression;
    char *pointer;
    char *length;
} F2cEmittedCharacterControl;

typedef struct F2cEmittedIoStatus {
    char *iostat;
    F2cEmittedCharacterControl iomsg;
    char *err_label;
} F2cEmittedIoStatus;

int f2c_io_emit_character_control(Unit *unit, const F2cStatement *statement, F2cIoControlKind kind,
                                  const char *default_pointer, const char *default_length,
                                  F2cEmittedCharacterControl *result);
void f2c_io_free_character_control(F2cEmittedCharacterControl *control);
int f2c_io_emit_status_controls(Unit *unit, const F2cStatement *statement,
                                F2cEmittedIoStatus *result);
void f2c_io_free_status_controls(F2cEmittedIoStatus *status);
void f2c_io_emit_control_result(Context *context, Unit *unit, const F2cStatement *statement,
                                const F2cEmittedIoStatus *status, const char *operation, int depth);
int f2c_emit_iolength_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth);

#endif
