set(live_generated "${BINARY_DIR}/generated_live_external_state.c")
execute_process(
    COMMAND "${F2C}" -o "${live_generated}"
            "${SOURCE_DIR}/test/fixtures/live_external_state.f90"
    RESULT_VARIABLE live_translate_status
    OUTPUT_VARIABLE live_translate_output ERROR_VARIABLE live_translate_error)
if(NOT live_translate_status EQUAL 0)
    message(FATAL_ERROR "live state translation failed: ${live_translate_error}${live_translate_output}")
endif()
foreach(live_optimization IN ITEMS 0 2 3)
    set(live_executable "${BINARY_DIR}/live_state_O${live_optimization}")
    if(F2C_MSVC_FRONTEND)
        set(live_executable "${live_executable}.exe")
        set(live_optimization_flag /O2)
        if(live_optimization EQUAL 0)
            set(live_optimization_flag /Od)
        endif()
        set(live_compile_command ${CC_COMMAND} /std:c17 ${live_optimization_flag} /W4
            "/I${BINARY_DIR}" "${SOURCE_DIR}/test/generated/live_state_contracts.c"
            "/Fe${live_executable}")
    else()
        set(live_compile_command ${CC_COMMAND} -std=c17 "-O${live_optimization}"
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
            -Wmissing-prototypes -Werror "-I${BINARY_DIR}"
            "${SOURCE_DIR}/test/generated/live_state_contracts.c" -lm -o "${live_executable}")
    endif()
    execute_process(COMMAND ${live_compile_command}
        RESULT_VARIABLE live_compile_status
        OUTPUT_VARIABLE live_compile_output ERROR_VARIABLE live_compile_error)
    if(NOT live_compile_status EQUAL 0)
        message(FATAL_ERROR "live state strict C17 failed: ${live_compile_error}${live_compile_output}")
    endif()
    execute_process(COMMAND "${live_executable}"
        RESULT_VARIABLE live_run_status
        OUTPUT_VARIABLE live_run_output ERROR_VARIABLE live_run_error)
    string(REPLACE "\r\n" "\n" live_run_output "${live_run_output}")
    if(NOT live_run_status EQUAL 0 OR
       NOT live_run_output STREQUAL "external live state contracts passed\n")
        message(FATAL_ERROR "live state execution failed: ${live_run_error}${live_run_output}")
    endif()
endforeach()
