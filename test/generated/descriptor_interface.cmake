# Exercise public headers in a separate translation unit, including three
# independently generated interfaces with the same canonical descriptor ABI.
set(descriptor_sources "")
foreach(descriptor_fixture descriptor_interface descriptor_interface_second descriptor_byte_views)
    string(REPLACE "_" "-" descriptor_stem "${descriptor_fixture}")
    set(descriptor_source "${BINARY_DIR}/${descriptor_stem}.c")
    execute_process(
        COMMAND "${F2C}" "${SOURCE_DIR}/test/fixtures/${descriptor_fixture}.f90"
                -o "${descriptor_source}" --header "${BINARY_DIR}/${descriptor_stem}.h"
        RESULT_VARIABLE descriptor_status OUTPUT_VARIABLE descriptor_output
        ERROR_VARIABLE descriptor_error)
    if(NOT descriptor_status EQUAL 0)
        message(FATAL_ERROR "descriptor interface translation failed: ${descriptor_error}${descriptor_output}")
    endif()
    list(APPEND descriptor_sources "${descriptor_source}")
endforeach()
if(F2C_MSVC_FRONTEND)
    set(descriptor_profiles 2)
else()
    set(descriptor_profiles 0 2 3)
    if(CC_ID MATCHES "Clang" OR CC_ID STREQUAL "GNU")
        list(APPEND descriptor_profiles sanitized)
    endif()
endif()
foreach(descriptor_profile IN LISTS descriptor_profiles)
    set(descriptor_executable "${BINARY_DIR}/descriptor-interface-${descriptor_profile}")
    if(F2C_MSVC_FRONTEND)
        set(descriptor_executable "${descriptor_executable}.exe")
        set(descriptor_command ${CC_COMMAND} /std:c17 /O2 /W4 /WX "/I${BINARY_DIR}"
            ${descriptor_sources} "${SOURCE_DIR}/test/generated/descriptor_interface_contract.c"
            "/Fe${descriptor_executable}")
    else()
        set(descriptor_optimization "-O${descriptor_profile}")
        set(descriptor_instrumentation "")
        if(descriptor_profile STREQUAL "sanitized")
            set(descriptor_optimization -O1)
            set(descriptor_instrumentation -g -fsanitize=address,undefined -fno-sanitize-recover=all)
        endif()
        set(descriptor_command ${CC_COMMAND} -std=c17 ${descriptor_optimization}
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
            -Wmissing-prototypes -Werror ${descriptor_instrumentation} "-I${BINARY_DIR}"
            ${descriptor_sources} "${SOURCE_DIR}/test/generated/descriptor_interface_contract.c"
            -lm -o "${descriptor_executable}")
    endif()
    execute_process(COMMAND ${descriptor_command}
        RESULT_VARIABLE descriptor_status OUTPUT_VARIABLE descriptor_output
        ERROR_VARIABLE descriptor_error)
    if(NOT descriptor_status EQUAL 0)
        message(FATAL_ERROR "descriptor interface ${descriptor_profile} compilation failed: ${descriptor_error}${descriptor_output}")
    endif()
    execute_process(COMMAND "${descriptor_executable}"
        RESULT_VARIABLE descriptor_status OUTPUT_VARIABLE descriptor_output
        ERROR_VARIABLE descriptor_error)
    if(NOT descriptor_status EQUAL 0)
        message(FATAL_ERROR "descriptor interface ${descriptor_profile} ABI failed (exit ${descriptor_status}): ${descriptor_error}${descriptor_output}")
    endif()
endforeach()
