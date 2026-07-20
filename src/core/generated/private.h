#ifndef F2C_CORE_GENERATED_PRIVATE_H
#define F2C_CORE_GENERATED_PRIVATE_H

#include "internal/base.h"

void f2c_emit_bit_intrinsic_support(Buffer *output);
void f2c_emit_character_intrinsic_support(Buffer *output);
void f2c_emit_numeric_model_contract(Buffer *output);
void f2c_emit_numeric_model_support(Buffer *output);
void f2c_emit_numeric_operation_support(Buffer *output);
void f2c_emit_real_representation_support(Buffer *output);
void f2c_emit_time_intrinsic_support(Buffer *output);
void f2c_emit_relation_reduction_support(Buffer *output, int needs_complex);
void f2c_emit_io_stream_support(Buffer *output);
void f2c_emit_list_io_support(Buffer *output, int needs_complex);
void f2c_emit_file_unit_support(Buffer *output);
void f2c_emit_record_io_support(Buffer *output);

#endif
