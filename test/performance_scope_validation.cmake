if(NOT DEFINED SH OR NOT DEFINED F2C OR NOT DEFINED SCRIPT)
    message(FATAL_ERROR "SH, F2C and SCRIPT are required")
endif()

execute_process(
    COMMAND "${SH}" "${SCRIPT}" "${F2C}" invalid
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 2)
    message(FATAL_ERROR "invalid performance scope returned ${result}: ${output}${error}")
endif()
if(NOT error MATCHES "unsupported performance scope: invalid")
    message(FATAL_ERROR "invalid performance scope diagnostic was not stable: ${error}")
endif()
