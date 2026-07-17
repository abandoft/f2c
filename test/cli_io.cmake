if(NOT DEFINED F2C OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "F2C, SOURCE_DIR and BINARY_DIR are required")
endif()

set(work "${BINARY_DIR}/cli-io")
set(input "${SOURCE_DIR}/test/fixtures/daxpy.f90")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

execute_process(
    COMMAND "${F2C}" - -o -
    INPUT_FILE "${input}"
    RESULT_VARIABLE stream_status
    OUTPUT_VARIABLE stream_output
    ERROR_VARIABLE stream_error
)
if(NOT stream_status EQUAL 0 OR NOT stream_output MATCHES "void daxpy")
    message(FATAL_ERROR "stdin/stdout translation failed: ${stream_error}${stream_output}")
endif()

execute_process(
    COMMAND "${F2C}" - -
    INPUT_FILE "${input}"
    RESULT_VARIABLE repeated_stdin_status
    OUTPUT_QUIET
    ERROR_VARIABLE repeated_stdin_error
)
if(repeated_stdin_status EQUAL 0 OR NOT repeated_stdin_error MATCHES "only once")
    message(FATAL_ERROR "repeated stdin was not rejected deterministically")
endif()

set(code_output "${work}/result.c")
file(WRITE "${code_output}" "preserve-existing-output\n")
execute_process(
    COMMAND "${F2C}" "${input}" -o "${code_output}"
            --header "${work}/missing/result.h"
    RESULT_VARIABLE transaction_status
    OUTPUT_QUIET
    ERROR_VARIABLE transaction_error
)
if(transaction_status EQUAL 0)
    message(FATAL_ERROR "output transaction unexpectedly succeeded")
endif()
file(READ "${code_output}" preserved_output)
if(NOT preserved_output STREQUAL "preserve-existing-output\n")
    message(FATAL_ERROR "failed header staging modified the existing C output")
endif()

file(GLOB leaked_temporary_files "${work}/*.f2c-*")
if(leaked_temporary_files)
    message(FATAL_ERROR "CLI left temporary output artifacts: ${leaked_temporary_files}")
endif()

set(directory_output "${work}/directory-output")
file(MAKE_DIRECTORY "${directory_output}")
execute_process(
    COMMAND "${F2C}" "${input}" -o "${directory_output}"
    RESULT_VARIABLE directory_status
    OUTPUT_QUIET
    ERROR_QUIET
)
if(directory_status EQUAL 0 OR NOT IS_DIRECTORY "${directory_output}")
    message(FATAL_ERROR "CLI replaced a directory output target")
endif()
