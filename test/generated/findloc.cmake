# Independent model/ABI checks also run on platforms without native Fortran.
set(findloc_source "${BINARY_DIR}/findloc-contract.c")
set(findloc_header "${BINARY_DIR}/findloc-contract.h")
set(findloc_executable "${BINARY_DIR}/generated_findloc_contract_test")
execute_process(
    COMMAND "${F2C}" "${SOURCE_DIR}/test/fixtures/findloc_contract.f90"
            -o "${findloc_source}" --header "${findloc_header}"
    RESULT_VARIABLE findloc_translate_status
    OUTPUT_VARIABLE findloc_translate_output
    ERROR_VARIABLE findloc_translate_error)
if(NOT findloc_translate_status EQUAL 0)
    message(FATAL_ERROR "FINDLOC ABI translation failed: ${findloc_translate_error}${findloc_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    set(findloc_executable "${findloc_executable}.exe")
    set(findloc_compile_command ${CC_COMMAND} /std:c17 /O2 /W4 "/I${BINARY_DIR}"
        "${findloc_source}" "${SOURCE_DIR}/test/generated/findloc_contract.c"
        "/Fe${findloc_executable}")
else()
    set(findloc_compile_command ${CC_COMMAND} -std=c17 -O2 -Wall -Wextra -Wpedantic
        -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
        "-I${BINARY_DIR}" "${findloc_source}" "${SOURCE_DIR}/test/generated/findloc_contract.c"
        -lm -o "${findloc_executable}")
endif()
execute_process(COMMAND ${findloc_compile_command}
    RESULT_VARIABLE findloc_compile_status
    OUTPUT_VARIABLE findloc_compile_output
    ERROR_VARIABLE findloc_compile_error)
if(NOT findloc_compile_status EQUAL 0)
    message(FATAL_ERROR "FINDLOC ABI client did not compile: ${findloc_compile_error}${findloc_compile_output}")
endif()
execute_process(COMMAND "${findloc_executable}"
    RESULT_VARIABLE findloc_run_status
    OUTPUT_VARIABLE findloc_run_output
    ERROR_VARIABLE findloc_run_error)
if(NOT findloc_run_status EQUAL 0)
    message(FATAL_ERROR "FINDLOC ABI client failed: ${findloc_run_error}${findloc_run_output}")
endif()
