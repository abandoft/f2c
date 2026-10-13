set(copy_source "${BINARY_DIR}/array-copy-contract.c")
execute_process(COMMAND "${COPY_EMITTER}"
    OUTPUT_FILE "${copy_source}"
    RESULT_VARIABLE copy_emit_status ERROR_VARIABLE copy_emit_error)
if(NOT copy_emit_status EQUAL 0)
    message(FATAL_ERROR "array snapshot contract emission failed: ${copy_emit_error}")
endif()
if(F2C_MSVC_FRONTEND)
    set(copy_profiles 2)
else()
    set(copy_profiles 0 2 3)
    if(CC_ID MATCHES "Clang" OR CC_ID STREQUAL "GNU")
        list(APPEND copy_profiles sanitized)
    endif()
endif()
foreach(copy_profile IN LISTS copy_profiles)
    set(copy_executable "${BINARY_DIR}/array-copy-contract-${copy_profile}")
    if(F2C_MSVC_FRONTEND)
        set(copy_executable "${copy_executable}.exe")
        set(copy_compile_command ${CC_COMMAND} /std:c17 /O2 /W4 /WX
            "${copy_source}" "/Fe${copy_executable}")
    else()
        set(copy_optimization "-O${copy_profile}")
        set(copy_instrumentation "")
        if(copy_profile STREQUAL "sanitized")
            set(copy_optimization -O1)
            set(copy_instrumentation -g -fsanitize=address,undefined -fno-sanitize-recover=all)
        endif()
        set(copy_compile_command ${CC_COMMAND} -std=c17 ${copy_optimization}
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
            -Wmissing-prototypes -Werror ${copy_instrumentation}
            "${copy_source}" -o "${copy_executable}")
    endif()
    execute_process(COMMAND ${copy_compile_command}
        RESULT_VARIABLE copy_compile_status OUTPUT_VARIABLE copy_compile_output
        ERROR_VARIABLE copy_compile_error)
    if(NOT copy_compile_status EQUAL 0)
        message(FATAL_ERROR "array snapshot ${copy_profile} compilation failed: ${copy_compile_error}${copy_compile_output}")
    endif()
    execute_process(COMMAND "${copy_executable}"
        RESULT_VARIABLE copy_run_status ERROR_VARIABLE copy_run_error)
    if(NOT copy_run_status EQUAL 0)
        message(FATAL_ERROR "array snapshot ${copy_profile} contract failed: ${copy_run_error}")
    endif()
    execute_process(COMMAND "${copy_executable}" --overflow
        RESULT_VARIABLE copy_overflow_status ERROR_VARIABLE copy_overflow_error)
    if(NOT copy_overflow_status EQUAL 99)
        message(FATAL_ERROR "array snapshot ${copy_profile} overflow did not fail before address evaluation: ${copy_overflow_status} ${copy_overflow_error}")
    endif()
endforeach()
