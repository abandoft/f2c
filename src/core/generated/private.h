#ifndef F2C_CORE_GENERATED_PRIVATE_H
#define F2C_CORE_GENERATED_PRIVATE_H

#include "internal/base.h"

void f2c_emit_bit_intrinsic_support(Buffer *output);
void f2c_emit_character_intrinsic_support(Buffer *output);
void f2c_emit_qualified_character_support(Buffer *output);
void f2c_emit_character_snapshot_support(Buffer *output, int needs_qualified);
void f2c_emit_extremum_support(Buffer *output, int needs_minimum, int needs_maximum);
void f2c_emit_numeric_conversion_support(Buffer *output);
void f2c_emit_numeric_model_contract(Buffer *output);
void f2c_emit_numeric_model_support(Buffer *output);
void f2c_emit_numeric_operation_support(Buffer *output);
void f2c_emit_real_representation_support(Buffer *output);
void f2c_emit_time_intrinsic_support(Buffer *output);
void f2c_emit_process_cpu_time_support(Buffer *output);
void f2c_emit_transfer_support(Buffer *output, int needs_complex);
void f2c_emit_reduction_support(Buffer *output, int needs_complex, int needs_qualified);
void f2c_emit_relation_reduction_support(Buffer *output, int needs_complex, int needs_qualified);
void f2c_emit_io_stream_support(Buffer *output);
void f2c_emit_io_control_model(Buffer *output);
void f2c_emit_io_control_support(Buffer *output);
void f2c_emit_io_number_support(Buffer *output);
void f2c_emit_io_number_input_support(Buffer *output);
void f2c_emit_list_io_support(Buffer *output, int needs_complex);
void f2c_emit_list_read_support(Buffer *output);
void f2c_emit_list_token_support(Buffer *output);
void f2c_emit_list_write_support(Buffer *output);
void f2c_emit_list_complex_support(Buffer *output);
void f2c_emit_file_unit_support(Buffer *output);
void f2c_emit_record_io_support(Buffer *output);

#endif
