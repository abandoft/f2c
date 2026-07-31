if(NOT DEFINED F2C OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "F2C and BINARY_DIR are required")
endif()

set(work "${BINARY_DIR}/cli-module-resolution")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(input "${work}/external-consumer.f90")
file(
    WRITE
    "${input}"
    "program external_consumer\n  use external_provider\nend program external_consumer\n"
)

execute_process(
    COMMAND "${F2C}" "${input}" -o -
    RESULT_VARIABLE missing_status
    OUTPUT_QUIET
    ERROR_VARIABLE missing_error
)
if(
    missing_status EQUAL 0
    OR NOT missing_error MATCHES "non-intrinsic module 'external_provider' is not present"
)
    message(FATAL_ERROR "CLI silently accepted an unresolved module: ${missing_error}")
endif()

execute_process(
    COMMAND "${F2C}" --external-module External_Provider "${input}" -o -
    RESULT_VARIABLE separate_status
    OUTPUT_VARIABLE separate_output
    ERROR_VARIABLE separate_error
)
if(NOT separate_status EQUAL 0 OR NOT separate_output MATCHES "int main\\(void\\)")
    message(
        FATAL_ERROR
        "CLI separate external-module option failed: ${separate_error}${separate_output}"
    )
endif()

execute_process(
    COMMAND "${F2C}" --external-module=External_Provider "${input}" -o -
    RESULT_VARIABLE joined_status
    OUTPUT_VARIABLE joined_output
    ERROR_VARIABLE joined_error
)
if(NOT joined_status EQUAL 0 OR NOT joined_output MATCHES "int main\\(void\\)")
    message(
        FATAL_ERROR
        "CLI joined external-module option failed: ${joined_error}${joined_output}"
    )
endif()

execute_process(
    COMMAND "${F2C}" --external-module bad-name "${input}" -o -
    RESULT_VARIABLE invalid_status
    OUTPUT_QUIET
    ERROR_VARIABLE invalid_error
)
if(invalid_status EQUAL 0 OR NOT invalid_error MATCHES "is not a valid Fortran name")
    message(FATAL_ERROR "CLI accepted an invalid external-module name: ${invalid_error}")
endif()

execute_process(
    COMMAND
        "${F2C}"
        --external-module external_provider
        --external-module EXTERNAL_PROVIDER
        "${input}"
        -o
        -
    RESULT_VARIABLE duplicate_status
    OUTPUT_QUIET
    ERROR_VARIABLE duplicate_error
)
if(duplicate_status EQUAL 0 OR NOT duplicate_error MATCHES "is listed more than once")
    message(FATAL_ERROR "CLI accepted a duplicate external-module name: ${duplicate_error}")
endif()

set(intrinsic_input "${work}/unknown-intrinsic.f90")
file(
    WRITE
    "${intrinsic_input}"
    "program unknown_intrinsic\n  use, intrinsic :: vendor_magic\nend program unknown_intrinsic\n"
)
execute_process(
    COMMAND "${F2C}" --external-module vendor_magic "${intrinsic_input}" -o -
    RESULT_VARIABLE intrinsic_status
    OUTPUT_QUIET
    ERROR_VARIABLE intrinsic_error
)
if(intrinsic_status EQUAL 0 OR NOT intrinsic_error MATCHES "intrinsic module.*is not supported")
    message(FATAL_ERROR "external permission overrode intrinsic-module semantics: ${intrinsic_error}")
endif()
