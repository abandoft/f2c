set(work "${BINARY_DIR}/reproducible-output")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

foreach(run RANGE 1 2)
    execute_process(
        COMMAND "${F2C}"
                -o "${work}/project-${run}.c"
                --header "${work}/project-${run}.h"
                "${SOURCE_DIR}/test/fixtures/project_caller.f90"
                "${SOURCE_DIR}/test/fixtures/project_definition.f90"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "deterministic translation run ${run} failed: ${error}${output}")
    endif()
endforeach()

foreach(run RANGE 1 2)
    execute_process(
        COMMAND "${F2C}"
                -o "${work}/procedure-${run}.c"
                --header "${work}/procedure-${run}.h"
                "${SOURCE_DIR}/test/fixtures/procedure_interface.f90"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "deterministic PROCEDURE translation run ${run} failed: ${error}${output}")
    endif()
endforeach()

foreach(run RANGE 1 2)
    execute_process(
        COMMAND "${F2C}"
                -o "${work}/deferred-${run}.c"
                --header "${work}/deferred-${run}.h"
                "${SOURCE_DIR}/test/fixtures/deferred_character.f90"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
                "deterministic deferred CHARACTER translation run ${run} failed: ${error}${output}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${work}/project-1.c" "${work}/project-2.c"
    RESULT_VARIABLE code_diff)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${work}/project-1.h" "${work}/project-2.h"
    RESULT_VARIABLE header_diff)
if(NOT code_diff EQUAL 0 OR NOT header_diff EQUAL 0)
    message(FATAL_ERROR "repeated project translation produced non-deterministic output")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${work}/procedure-1.c" "${work}/procedure-2.c"
    RESULT_VARIABLE procedure_code_diff)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${work}/procedure-1.h" "${work}/procedure-2.h"
    RESULT_VARIABLE procedure_header_diff)
if(NOT procedure_code_diff EQUAL 0 OR NOT procedure_header_diff EQUAL 0)
    message(FATAL_ERROR "repeated PROCEDURE translation produced non-deterministic output")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${work}/deferred-1.c" "${work}/deferred-2.c"
    RESULT_VARIABLE deferred_code_diff)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${work}/deferred-1.h" "${work}/deferred-2.h"
    RESULT_VARIABLE deferred_header_diff)
if(NOT deferred_code_diff EQUAL 0 OR NOT deferred_header_diff EQUAL 0)
    message(FATAL_ERROR "repeated deferred CHARACTER translation produced non-deterministic output")
endif()
