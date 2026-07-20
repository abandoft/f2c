set(F2C_MSVC_FRONTEND FALSE)
if(CC_ID STREQUAL "MSVC" OR CC_FRONTEND_VARIANT STREQUAL "MSVC")
    set(F2C_MSVC_FRONTEND TRUE)
endif()

set(CC_COMMAND "${CC}")
find_program(F2C_GFORTRAN NAMES gfortran)
if(F2C_MSVC_FRONTEND AND
   ("$ENV{INCLUDE}" STREQUAL "" OR "$ENV{LIB}" STREQUAL ""))
    string(REGEX REPLACE ",.*$" "" visual_studio_root "${VS_INSTALLATION}")
    set(visual_studio_environment
        "${visual_studio_root}/Common7/Tools/VsDevCmd.bat")
    if(NOT EXISTS "${visual_studio_environment}")
        message(FATAL_ERROR
                "Visual Studio development environment not found: ${visual_studio_environment}")
    endif()

    set(target_architecture "${VS_TARGET_ARCHITECTURE}")
    if(target_architecture STREQUAL "Win32")
        set(target_architecture x86)
    elseif(target_architecture STREQUAL "ARM64")
        set(target_architecture arm64)
    endif()
    if(target_architecture STREQUAL "")
        message(FATAL_ERROR "the Visual Studio target architecture was not provided")
    endif()

    set(host_option "")
    if(NOT VS_HOST_ARCHITECTURE STREQUAL "")
        string(TOLOWER "${VS_HOST_ARCHITECTURE}" host_architecture)
        set(host_option "-host_arch=${host_architecture}")
    endif()

    file(TO_NATIVE_PATH "${visual_studio_environment}" visual_studio_environment_native)
    file(TO_NATIVE_PATH "${CC}" compiler_native)
    set(compiler_wrapper "${BINARY_DIR}/f2c-msvc-compiler.cmd")
    file(
        WRITE "${compiler_wrapper}"
        "@echo off\r\ncall \"${visual_studio_environment_native}\" -no_logo -arch=${target_architecture} ${host_option} >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n\"${compiler_native}\" %*\r\n"
    )
    set(CC_COMMAND cmd.exe /d /c call "${compiler_wrapper}")
endif()

set(generated "${BINARY_DIR}/generated_daxpy.c")
set(executable "${BINARY_DIR}/generated_daxpy_test")

execute_process(
    COMMAND "${F2C}" -o "${generated}" "${SOURCE_DIR}/test/fixtures/daxpy.f90"
    RESULT_VARIABLE translate_status
    OUTPUT_VARIABLE translate_output
    ERROR_VARIABLE translate_error)
if(NOT translate_status EQUAL 0)
    message(FATAL_ERROR "translation failed: ${translate_error}${translate_output}")
endif()

foreach(
    io_fixture
    IN ITEMS
       assigned_format
       action_statements
       legacy_control
       print_formats
       formatted_internal
       formatted_record_input
       namelist_internal
       pointer_semantics
       pointer_dummy
       procedure_pointer
       procedure_pointer_component
       vector_subscript
       reduction_intrinsics
       transform_intrinsics
       transform_character_derived
       derived_namelist
       dynamic_derived_namelist
       namelist_auto_allocate
       nonadvancing_io
       file_control
       record_io
       iolength
       corrupt_record_io
       allocatable_component
       finalization
       block_finalization
       inheritance
       polymorphic_dummy
       select_type
       type_bound_dispatch
       deferred_binding
       defined_io
       derived_type
       derived_pointer_component
       structure_constructor
       internal_file_array
       internal_file_semantics
       module_use
       module_access
       module_generic
       module_generic_explicit
       defined_operator
       defined_assignment
       module_dependency_order
       module_interface_use
       module_procedure
       module_allocatable
       allocatable_scope
       many_arguments
       statement_function
       statement_function_single_eval
       do_semantics
       data_statement
       lexical_literals
       select_case
       named_constructs
       where_construct
       elemental_procedure
       array_inquiry
       bit_intrinsics
       real_representation_intrinsics
       character_intrinsics
       descriptor_temporary
       equivalence_group
       declaration_matrix
       complex_arithmetic
)
    set(io_generated "${BINARY_DIR}/generated_${io_fixture}.c")
    set(io_executable "${BINARY_DIR}/generated_${io_fixture}_test")
    set(io_source "${SOURCE_DIR}/test/fixtures/${io_fixture}.f90")
    if(io_fixture STREQUAL "assigned_format" OR io_fixture STREQUAL "legacy_control")
        set(io_source "${SOURCE_DIR}/test/fixtures/${io_fixture}.f")
    endif()
    execute_process(
        COMMAND "${F2C}" -o "${io_generated}"
                "${io_source}"
        RESULT_VARIABLE io_translate_status
        OUTPUT_VARIABLE io_translate_output
        ERROR_VARIABLE io_translate_error)
    if(NOT io_translate_status EQUAL 0)
        message(FATAL_ERROR
                "${io_fixture} translation failed: ${io_translate_error}${io_translate_output}")
    endif()
    if(F2C_MSVC_FRONTEND)
        execute_process(
            COMMAND ${CC_COMMAND} /std:c17 /O2 /W4 "${io_generated}"
                    "/Fe${io_executable}.exe"
            RESULT_VARIABLE io_compile_status
            OUTPUT_VARIABLE io_compile_output
            ERROR_VARIABLE io_compile_error)
        set(io_executable "${io_executable}.exe")
    else()
        execute_process(
            COMMAND ${CC_COMMAND} -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                    -Wstrict-prototypes -Wmissing-prototypes -Werror "${io_generated}"
                    -lm -o "${io_executable}"
            RESULT_VARIABLE io_compile_status
            OUTPUT_VARIABLE io_compile_output
            ERROR_VARIABLE io_compile_error)
    endif()
    if(NOT io_compile_status EQUAL 0)
        message(FATAL_ERROR
                "${io_fixture} generated C17 did not compile: ${io_compile_error}${io_compile_output}")
    endif()
    execute_process(
        COMMAND "${io_executable}"
        RESULT_VARIABLE io_run_status
        OUTPUT_VARIABLE io_run_output
        ERROR_VARIABLE io_run_error)
    if(NOT io_run_status EQUAL 0)
        message(FATAL_ERROR
                "${io_fixture} generated executable failed: ${io_run_error}")
    endif()
    if(io_fixture STREQUAL "print_formats")
        string(REPLACE "\r\n" "\n" io_run_output "${io_run_output}")
        set(print_formats_expected
            "LITERAL   7\nLABEL  1  2  3\nRUNTIME   9\n\n")
        if(NOT io_run_output STREQUAL print_formats_expected)
            message(FATAL_ERROR
                    "PRINT format output differs from Fortran semantics: [${io_run_output}]")
        endif()
    elseif(io_fixture STREQUAL "assigned_format")
        string(REPLACE "\r\n" "\n" io_run_output "${io_run_output}")
        if(NOT io_run_output STREQUAL "ASSIGNED  7\n")
            message(FATAL_ERROR
                    "assigned FORMAT output differs from Fortran semantics: [${io_run_output}]")
    elseif(io_fixture STREQUAL "module_access" OR io_fixture STREQUAL "module_generic" OR
           io_fixture STREQUAL "module_generic_explicit" OR
           io_fixture STREQUAL "defined_operator" OR
           io_fixture STREQUAL "defined_assignment")
        string(REPLACE "\r\n" "\n" io_run_output "${io_run_output}")
        if(io_fixture STREQUAL "module_access")
            set(module_expected_output "8\n")
        elseif(io_fixture STREQUAL "module_generic")
            set(module_expected_output "4 6.0\n")
        elseif(io_fixture STREQUAL "module_generic_explicit")
            set(module_expected_output "5 7.0\n")
        elseif(io_fixture STREQUAL "defined_operator")
            set(module_expected_output "23 12.5 42 4 5\n")
        else()
            set(module_expected_output "14 2 4 6\n")
        endif()
        if(NOT io_run_output STREQUAL module_expected_output)
            message(FATAL_ERROR
                    "${io_fixture} output differs from Fortran semantics: [${io_run_output}]")
        endif()
        if(F2C_GFORTRAN)
            set(module_native "${BINARY_DIR}/native_${io_fixture}_test")
            execute_process(
                COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                        "${io_source}" -o "${module_native}"
                RESULT_VARIABLE module_native_compile_status
                OUTPUT_VARIABLE module_native_compile_output
                ERROR_VARIABLE module_native_compile_error)
            if(NOT module_native_compile_status EQUAL 0)
                message(FATAL_ERROR
                        "native ${io_fixture} oracle did not compile: ${module_native_compile_error}${module_native_compile_output}")
            endif()
            execute_process(
                COMMAND "${module_native}"
                RESULT_VARIABLE module_native_run_status
                OUTPUT_VARIABLE module_native_output
                ERROR_VARIABLE module_native_error)
            string(REPLACE "\r\n" "\n" module_native_output "${module_native_output}")
            if(NOT module_native_run_status EQUAL 0 OR
               NOT io_run_output STREQUAL module_native_output)
                message(FATAL_ERROR
                        "generated/native ${io_fixture} mismatch: generated='${io_run_output}' native='${module_native_output}' error='${module_native_error}'")
            endif()
        endif()
    endif()
    endif()
endforeach()

if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c11 /c "${generated}"
                "/Fo${BINARY_DIR}/generated_pre_c17_reject.obj"
        RESULT_VARIABLE pre_c17_status
        OUTPUT_QUIET
        ERROR_QUIET)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${generated}"
                "${SOURCE_DIR}/test/fixtures/daxpy_harness.c" "/Fe${executable}.exe"
        RESULT_VARIABLE compile_status
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error)
    set(executable "${executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c11 -c "${generated}"
                -o "${BINARY_DIR}/generated_pre_c17_reject.o"
        RESULT_VARIABLE pre_c17_status
        OUTPUT_QUIET
        ERROR_QUIET)
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${generated}"
                "${SOURCE_DIR}/test/fixtures/daxpy_harness.c" -lm -o "${executable}"
        RESULT_VARIABLE compile_status
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error)
endif()
if(pre_c17_status EQUAL 0)
    message(FATAL_ERROR "generated source unexpectedly accepted a pre-C17 compilation mode")
endif()
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "generated C17 did not compile: ${compile_error}${compile_output}")
endif()

execute_process(COMMAND "${executable}" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "generated DAXPY implementation failed its correctness check")
endif()

set(project_generated "${BINARY_DIR}/generated_project.c")
set(project_header "${BINARY_DIR}/generated_project.h")
set(project_executable "${BINARY_DIR}/generated_project_test")
execute_process(
    COMMAND "${F2C}" -o "${project_generated}" --header "${project_header}"
            "${SOURCE_DIR}/test/fixtures/project_caller.f90"
            "${SOURCE_DIR}/test/fixtures/project_definition.f90"
    RESULT_VARIABLE project_translate_status
    OUTPUT_VARIABLE project_translate_output
    ERROR_VARIABLE project_translate_error)
if(NOT project_translate_status EQUAL 0)
    message(FATAL_ERROR
            "project translation failed: ${project_translate_error}${project_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${project_generated}"
                "${SOURCE_DIR}/test/fixtures/project_harness.c" "/I${BINARY_DIR}"
                "/Fe${project_executable}.exe"
        RESULT_VARIABLE project_compile_status
        OUTPUT_VARIABLE project_compile_output
        ERROR_VARIABLE project_compile_error)
    set(project_executable "${project_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${project_generated}"
                "${SOURCE_DIR}/test/fixtures/project_harness.c" -I "${BINARY_DIR}"
                -lm -o "${project_executable}"
        RESULT_VARIABLE project_compile_status
        OUTPUT_VARIABLE project_compile_output
        ERROR_VARIABLE project_compile_error)
endif()
if(NOT project_compile_status EQUAL 0)
    message(FATAL_ERROR
            "generated project C17 did not compile: ${project_compile_error}${project_compile_output}")
endif()
execute_process(COMMAND "${project_executable}" RESULT_VARIABLE project_run_status)
if(NOT project_run_status EQUAL 0)
    message(FATAL_ERROR "generated multi-input project failed its correctness check")
endif()

set(section_generated "${BINARY_DIR}/generated_reverse_cube.c")
set(section_executable "${BINARY_DIR}/generated_reverse_cube_test")
execute_process(
    COMMAND "${F2C}" -o "${section_generated}"
            "${SOURCE_DIR}/test/fixtures/reverse_cube.f90"
    RESULT_VARIABLE section_translate_status
    OUTPUT_VARIABLE section_translate_output
    ERROR_VARIABLE section_translate_error)
if(NOT section_translate_status EQUAL 0)
    message(FATAL_ERROR
            "rank-three section translation failed: ${section_translate_error}${section_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${section_generated}"
                "${SOURCE_DIR}/test/fixtures/reverse_cube_harness.c"
                "/Fe${section_executable}.exe"
        RESULT_VARIABLE section_compile_status
        OUTPUT_VARIABLE section_compile_output
        ERROR_VARIABLE section_compile_error)
    set(section_executable "${section_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${section_generated}"
                "${SOURCE_DIR}/test/fixtures/reverse_cube_harness.c" -lm
                -o "${section_executable}"
        RESULT_VARIABLE section_compile_status
        OUTPUT_VARIABLE section_compile_output
        ERROR_VARIABLE section_compile_error)
endif()
if(NOT section_compile_status EQUAL 0)
    message(FATAL_ERROR
            "rank-three section C17 did not compile: ${section_compile_error}${section_compile_output}")
endif()
execute_process(COMMAND "${section_executable}" RESULT_VARIABLE section_run_status)
if(NOT section_run_status EQUAL 0)
    message(FATAL_ERROR "generated rank-three overlapping section assignment is incorrect")
endif()

set(arithmetic_if_generated "${BINARY_DIR}/generated_arithmetic_if.c")
set(arithmetic_if_executable "${BINARY_DIR}/generated_arithmetic_if_test")
execute_process(
    COMMAND "${F2C}" -o "${arithmetic_if_generated}"
            "${SOURCE_DIR}/test/fixtures/arithmetic_if.f90"
    RESULT_VARIABLE arithmetic_if_translate_status
    OUTPUT_VARIABLE arithmetic_if_translate_output
    ERROR_VARIABLE arithmetic_if_translate_error)
if(NOT arithmetic_if_translate_status EQUAL 0)
    message(FATAL_ERROR
            "arithmetic IF translation failed: ${arithmetic_if_translate_error}${arithmetic_if_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${arithmetic_if_generated}"
                "${SOURCE_DIR}/test/fixtures/arithmetic_if_harness.c"
                "/Fe${arithmetic_if_executable}.exe"
        RESULT_VARIABLE arithmetic_if_compile_status
        OUTPUT_VARIABLE arithmetic_if_compile_output
        ERROR_VARIABLE arithmetic_if_compile_error)
    set(arithmetic_if_executable "${arithmetic_if_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${arithmetic_if_generated}"
                "${SOURCE_DIR}/test/fixtures/arithmetic_if_harness.c" -lm
                -o "${arithmetic_if_executable}"
        RESULT_VARIABLE arithmetic_if_compile_status
        OUTPUT_VARIABLE arithmetic_if_compile_output
        ERROR_VARIABLE arithmetic_if_compile_error)
endif()
if(NOT arithmetic_if_compile_status EQUAL 0)
    message(FATAL_ERROR
            "arithmetic IF C17 did not compile: ${arithmetic_if_compile_error}${arithmetic_if_compile_output}")
endif()
execute_process(COMMAND "${arithmetic_if_executable}"
                RESULT_VARIABLE arithmetic_if_run_status)
if(NOT arithmetic_if_run_status EQUAL 0)
    message(FATAL_ERROR "generated arithmetic IF selected an incorrect branch")
endif()

set(assigned_goto_generated "${BINARY_DIR}/generated_assigned_goto.c")
set(assigned_goto_executable "${BINARY_DIR}/generated_assigned_goto_test")
execute_process(
    COMMAND "${F2C}" --fixed-form -o "${assigned_goto_generated}"
            "${SOURCE_DIR}/test/fixtures/assigned_goto.f"
    RESULT_VARIABLE assigned_goto_translate_status
    OUTPUT_VARIABLE assigned_goto_translate_output
    ERROR_VARIABLE assigned_goto_translate_error)
if(NOT assigned_goto_translate_status EQUAL 0)
    message(FATAL_ERROR
            "assigned GOTO translation failed: ${assigned_goto_translate_error}${assigned_goto_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${assigned_goto_generated}"
                "/Fe${assigned_goto_executable}.exe"
        RESULT_VARIABLE assigned_goto_compile_status
        OUTPUT_VARIABLE assigned_goto_compile_output
        ERROR_VARIABLE assigned_goto_compile_error)
    set(assigned_goto_executable "${assigned_goto_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${assigned_goto_generated}" -lm
                -o "${assigned_goto_executable}"
        RESULT_VARIABLE assigned_goto_compile_status
        OUTPUT_VARIABLE assigned_goto_compile_output
        ERROR_VARIABLE assigned_goto_compile_error)
endif()
if(NOT assigned_goto_compile_status EQUAL 0)
    message(FATAL_ERROR
            "assigned GOTO C17 did not compile: ${assigned_goto_compile_error}${assigned_goto_compile_output}")
endif()
execute_process(COMMAND "${assigned_goto_executable}"
                RESULT_VARIABLE assigned_goto_run_status)
if(NOT assigned_goto_run_status EQUAL 0)
    message(FATAL_ERROR "generated assigned GOTO control flow is incorrect")
endif()

set(character_abi_generated "${BINARY_DIR}/generated_character_abi.c")
set(character_abi_header "${BINARY_DIR}/generated_character_abi.h")
set(character_abi_executable "${BINARY_DIR}/generated_character_abi_test")
execute_process(
    COMMAND "${F2C}" -o "${character_abi_generated}" --header "${character_abi_header}"
            "${SOURCE_DIR}/test/fixtures/character_abi.f90"
    RESULT_VARIABLE character_abi_translate_status
    OUTPUT_VARIABLE character_abi_translate_output
    ERROR_VARIABLE character_abi_translate_error)
if(NOT character_abi_translate_status EQUAL 0)
    message(FATAL_ERROR
            "character ABI translation failed: ${character_abi_translate_error}${character_abi_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${character_abi_generated}"
                "${SOURCE_DIR}/test/fixtures/character_abi_harness.c"
                "/I${BINARY_DIR}"
                "/Fe${character_abi_executable}.exe"
        RESULT_VARIABLE character_abi_compile_status
        OUTPUT_VARIABLE character_abi_compile_output
        ERROR_VARIABLE character_abi_compile_error)
    set(character_abi_executable "${character_abi_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${character_abi_generated}"
                "${SOURCE_DIR}/test/fixtures/character_abi_harness.c" -I "${BINARY_DIR}" -lm
                -o "${character_abi_executable}"
        RESULT_VARIABLE character_abi_compile_status
        OUTPUT_VARIABLE character_abi_compile_output
        ERROR_VARIABLE character_abi_compile_error)
endif()
if(NOT character_abi_compile_status EQUAL 0)
    message(FATAL_ERROR
            "character ABI C17 did not compile: ${character_abi_compile_error}${character_abi_compile_output}")
endif()
execute_process(COMMAND "${character_abi_executable}"
                RESULT_VARIABLE character_abi_run_status)
if(NOT character_abi_run_status EQUAL 0)
    message(FATAL_ERROR "generated character padding, truncation, or length ABI is incorrect")
endif()

set(input_generated "${BINARY_DIR}/generated_fortran_input.c")
set(input_executable "${BINARY_DIR}/generated_fortran_input_test")
execute_process(
    COMMAND "${F2C}" -o "${input_generated}"
            "${SOURCE_DIR}/test/fixtures/fortran_numeric_input.f90"
    RESULT_VARIABLE input_translate_status
    OUTPUT_VARIABLE input_translate_output
    ERROR_VARIABLE input_translate_error)
if(NOT input_translate_status EQUAL 0)
    message(FATAL_ERROR
            "numeric-input translation failed: ${input_translate_error}${input_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${input_generated}" "/Fe${input_executable}.exe"
        RESULT_VARIABLE input_compile_status
        OUTPUT_VARIABLE input_compile_output
        ERROR_VARIABLE input_compile_error)
    set(input_executable "${input_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic "${input_generated}" -lm
                -o "${input_executable}"
        RESULT_VARIABLE input_compile_status
        OUTPUT_VARIABLE input_compile_output
        ERROR_VARIABLE input_compile_error)
endif()
if(NOT input_compile_status EQUAL 0)
    message(FATAL_ERROR
            "numeric-input C17 did not compile: ${input_compile_error}${input_compile_output}")
endif()
execute_process(
    COMMAND "${input_executable}"
    INPUT_FILE "${SOURCE_DIR}/test/fixtures/fortran_numeric_input.txt"
    RESULT_VARIABLE input_run_status)
if(NOT input_run_status EQUAL 0)
    message(FATAL_ERROR "generated reader rejected Fortran D-exponent or complex input")
endif()

set(implicit_generated "${BINARY_DIR}/generated_implicit_mapping.c")
set(implicit_header "${BINARY_DIR}/generated_implicit_mapping.h")
set(implicit_executable "${BINARY_DIR}/generated_implicit_mapping_test")
execute_process(
    COMMAND "${F2C}" -o "${implicit_generated}" --header "${implicit_header}"
            "${SOURCE_DIR}/test/fixtures/implicit_mapping.f90"
    RESULT_VARIABLE implicit_translate_status
    OUTPUT_VARIABLE implicit_translate_output
    ERROR_VARIABLE implicit_translate_error)
if(NOT implicit_translate_status EQUAL 0)
    message(FATAL_ERROR
            "IMPLICIT mapping translation failed: ${implicit_translate_error}${implicit_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${implicit_generated}"
                "${SOURCE_DIR}/test/fixtures/implicit_mapping_harness.c"
                "/I${BINARY_DIR}" "/Fe${implicit_executable}.exe"
        RESULT_VARIABLE implicit_compile_status
        OUTPUT_VARIABLE implicit_compile_output
        ERROR_VARIABLE implicit_compile_error)
    set(implicit_executable "${implicit_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wstrict-prototypes -Wmissing-prototypes -Werror "${implicit_generated}"
                "${SOURCE_DIR}/test/fixtures/implicit_mapping_harness.c"
                -I "${BINARY_DIR}" -lm -o "${implicit_executable}"
        RESULT_VARIABLE implicit_compile_status
        OUTPUT_VARIABLE implicit_compile_output
        ERROR_VARIABLE implicit_compile_error)
endif()
if(NOT implicit_compile_status EQUAL 0)
    message(FATAL_ERROR
            "IMPLICIT mapping C17 did not compile: ${implicit_compile_error}${implicit_compile_output}")
endif()
execute_process(COMMAND "${implicit_executable}"
                RESULT_VARIABLE implicit_run_status
                OUTPUT_VARIABLE implicit_generated_output)
if(NOT implicit_run_status EQUAL 0)
    message(FATAL_ERROR "generated IMPLICIT mapping or function-result ABI is incorrect")
endif()
if(F2C_GFORTRAN)
    foreach(print_oracle IN ITEMS print_formats assigned_format)
        set(print_native_executable "${BINARY_DIR}/native_${print_oracle}_test")
        set(print_source "${SOURCE_DIR}/test/fixtures/${print_oracle}.f90")
        set(print_standard f2018)
        set(print_warnings -Wall -Wextra -Werror)
        if(print_oracle STREQUAL "assigned_format")
            set(print_source "${SOURCE_DIR}/test/fixtures/${print_oracle}.f")
            set(print_standard legacy)
            list(APPEND print_warnings -Wno-unused-label)
        endif()
        execute_process(
            COMMAND "${F2C_GFORTRAN}" "-std=${print_standard}" ${print_warnings}
                    "${print_source}" -o "${print_native_executable}"
            RESULT_VARIABLE print_native_compile_status
            OUTPUT_VARIABLE print_native_compile_output
            ERROR_VARIABLE print_native_compile_error)
        if(NOT print_native_compile_status EQUAL 0)
            message(FATAL_ERROR
                    "native ${print_oracle} oracle did not compile: ${print_native_compile_error}${print_native_compile_output}")
        endif()
        execute_process(
            COMMAND "${print_native_executable}"
            RESULT_VARIABLE print_native_run_status
            OUTPUT_VARIABLE print_native_output)
        execute_process(
            COMMAND "${BINARY_DIR}/generated_${print_oracle}_test"
            RESULT_VARIABLE print_generated_run_status
            OUTPUT_VARIABLE print_generated_output)
        string(REPLACE "\r\n" "\n" print_native_output "${print_native_output}")
        string(REPLACE "\r\n" "\n" print_generated_output "${print_generated_output}")
        if(NOT print_native_run_status EQUAL 0 OR NOT print_generated_run_status EQUAL 0)
            message(FATAL_ERROR "${print_oracle} differential executable failed")
        endif()
        if(NOT print_generated_output STREQUAL print_native_output)
            message(FATAL_ERROR
                    "generated/native ${print_oracle} mismatch: generated='${print_generated_output}' native='${print_native_output}'")
        endif()
    endforeach()

    set(implicit_native_executable "${BINARY_DIR}/native_implicit_mapping_test")
    execute_process(
        COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                "${SOURCE_DIR}/test/fixtures/implicit_mapping.f90"
                "${SOURCE_DIR}/test/fixtures/implicit_mapping_driver.f90"
                -o "${implicit_native_executable}"
        RESULT_VARIABLE implicit_native_compile_status
        OUTPUT_VARIABLE implicit_native_compile_output
        ERROR_VARIABLE implicit_native_compile_error)
    if(NOT implicit_native_compile_status EQUAL 0)
        message(FATAL_ERROR
                "native IMPLICIT oracle did not compile: ${implicit_native_compile_error}${implicit_native_compile_output}")
    endif()
    execute_process(
        COMMAND "${implicit_native_executable}"
        RESULT_VARIABLE implicit_native_run_status
        OUTPUT_VARIABLE implicit_native_output)
    if(NOT implicit_native_run_status EQUAL 0)
        message(FATAL_ERROR "native IMPLICIT oracle failed")
    endif()
    if(NOT implicit_generated_output STREQUAL implicit_native_output)
        message(FATAL_ERROR
                "generated/native IMPLICIT differential mismatch: generated='${implicit_generated_output}' native='${implicit_native_output}'")
    endif()
endif()

set(optional_generated "${BINARY_DIR}/generated_optional_arguments.c")
set(optional_header "${BINARY_DIR}/generated_optional_arguments.h")
set(optional_executable "${BINARY_DIR}/generated_optional_arguments_test")
execute_process(
    COMMAND "${F2C}" -o "${optional_generated}" --header "${optional_header}"
            "${SOURCE_DIR}/test/fixtures/optional_arguments.f90"
    RESULT_VARIABLE optional_translate_status
    OUTPUT_VARIABLE optional_translate_output
    ERROR_VARIABLE optional_translate_error)
if(NOT optional_translate_status EQUAL 0)
    message(FATAL_ERROR
            "OPTIONAL translation failed: ${optional_translate_error}${optional_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${optional_generated}"
                "${SOURCE_DIR}/test/fixtures/optional_arguments_harness.c"
                "/I${BINARY_DIR}" "/Fe${optional_executable}.exe"
        RESULT_VARIABLE optional_compile_status
        OUTPUT_VARIABLE optional_compile_output
        ERROR_VARIABLE optional_compile_error)
    set(optional_executable "${optional_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wstrict-prototypes -Wmissing-prototypes -Werror "${optional_generated}"
                "${SOURCE_DIR}/test/fixtures/optional_arguments_harness.c"
                -I "${BINARY_DIR}" -lm -o "${optional_executable}"
        RESULT_VARIABLE optional_compile_status
        OUTPUT_VARIABLE optional_compile_output
        ERROR_VARIABLE optional_compile_error)
endif()
if(NOT optional_compile_status EQUAL 0)
    message(FATAL_ERROR
            "OPTIONAL generated C17 did not compile: ${optional_compile_error}${optional_compile_output}")
endif()
execute_process(
    COMMAND "${optional_executable}"
    RESULT_VARIABLE optional_run_status
    OUTPUT_VARIABLE optional_generated_output)
if(NOT optional_run_status EQUAL 0)
    message(FATAL_ERROR "generated OPTIONAL execution harness failed")
endif()
if(NOT optional_generated_output STREQUAL "1 3 26 68 95 93 301 21 10 15\n")
    message(FATAL_ERROR
            "generated OPTIONAL semantics are incorrect: '${optional_generated_output}'")
endif()
if(F2C_GFORTRAN)
    set(optional_native_executable "${BINARY_DIR}/native_optional_arguments_test")
    execute_process(
        COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                "${SOURCE_DIR}/test/fixtures/optional_arguments.f90"
                "${SOURCE_DIR}/test/fixtures/optional_arguments_driver.f90"
                -o "${optional_native_executable}"
        RESULT_VARIABLE optional_native_compile_status
        OUTPUT_VARIABLE optional_native_compile_output
        ERROR_VARIABLE optional_native_compile_error)
    if(NOT optional_native_compile_status EQUAL 0)
        message(FATAL_ERROR
                "native OPTIONAL oracle did not compile: ${optional_native_compile_error}${optional_native_compile_output}")
    endif()
    execute_process(
        COMMAND "${optional_native_executable}"
        RESULT_VARIABLE optional_native_run_status
        OUTPUT_VARIABLE optional_native_output)
    if(NOT optional_native_run_status EQUAL 0)
        message(FATAL_ERROR "native OPTIONAL oracle failed")
    endif()
    if(NOT optional_generated_output STREQUAL optional_native_output)
        message(FATAL_ERROR
                "generated/native OPTIONAL differential mismatch: generated='${optional_generated_output}' native='${optional_native_output}'")
    endif()
endif()

set(interface_generated "${BINARY_DIR}/generated_explicit_interface.c")
set(interface_header "${BINARY_DIR}/generated_explicit_interface.h")
set(interface_executable "${BINARY_DIR}/generated_explicit_interface_test")
execute_process(
    COMMAND "${F2C}" -o "${interface_generated}" --header "${interface_header}"
            "${SOURCE_DIR}/test/fixtures/explicit_interface.f90"
    RESULT_VARIABLE interface_translate_status
    OUTPUT_VARIABLE interface_translate_output
    ERROR_VARIABLE interface_translate_error)
if(NOT interface_translate_status EQUAL 0)
    message(FATAL_ERROR
            "explicit-INTERFACE translation failed: ${interface_translate_error}${interface_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${interface_generated}"
                "${SOURCE_DIR}/test/fixtures/explicit_interface_harness.c"
                "/I${BINARY_DIR}" "/Fe${interface_executable}.exe"
        RESULT_VARIABLE interface_compile_status
        OUTPUT_VARIABLE interface_compile_output
        ERROR_VARIABLE interface_compile_error)
    set(interface_executable "${interface_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wstrict-prototypes -Wmissing-prototypes -Werror "${interface_generated}"
                "${SOURCE_DIR}/test/fixtures/explicit_interface_harness.c"
                -I "${BINARY_DIR}" -lm -o "${interface_executable}"
        RESULT_VARIABLE interface_compile_status
        OUTPUT_VARIABLE interface_compile_output
        ERROR_VARIABLE interface_compile_error)
endif()
if(NOT interface_compile_status EQUAL 0)
    message(FATAL_ERROR
            "explicit-INTERFACE generated C17 did not compile: ${interface_compile_error}${interface_compile_output}")
endif()
execute_process(
    COMMAND "${interface_executable}"
    RESULT_VARIABLE interface_run_status
    OUTPUT_VARIABLE interface_generated_output)
if(NOT interface_run_status EQUAL 0)
    message(FATAL_ERROR "generated explicit-INTERFACE execution harness failed")
endif()
if(NOT interface_generated_output STREQUAL "12 13 14 15 89 42 112 213 1\n")
    message(FATAL_ERROR
            "generated explicit-INTERFACE semantics are incorrect: '${interface_generated_output}'")
endif()
if(F2C_GFORTRAN)
    set(interface_native_executable "${BINARY_DIR}/native_explicit_interface_test")
    execute_process(
        COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                "${SOURCE_DIR}/test/fixtures/explicit_interface.f90"
                "${SOURCE_DIR}/test/fixtures/explicit_interface_definition.f90"
                "${SOURCE_DIR}/test/fixtures/explicit_interface_driver.f90"
                -o "${interface_native_executable}"
        RESULT_VARIABLE interface_native_compile_status
        OUTPUT_VARIABLE interface_native_compile_output
        ERROR_VARIABLE interface_native_compile_error)
    if(NOT interface_native_compile_status EQUAL 0)
        message(FATAL_ERROR
                "native explicit-INTERFACE oracle did not compile: ${interface_native_compile_error}${interface_native_compile_output}")
    endif()
    execute_process(
        COMMAND "${interface_native_executable}"
        RESULT_VARIABLE interface_native_run_status
        OUTPUT_VARIABLE interface_native_output)
    if(NOT interface_native_run_status EQUAL 0)
        message(FATAL_ERROR "native explicit-INTERFACE oracle failed")
    endif()
    if(NOT interface_generated_output STREQUAL interface_native_output)
        message(FATAL_ERROR
                "generated/native explicit-INTERFACE differential mismatch: generated='${interface_generated_output}' native='${interface_native_output}'")
    endif()
endif()

set(procedure_generated "${BINARY_DIR}/generated_procedure_interface.c")
set(procedure_header "${BINARY_DIR}/generated_procedure_interface.h")
set(procedure_executable "${BINARY_DIR}/generated_procedure_interface_test")
execute_process(
    COMMAND "${F2C}" -o "${procedure_generated}" --header "${procedure_header}"
            "${SOURCE_DIR}/test/fixtures/procedure_interface.f90"
    RESULT_VARIABLE procedure_translate_status
    OUTPUT_VARIABLE procedure_translate_output
    ERROR_VARIABLE procedure_translate_error)
if(NOT procedure_translate_status EQUAL 0)
    message(FATAL_ERROR
            "ABSTRACT INTERFACE/PROCEDURE translation failed: ${procedure_translate_error}${procedure_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${procedure_generated}"
                "${SOURCE_DIR}/test/fixtures/procedure_interface_harness.c"
                "/I${BINARY_DIR}" "/Fe${procedure_executable}.exe"
        RESULT_VARIABLE procedure_compile_status
        OUTPUT_VARIABLE procedure_compile_output
        ERROR_VARIABLE procedure_compile_error)
    set(procedure_executable "${procedure_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wstrict-prototypes -Wmissing-prototypes -Werror "${procedure_generated}"
                "${SOURCE_DIR}/test/fixtures/procedure_interface_harness.c"
                -I "${BINARY_DIR}" -lm -o "${procedure_executable}"
        RESULT_VARIABLE procedure_compile_status
        OUTPUT_VARIABLE procedure_compile_output
        ERROR_VARIABLE procedure_compile_error)
endif()
if(NOT procedure_compile_status EQUAL 0)
    message(FATAL_ERROR
            "ABSTRACT INTERFACE/PROCEDURE generated C17 did not compile: ${procedure_compile_error}${procedure_compile_output}")
endif()
execute_process(
    COMMAND "${procedure_executable}"
    RESULT_VARIABLE procedure_run_status
    OUTPUT_VARIABLE procedure_generated_output)
if(NOT procedure_run_status EQUAL 0)
    message(FATAL_ERROR "generated ABSTRACT INTERFACE/PROCEDURE execution harness failed")
endif()
if(NOT procedure_generated_output STREQUAL "12 9 -1 12 3 6 9 1 1 5\n")
    message(FATAL_ERROR
            "generated ABSTRACT INTERFACE/PROCEDURE semantics are incorrect: '${procedure_generated_output}'")
endif()
if(F2C_GFORTRAN)
    set(procedure_native_executable "${BINARY_DIR}/native_procedure_interface_test")
    execute_process(
        COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                "${SOURCE_DIR}/test/fixtures/procedure_interface.f90"
                "${SOURCE_DIR}/test/fixtures/procedure_interface_driver.f90"
                -o "${procedure_native_executable}"
        RESULT_VARIABLE procedure_native_compile_status
        OUTPUT_VARIABLE procedure_native_compile_output
        ERROR_VARIABLE procedure_native_compile_error)
    if(NOT procedure_native_compile_status EQUAL 0)
        message(FATAL_ERROR
                "native ABSTRACT INTERFACE/PROCEDURE oracle did not compile: ${procedure_native_compile_error}${procedure_native_compile_output}")
    endif()
    execute_process(
        COMMAND "${procedure_native_executable}"
        RESULT_VARIABLE procedure_native_run_status
        OUTPUT_VARIABLE procedure_native_output)
    if(NOT procedure_native_run_status EQUAL 0)
        message(FATAL_ERROR "native ABSTRACT INTERFACE/PROCEDURE oracle failed")
    endif()
    if(NOT procedure_generated_output STREQUAL procedure_native_output)
        message(FATAL_ERROR
                "generated/native ABSTRACT INTERFACE/PROCEDURE differential mismatch: generated='${procedure_generated_output}' native='${procedure_native_output}'")
    endif()
endif()

set(deferred_generated "${BINARY_DIR}/generated_deferred_character.c")
set(deferred_header "${BINARY_DIR}/generated_deferred_character.h")
set(deferred_executable "${BINARY_DIR}/generated_deferred_character_test")
execute_process(
    COMMAND "${F2C}" -o "${deferred_generated}" --header "${deferred_header}"
            "${SOURCE_DIR}/test/fixtures/deferred_character.f90"
    RESULT_VARIABLE deferred_translate_status
    OUTPUT_VARIABLE deferred_translate_output
    ERROR_VARIABLE deferred_translate_error)
if(NOT deferred_translate_status EQUAL 0)
    message(FATAL_ERROR
            "deferred CHARACTER translation failed: ${deferred_translate_error}${deferred_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${deferred_generated}"
                "${SOURCE_DIR}/test/fixtures/deferred_character_harness.c"
                "/I${BINARY_DIR}" "/Fe${deferred_executable}.exe"
        RESULT_VARIABLE deferred_compile_status
        OUTPUT_VARIABLE deferred_compile_output
        ERROR_VARIABLE deferred_compile_error)
    set(deferred_executable "${deferred_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wstrict-prototypes -Wmissing-prototypes -Werror "${deferred_generated}"
                "${SOURCE_DIR}/test/fixtures/deferred_character_harness.c"
                -I "${BINARY_DIR}" -lm -o "${deferred_executable}"
        RESULT_VARIABLE deferred_compile_status
        OUTPUT_VARIABLE deferred_compile_output
        ERROR_VARIABLE deferred_compile_error)
endif()
if(NOT deferred_compile_status EQUAL 0)
    message(FATAL_ERROR
            "deferred CHARACTER generated C17 did not compile: ${deferred_compile_error}${deferred_compile_output}")
endif()
execute_process(
    COMMAND "${deferred_executable}"
    RESULT_VARIABLE deferred_run_status
    OUTPUT_VARIABLE deferred_generated_output)
if(NOT deferred_run_status EQUAL 0)
    message(FATAL_ERROR "generated deferred CHARACTER execution harness failed")
endif()
if(NOT deferred_generated_output STREQUAL "5 65 69 0 3 1 1 1 5 0 1 0 0 1 11 55 7 9 11 55 6 8 2 1 1 1 1 3 2 1 1 1 1 1 1 1 2 9 19 109 36 2 1 3 1 2 1 1 3 0 1 36 36 0 2 1\n")
    message(FATAL_ERROR
            "generated deferred CHARACTER semantics are incorrect: '${deferred_generated_output}'")
endif()
if(F2C_GFORTRAN)
    set(move_alloc_probe "${BINARY_DIR}/move_alloc_stat_probe.f90")
    set(move_alloc_probe_object "${BINARY_DIR}/move_alloc_stat_probe.o")
    file(WRITE "${move_alloc_probe}"
         "program move_alloc_stat_probe\n"
         "  integer, allocatable :: from(:), to(:)\n"
         "  integer :: status\n"
         "  character(len=32) :: message\n"
         "  allocate(from(1))\n"
         "  call move_alloc(from, to, stat=status, errmsg=message)\n"
         "end program move_alloc_stat_probe\n")
    execute_process(
        COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                -c "${move_alloc_probe}" -o "${move_alloc_probe_object}"
        RESULT_VARIABLE move_alloc_probe_status
        OUTPUT_VARIABLE move_alloc_probe_output
        ERROR_VARIABLE move_alloc_probe_error)
    if(move_alloc_probe_status EQUAL 0)
        set(deferred_native_executable "${BINARY_DIR}/native_deferred_character_test")
        execute_process(
            COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                "${SOURCE_DIR}/test/fixtures/deferred_character.f90"
                "${SOURCE_DIR}/test/fixtures/deferred_character_driver.f90"
                -o "${deferred_native_executable}"
            RESULT_VARIABLE deferred_native_compile_status
            OUTPUT_VARIABLE deferred_native_compile_output
            ERROR_VARIABLE deferred_native_compile_error)
        if(NOT deferred_native_compile_status EQUAL 0)
            message(FATAL_ERROR
                    "native deferred CHARACTER oracle did not compile: ${deferred_native_compile_error}${deferred_native_compile_output}")
        endif()
        execute_process(
            COMMAND "${deferred_native_executable}"
            RESULT_VARIABLE deferred_native_run_status
            OUTPUT_VARIABLE deferred_native_output)
        if(NOT deferred_native_run_status EQUAL 0)
            message(FATAL_ERROR "native deferred CHARACTER oracle failed")
        endif()
        if(NOT deferred_generated_output STREQUAL deferred_native_output)
            message(FATAL_ERROR
                    "generated/native deferred CHARACTER differential mismatch: generated='${deferred_generated_output}' native='${deferred_native_output}'")
        endif()
    else()
        message(STATUS
                "Skipping native deferred CHARACTER oracle: ${F2C_GFORTRAN} lacks MOVE_ALLOC STAT=/ERRMSG= support")
    endif()
endif()

set(constructor_generated "${BINARY_DIR}/generated_array_constructor.c")
set(constructor_header "${BINARY_DIR}/generated_array_constructor.h")
set(constructor_executable "${BINARY_DIR}/generated_array_constructor_test")
execute_process(
    COMMAND "${F2C}" -o "${constructor_generated}" --header "${constructor_header}"
            "${SOURCE_DIR}/test/fixtures/array_constructor.f90"
    RESULT_VARIABLE constructor_translate_status
    OUTPUT_VARIABLE constructor_translate_output
    ERROR_VARIABLE constructor_translate_error)
if(NOT constructor_translate_status EQUAL 0)
    message(FATAL_ERROR
            "array-constructor translation failed: ${constructor_translate_error}${constructor_translate_output}")
endif()
if(F2C_MSVC_FRONTEND)
    execute_process(
        COMMAND ${CC_COMMAND} /std:c17 /W4 "${constructor_generated}"
                "${SOURCE_DIR}/test/fixtures/array_constructor_harness.c"
                "/I${BINARY_DIR}" "/Fe${constructor_executable}.exe"
        RESULT_VARIABLE constructor_compile_status
        OUTPUT_VARIABLE constructor_compile_output
        ERROR_VARIABLE constructor_compile_error)
    set(constructor_executable "${constructor_executable}.exe")
else()
    execute_process(
        COMMAND ${CC_COMMAND} -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wstrict-prototypes -Wmissing-prototypes -Werror "${constructor_generated}"
                "${SOURCE_DIR}/test/fixtures/array_constructor_harness.c"
                -I "${BINARY_DIR}" -lm -o "${constructor_executable}"
        RESULT_VARIABLE constructor_compile_status
        OUTPUT_VARIABLE constructor_compile_output
        ERROR_VARIABLE constructor_compile_error)
endif()
if(NOT constructor_compile_status EQUAL 0)
    message(FATAL_ERROR
            "array-constructor C17 did not compile: ${constructor_compile_error}${constructor_compile_output}")
endif()
execute_process(
    COMMAND "${constructor_executable}"
    RESULT_VARIABLE constructor_run_status
    OUTPUT_VARIABLE constructor_generated_output)
if(NOT constructor_run_status EQUAL 0)
    message(FATAL_ERROR
            "generated nested or negative-step array-constructor implied DO is incorrect")
endif()
if(F2C_GFORTRAN)
    set(constructor_native_executable "${BINARY_DIR}/native_array_constructor_test")
    execute_process(
        COMMAND "${F2C_GFORTRAN}" -std=f2018 -Wall -Wextra -Werror
                "${SOURCE_DIR}/test/fixtures/array_constructor.f90"
                "${SOURCE_DIR}/test/fixtures/array_constructor_driver.f90"
                -o "${constructor_native_executable}"
        RESULT_VARIABLE constructor_native_compile_status
        OUTPUT_VARIABLE constructor_native_compile_output
        ERROR_VARIABLE constructor_native_compile_error)
    if(NOT constructor_native_compile_status EQUAL 0)
        message(FATAL_ERROR
                "native array-constructor oracle did not compile: ${constructor_native_compile_error}${constructor_native_compile_output}")
    endif()
    execute_process(
        COMMAND "${constructor_native_executable}"
        RESULT_VARIABLE constructor_native_run_status
        OUTPUT_VARIABLE constructor_native_output)
    if(NOT constructor_native_run_status EQUAL 0)
        message(FATAL_ERROR "native array-constructor oracle failed")
    endif()
    if(NOT constructor_generated_output STREQUAL constructor_native_output)
        message(FATAL_ERROR
                "generated/native array-constructor differential mismatch: generated='${constructor_generated_output}' native='${constructor_native_output}'")
    endif()
endif()
