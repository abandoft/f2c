if(NOT DEFINED F2C OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "F2C, SOURCE_DIR and BINARY_DIR are required")
endif()

set(work "${BINARY_DIR}/cli-io")
set(input "${SOURCE_DIR}/test/fixtures/daxpy.f90")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(preprocessor_input "${work}/conditional.F90")
file(
    WRITE
    "${preprocessor_input}"
    "#ifdef SELECTED\nsubroutine selected()\nend subroutine selected\n#else\nsubroutine fallback()\nend subroutine fallback\n#endif\n"
)
execute_process(
    COMMAND "${F2C}" -DSELECTED=1 "${preprocessor_input}" -o -
    RESULT_VARIABLE define_status
    OUTPUT_VARIABLE define_output
    ERROR_VARIABLE define_error
)
if(
    NOT define_status EQUAL 0
    OR NOT define_output MATCHES "void selected\\(void\\)"
    OR define_output MATCHES "fallback"
)
    message(FATAL_ERROR "CLI -D conditional definition failed: ${define_error}${define_output}")
endif()

execute_process(
    COMMAND "${F2C}" -D SELECTED=1 -U SELECTED "${preprocessor_input}" -o -
    RESULT_VARIABLE undefine_status
    OUTPUT_VARIABLE undefine_output
    ERROR_VARIABLE undefine_error
)
if(
    NOT undefine_status EQUAL 0
    OR NOT undefine_output MATCHES "void fallback\\(void\\)"
    OR undefine_output MATCHES "void selected\\(void\\)"
)
    message(FATAL_ERROR "CLI -U conditional definition failed: ${undefine_error}${undefine_output}")
endif()

execute_process(
    COMMAND "${F2C}" -D1INVALID "${preprocessor_input}" -o -
    RESULT_VARIABLE invalid_define_status
    OUTPUT_QUIET
    ERROR_VARIABLE invalid_define_error
)
if(invalid_define_status EQUAL 0 OR NOT invalid_define_error MATCHES "invalid conditional")
    message(FATAL_ERROR "CLI accepted an invalid -D name")
endif()

set(include_directory "${work}/include")
file(MAKE_DIRECTORY "${include_directory}")
file(WRITE "${include_directory}/values.inc" "#define INCLUDED_CLI_VALUE 23\n")
set(include_input "${work}/include_main.F90")
file(
    WRITE
    "${include_input}"
    "#include <values.inc>\nsubroutine cli_include(value)\ninteger :: value\nvalue = INCLUDED_CLI_VALUE\nend subroutine cli_include\n"
)
execute_process(
    COMMAND "${F2C}" "-I${include_directory}" "${include_input}" -o -
    RESULT_VARIABLE include_status
    OUTPUT_VARIABLE include_output
    ERROR_VARIABLE include_error
)
if(NOT include_status EQUAL 0 OR NOT include_output MATCHES "\\(\\*value\\) = 23")
    message(FATAL_ERROR "CLI -I include resolution failed: ${include_error}${include_output}")
endif()

file(WRITE "${include_directory}/fortran_body.inc" "integer :: local_value\nlocal_value = 37\n")
file(
    WRITE
    "${work}/fortran_include.f90"
    "subroutine cli_fortran_include()\ninclude 'fortran_body.inc'\nend subroutine cli_fortran_include\n"
)
execute_process(
    COMMAND "${F2C}" -I "${include_directory}" "${work}/fortran_include.f90" -o -
    RESULT_VARIABLE fortran_include_status
    OUTPUT_VARIABLE fortran_include_output
    ERROR_VARIABLE fortran_include_error
)
if(
    NOT fortran_include_status EQUAL 0
    OR NOT fortran_include_output MATCHES "local_value = 37"
)
    message(
        FATAL_ERROR
        "CLI Fortran INCLUDE resolution failed: ${fortran_include_error}${fortran_include_output}"
    )
endif()

file(WRITE "${work}/local.inc" "#define LOCAL_CLI_VALUE 29\n")
file(
    WRITE
    "${work}/quoted_main.F90"
    "#include \"local.inc\"\nsubroutine quoted_include(value)\ninteger :: value\nvalue = LOCAL_CLI_VALUE\nend subroutine quoted_include\n"
)
execute_process(
    COMMAND "${F2C}" "${work}/quoted_main.F90" -o -
    RESULT_VARIABLE quoted_include_status
    OUTPUT_VARIABLE quoted_include_output
    ERROR_VARIABLE quoted_include_error
)
if(
    NOT quoted_include_status EQUAL 0
    OR NOT quoted_include_output MATCHES "\\(\\*value\\) = 29"
)
    message(
        FATAL_ERROR
        "CLI quoted include resolution failed: ${quoted_include_error}${quoted_include_output}"
    )
endif()

file(MAKE_DIRECTORY "${work}/cycle")
file(WRITE "${work}/cycle/loop.inc" "#include \"../cycle/loop.inc\"\n")
execute_process(
    COMMAND "${F2C}" "${work}/cycle/loop.inc" -o -
    RESULT_VARIABLE include_cycle_status
    OUTPUT_QUIET
    ERROR_VARIABLE include_cycle_error
)
if(include_cycle_status EQUAL 0 OR NOT include_cycle_error MATCHES "recursive include cycle")
    message(FATAL_ERROR "CLI include paths were not normalized: ${include_cycle_error}")
endif()

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
