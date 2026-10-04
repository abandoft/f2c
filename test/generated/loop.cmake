# The generated procedure ABI is checked without relying on undefined native
# Fortran overflow behavior. Keep this check available on Windows without FC.
set(loop_source "${BINARY_DIR}/loop-contract.c")
set(loop_header "${BINARY_DIR}/loop-contract.h")
set(loop_executable "${BINARY_DIR}/generated_loop_contract_test")
execute_process(
    COMMAND "${F2C}" "${SOURCE_DIR}/test/fixtures/loop_contract.f90"
            -o "${loop_source}" --header "${loop_header}"
    RESULT_VARIABLE loop_translate_status
    OUTPUT_VARIABLE loop_translate_output
    ERROR_VARIABLE loop_translate_error)
if(NOT loop_translate_status EQUAL 0)
    message(FATAL_ERROR "loop ABI translation failed: ${loop_translate_error}${loop_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    set(loop_executable "${loop_executable}.exe")
    set(loop_compile_command ${CC_COMMAND} /std:c17 /O2 /W4 "/I${BINARY_DIR}"
        "${loop_source}" "${SOURCE_DIR}/test/generated/loop_contract.c"
        "/Fe${loop_executable}")
else()
    set(loop_compile_command ${CC_COMMAND} -std=c17 -O2 -Wall -Wextra -Wpedantic
        -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
        "-I${BINARY_DIR}" "${loop_source}" "${SOURCE_DIR}/test/generated/loop_contract.c"
        -lm -o "${loop_executable}")
endif()
execute_process(COMMAND ${loop_compile_command}
    RESULT_VARIABLE loop_compile_status
    OUTPUT_VARIABLE loop_compile_output
    ERROR_VARIABLE loop_compile_error)
if(NOT loop_compile_status EQUAL 0)
    message(FATAL_ERROR "loop ABI client did not compile: ${loop_compile_error}${loop_compile_output}")
endif()
execute_process(COMMAND "${loop_executable}"
    RESULT_VARIABLE loop_run_status
    OUTPUT_VARIABLE loop_run_output
    ERROR_VARIABLE loop_run_error)
if(NOT loop_run_status EQUAL 0)
    message(FATAL_ERROR "loop ABI client failed: ${loop_run_error}${loop_run_output}")
endif()
