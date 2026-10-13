set(model_source "${BINARY_DIR}/intrinsic-result-model.c")
set(model_header "${BINARY_DIR}/intrinsic-result-model.h")
execute_process(
    COMMAND "${F2C}" "${SOURCE_DIR}/test/fixtures/intrinsic_result_model.f90"
            -o "${model_source}" --header "${model_header}"
    RESULT_VARIABLE model_translate_status
    OUTPUT_VARIABLE model_translate_output ERROR_VARIABLE model_translate_error)
if(NOT model_translate_status EQUAL 0)
    message(FATAL_ERROR "intrinsic result model translation failed: ${model_translate_error}${model_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    set(model_profiles 2)
else()
    set(model_profiles 0 2 3)
    if(CC_ID MATCHES "Clang" OR CC_ID STREQUAL "GNU")
        list(APPEND model_profiles sanitized)
    endif()
endif()
foreach(model_profile IN LISTS model_profiles)
    set(model_executable "${BINARY_DIR}/intrinsic-result-contract-${model_profile}")
    if(F2C_MSVC_FRONTEND)
        set(model_executable "${model_executable}.exe")
        set(model_compile_command ${CC_COMMAND} /std:c17 /O2 /W4 /WX "/I${BINARY_DIR}"
            "${model_source}" "${SOURCE_DIR}/test/generated/intrinsic_result_contract.c"
            "/Fe${model_executable}")
    else()
        set(model_optimization "-O${model_profile}")
        set(model_instrumentation "")
        if(model_profile STREQUAL "sanitized")
            set(model_optimization -O1)
            set(model_instrumentation -g -fsanitize=address,undefined -fno-sanitize-recover=all)
        endif()
        set(model_compile_command ${CC_COMMAND} -std=c17 ${model_optimization}
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
            -Wmissing-prototypes -Werror ${model_instrumentation} "-I${BINARY_DIR}"
            "${model_source}" "${SOURCE_DIR}/test/generated/intrinsic_result_contract.c"
            -lm -o "${model_executable}")
    endif()
    execute_process(COMMAND ${model_compile_command}
        RESULT_VARIABLE model_compile_status OUTPUT_VARIABLE model_compile_output
        ERROR_VARIABLE model_compile_error)
    if(NOT model_compile_status EQUAL 0)
        message(FATAL_ERROR "intrinsic result ${model_profile} compilation failed: ${model_compile_error}${model_compile_output}")
    endif()
    execute_process(COMMAND "${model_executable}"
        RESULT_VARIABLE model_run_status OUTPUT_VARIABLE model_run_output
        ERROR_VARIABLE model_run_error)
    if(NOT model_run_status EQUAL 0)
        message(FATAL_ERROR "intrinsic result ${model_profile} ABI contract failed: ${model_run_error}${model_run_output}")
    endif()
endforeach()
