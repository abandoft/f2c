cmake_minimum_required(VERSION 3.20)

project(
    f2c
    VERSION 1.0.0
    DESCRIPTION "A modern, high-performance F2C"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

option(F2C_BUILD_TESTING "Build the f2c test suite" ${BUILD_TESTING})
option(F2C_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(
    F2C_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)
option(F2C_BUILD_LIBFUZZER "Build the coverage-guided libFuzzer target" OFF)

set(
    F2C_CORE_SOURCES
    src/core/support.c
    src/core/transpile.c
    src/frontend/interface.c
    src/frontend/lexer.c
    src/frontend/modules.c
    src/frontend/parser.c
    src/frontend/procedure.c
    src/frontend/source.c
    src/semantic/character.c
    src/semantic/constant.c
    src/semantic/implicit.c
    src/semantic/intrinsic.c
    src/semantic/procedure.c
    src/semantic/type.c
    src/semantic/validation.c
    src/ast/expression.c
    src/ast/internal.h
    src/ast/lexer.c
    src/ast/parser.c
    src/ast/statement.c
    src/ast/visit.c
    src/codegen/allocation.c
    src/codegen/array.c
    src/codegen/call.c
    src/codegen/data.c
    src/codegen/expression.c
    src/codegen/io.c
    src/codegen/io/support.c
    src/codegen/io/format.c
    src/codegen/lowering.c
    src/codegen/ownership.c
    src/codegen/statement.c
    src/codegen/transform.c
    src/codegen/type/derived.c
    src/codegen/unit.c
    src/internal/f2c.h
)

function(f2c_configure_core_target target)
    target_compile_features(${target} PUBLIC c_std_17)
    target_compile_definitions(
        ${target}
        PRIVATE
            F2C_BUILDING_LIBRARY
            F2C_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(
        ${target}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    set_target_properties(
        ${target}
        PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO
            POSITION_INDEPENDENT_CODE YES
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(F2C_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
                -Wstrict-prototypes
                -Wmissing-prototypes
        )
        if(F2C_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(F2C_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(
                ${target}
                PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            # PUBLIC is required because a static library has no link step of its own.
            target_link_options(${target} PUBLIC -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

add_library(f2c_core ${F2C_CORE_SOURCES})
add_library(f2c::core ALIAS f2c_core)
f2c_configure_core_target(f2c_core)
set_target_properties(
    f2c_core
    PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        C_VISIBILITY_PRESET hidden
)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(f2c_core PUBLIC F2C_SHARED)
endif()

add_executable(f2c src/cli/main.c)
target_compile_features(f2c PRIVATE c_std_17)
target_link_libraries(f2c PRIVATE f2c::core)
if(MSVC)
    target_compile_options(f2c PRIVATE /W4 /permissive- /utf-8)
    if(F2C_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(f2c PRIVATE /WX)
    endif()
else()
    target_compile_options(
        f2c
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
    )
    if(F2C_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(f2c PRIVATE -Werror)
    endif()
endif()

if(F2C_BUILD_TESTING)
    if(BUILD_SHARED_LIBS)
        # White-box tests use private compiler APIs without making them part of
        # the shared-library ABI. Public API tests still link against f2c_core.
        add_library(f2c_internal_test_core STATIC ${F2C_CORE_SOURCES})
        f2c_configure_core_target(f2c_internal_test_core)
    endif()

    function(f2c_configure_test_compilation target)
        target_compile_features(${target} PRIVATE c_std_17)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /utf-8)
            if(F2C_ENABLE_WARNINGS_AS_ERRORS)
                target_compile_options(${target} PRIVATE /WX)
            endif()
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
            if(F2C_ENABLE_WARNINGS_AS_ERRORS)
                target_compile_options(${target} PRIVATE -Werror)
            endif()
        endif()
    endfunction()

    function(f2c_configure_test_target target)
        f2c_configure_test_compilation(${target})
        target_link_libraries(${target} PRIVATE f2c::core)
    endfunction()

    function(f2c_configure_internal_test_target target)
        f2c_configure_test_compilation(${target})
        if(BUILD_SHARED_LIBS)
            target_link_libraries(${target} PRIVATE f2c_internal_test_core)
        else()
            target_link_libraries(${target} PRIVATE f2c::core)
        endif()
    endfunction()

    add_executable(f2c_tests test/test_transpile.c)
    f2c_configure_test_target(f2c_tests)
    add_test(NAME unit COMMAND f2c_tests)

    add_test(NAME cli_help COMMAND f2c --help)
    set_tests_properties(
        cli_help
        PROPERTIES PASS_REGULAR_EXPRESSION "Source form is automatic by default"
    )
    add_test(
        NAME cli_conflicting_source_form
        COMMAND f2c --free-form --fixed-form test/fixtures/daxpy.f90
    )
    set_tests_properties(cli_conflicting_source_form PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME cli_auto_fixed_form
        COMMAND
            f2c
            ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/assigned_goto.f
            -o
            ${CMAKE_CURRENT_BINARY_DIR}/cli-auto-fixed.c
    )

    add_executable(f2c_ast_tests test/test_ast.c)
    target_include_directories(f2c_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_ast_tests)
    add_test(NAME ast COMMAND f2c_ast_tests)

    add_executable(f2c_lexer_tests test/test_lexer.c)
    target_include_directories(f2c_lexer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_lexer_tests)
    add_test(NAME lexer COMMAND f2c_lexer_tests)

    add_executable(f2c_statement_ast_tests test/test_statement_ast.c)
    target_include_directories(
        f2c_statement_ast_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    f2c_configure_internal_test_target(f2c_statement_ast_tests)
    add_test(NAME statement_ast COMMAND f2c_statement_ast_tests)

    add_executable(f2c_constant_tests test/test_constant.c)
    target_include_directories(f2c_constant_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_constant_tests)
    add_test(NAME constant COMMAND f2c_constant_tests)

    add_executable(f2c_fuzz_tests test/test_fuzz.c)
    f2c_configure_test_target(f2c_fuzz_tests)
    add_test(NAME mutation_fuzz COMMAND f2c_fuzz_tests)

    add_executable(f2c_benchmark_statistics_tests test/test_benchmark_statistics.c)
    target_include_directories(
        f2c_benchmark_statistics_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test
    )
    f2c_configure_test_compilation(f2c_benchmark_statistics_tests)
    add_test(NAME benchmark_statistics COMMAND f2c_benchmark_statistics_tests)

    add_test(
        NAME generated_c17
        COMMAND
            ${CMAKE_COMMAND}
            -DF2C=$<TARGET_FILE:f2c>
            -DCC=${CMAKE_C_COMPILER}
            -DCC_ID=${CMAKE_C_COMPILER_ID}
            -DCC_FRONTEND_VARIANT=${CMAKE_C_COMPILER_FRONTEND_VARIANT}
            "-DVS_INSTALLATION=${CMAKE_GENERATOR_INSTANCE}"
            -DVS_TARGET_ARCHITECTURE=${CMAKE_VS_PLATFORM_NAME}
            -DVS_HOST_ARCHITECTURE=${CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/compile_generated.cmake
    )
    add_test(
        NAME deterministic_output
        COMMAND
            ${CMAKE_COMMAND}
            -DF2C=$<TARGET_FILE:f2c>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/reproducible_output.cmake
    )

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME blas_result_diff_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/blas_result_diff.py self-test
        )
        add_test(
            NAME lapack_result_diff_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/lapack_result_diff.py self-test
        )
        add_test(
            NAME differential_manifest_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/differential_manifest.py self-test
        )
        add_test(
            NAME exhaustive_result_diff_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/exhaustive_result_diff.py self-test
        )
        add_test(
            NAME official_numerical_audit_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/official_numerical_audit.py --self-test
        )
        add_test(
            NAME lapack_trace_instrument_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/lapack_trace_instrument.py self-test
        )
        add_test(
            NAME blas_trace_instrument_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/blas_trace_instrument.py self-test
        )
    endif()
endif()

if(F2C_BUILD_LIBFUZZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "F2C_BUILD_LIBFUZZER requires Clang or AppleClang")
    endif()
    include(CheckCSourceCompiles)
    set(F2C_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=fuzzer")
    check_c_source_compiles(
        "#include <stddef.h>\n#include <stdint.h>\nint LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { (void)data; (void)size; return 0; }"
        F2C_COMPILER_HAS_LIBFUZZER
    )
    set(CMAKE_REQUIRED_FLAGS "${F2C_SAVED_REQUIRED_FLAGS}")
    if(NOT F2C_COMPILER_HAS_LIBFUZZER)
        message(
            FATAL_ERROR
            "F2C_BUILD_LIBFUZZER requires a compiler distribution with the libFuzzer runtime"
        )
    endif()
    add_executable(f2c_libfuzzer test/fuzz_transpile.c)
    target_compile_options(f2c_core PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_features(f2c_libfuzzer PRIVATE c_std_17)
    target_link_libraries(f2c_libfuzzer PRIVATE f2c::core)
    target_compile_options(
        f2c_libfuzzer
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer
    )
    target_link_options(f2c_libfuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
