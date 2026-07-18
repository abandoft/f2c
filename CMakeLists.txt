cmake_minimum_required(VERSION 3.20)

file(
    STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/f2c/version.h"
    F2C_VERSION_DEFINITION
    REGEX "^#define F2C_VERSION_STRING \"[0-9]+\\.[0-9]+\\.[0-9]+\"$"
    LIMIT_COUNT 1
)
string(
    REGEX REPLACE
    "^#define F2C_VERSION_STRING \"([^\"]+)\"$"
    "\\1"
    F2C_PROJECT_VERSION
    "${F2C_VERSION_DEFINITION}"
)
if(NOT F2C_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "include/f2c/version.h does not define a valid F2C_VERSION_STRING")
endif()

project(
    f2c
    VERSION "${F2C_PROJECT_VERSION}"
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
    src/ast/budget.c
    src/ast/expression.c
    src/ast/internal.h
    src/ast/parser.c
    src/ast/shape.c
    src/ast/statement.c
    src/ast/statement/case.c
    src/ast/statement/construct.c
    src/ast/statement/data.c
    src/ast/statement/io_control.c
    src/ast/statement/io_item.c
    src/ast/statement/label.c
    src/ast/statement/parenthesis.c
    src/ast/statement/private.h
    src/ast/token_stream.c
    src/ast/visit.c
    src/codegen/allocation.c
    src/codegen/array.c
    src/codegen/array/assignment.c
    src/codegen/array/call.c
    src/codegen/array/constructor.c
    src/codegen/array/element.c
    src/codegen/array/private.h
    src/codegen/array/temporary.c
    src/codegen/call.c
    src/codegen/codegen.h
    src/codegen/data.c
    src/codegen/descriptor/materialize.c
    src/codegen/descriptor/private.h
    src/codegen/descriptor/view.c
    src/codegen/expression.c
    src/codegen/expression/call.c
    src/codegen/expression/inquiry.c
    src/codegen/expression/private.h
    src/codegen/expression/reduction.c
    src/codegen/expression/statement_function.c
    src/codegen/io.c
    src/codegen/io/control/common.c
    src/codegen/io/control/inquire.c
    src/codegen/io/control/open.c
    src/codegen/io/control/position.c
    src/codegen/io/control/print.c
    src/codegen/io/control/private.h
    src/codegen/io/format.c
    src/codegen/io/formatted.c
    src/codegen/io/namelist.c
    src/codegen/io/private.h
    src/codegen/io/support.c
    src/codegen/lowering.c
    src/codegen/module.c
    src/codegen/ownership.c
    src/codegen/statement.c
    src/codegen/statement/select.c
    src/codegen/statement/private.h
    src/codegen/statement/target.c
    src/codegen/statement/type_select.c
    src/codegen/statement/where.c
    src/codegen/transform.c
    src/codegen/transform/findloc.c
    src/codegen/transform/inquiry.c
    src/codegen/transform/matrix.c
    src/codegen/transform/private.h
    src/codegen/transform/view.c
    src/codegen/type/derived.c
    src/codegen/unit.c
    src/codegen/unit/data.c
    src/codegen/unit/interface.c
    src/codegen/unit/private.h
    src/core/config.c
    src/core/cleanup.c
    src/core/generated/io.c
    src/core/generated/private.h
    src/core/generated/relation.c
    src/core/support.c
    src/core/transpile.c
    src/frontend/analyze.c
    src/frontend/attributes.c
    src/frontend/declaration/common.c
    src/frontend/declaration/dimension.c
    src/frontend/declaration/entity.c
    src/frontend/declaration/namelist.c
    src/frontend/declaration/optional.c
    src/frontend/declaration/private.h
    src/frontend/declaration/syntax.c
    src/frontend/declaration/type.c
    src/frontend/derived.c
    src/frontend/frontend.h
    src/frontend/interface.c
    src/frontend/lexer.c
    src/frontend/module_constants.h
    src/frontend/modules.c
    src/frontend/parser.c
    src/frontend/pipeline.c
    src/frontend/pipeline.h
    src/frontend/private.h
    src/frontend/procedure.c
    src/frontend/preprocessor.c
    src/frontend/preprocessor.h
    src/frontend/preprocessor/expansion.c
    src/frontend/preprocessor/expression.c
    src/frontend/preprocessor/include.c
    src/frontend/preprocessor/private.h
    src/frontend/references.c
    src/frontend/source.c
    src/frontend/source_map.c
    src/frontend/statement.c
    src/frontend/syntax.c
    src/frontend/token.h
    src/frontend/token_cursor.c
    src/frontend/unit_header.c
    src/internal/base.h
    src/internal/context.h
    src/internal/f2c.h
    src/ir/expression.h
    src/ir/statement.h
    src/ir/type.h
    src/semantic/character.c
    src/semantic/constant.c
    src/semantic/implicit.c
    src/semantic/implicit/declaration.c
    src/semantic/implicit/private.h
    src/semantic/implicit/symbols.c
    src/semantic/intrinsic.c
    src/semantic/intrinsic.h
    src/semantic/model.h
    src/semantic/procedure.c
    src/semantic/semantic.h
    src/semantic/signature.c
    src/semantic/type.c
    src/semantic/validation.c
    src/semantic/validation/allocation.c
    src/semantic/validation/case.c
    src/semantic/validation/construct.c
    src/semantic/validation/data.c
    src/semantic/validation/expression.c
    src/semantic/validation/private.h
    src/semantic/validation/procedure.c
    src/semantic/validation/statement.c
    src/semantic/validation/where.c
)

function(f2c_configure_core_target target)
    target_compile_features(${target} PUBLIC c_std_17)
    target_compile_definitions(
        ${target}
        PRIVATE
            F2C_BUILDING_LIBRARY
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

set(F2C_CLI_SOURCES src/cli/include.c src/cli/include.h src/cli/io.c src/cli/io.h src/cli/main.c)
add_executable(f2c ${F2C_CLI_SOURCES})
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

    add_test(
        NAME architecture_boundaries
        COMMAND
            ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check_architecture.cmake
    )

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
    add_test(
        NAME cli_io
        COMMAND
            ${CMAKE_COMMAND}
            -DF2C=$<TARGET_FILE:f2c>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/cli_io.cmake
    )

    add_executable(f2c_ast_tests test/test_ast.c)
    target_include_directories(f2c_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_ast_tests)
    add_test(NAME ast COMMAND f2c_ast_tests)

    add_executable(f2c_lexer_tests test/test_lexer.c)
    target_include_directories(f2c_lexer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_lexer_tests)
    add_test(NAME lexer COMMAND f2c_lexer_tests)

    add_executable(f2c_preprocessor_tests test/test_preprocessor.c)
    f2c_configure_test_target(f2c_preprocessor_tests)
    add_test(NAME preprocessor COMMAND f2c_preprocessor_tests)

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

    add_executable(f2c_storage_tests test/test_storage.c)
    f2c_configure_test_target(f2c_storage_tests)
    add_test(NAME storage_semantics COMMAND f2c_storage_tests)

    add_executable(f2c_data_tests test/test_data.c)
    f2c_configure_test_target(f2c_data_tests)
    add_test(NAME data_semantics COMMAND f2c_data_tests)

    add_executable(f2c_io_tests test/test_io.c)
    f2c_configure_test_target(f2c_io_tests)
    add_test(NAME io_semantics COMMAND f2c_io_tests)

    add_executable(f2c_declaration_tests test/test_declaration.c)
    f2c_configure_test_target(f2c_declaration_tests)
    add_test(NAME declaration_semantics COMMAND f2c_declaration_tests)

    add_executable(f2c_elemental_tests test/test_elemental.c)
    f2c_configure_test_target(f2c_elemental_tests)
    add_test(NAME elemental_semantics COMMAND f2c_elemental_tests)

    add_executable(f2c_inquiry_tests test/test_inquiry.c)
    f2c_configure_test_target(f2c_inquiry_tests)
    add_test(NAME inquiry_semantics COMMAND f2c_inquiry_tests)

    add_executable(f2c_implicit_tests test/test_implicit.c)
    f2c_configure_test_target(f2c_implicit_tests)
    add_test(NAME implicit_semantics COMMAND f2c_implicit_tests)

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

    if(UNIX)
        find_program(F2C_SH_EXECUTABLE sh)
        if(F2C_SH_EXECUTABLE)
            add_test(
                NAME performance_scope_validation
                COMMAND
                    ${CMAKE_COMMAND}
                    -DSH=${F2C_SH_EXECUTABLE}
                    -DF2C=$<TARGET_FILE:f2c>
                    -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/reference_performance_matrix.sh
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/performance_scope_validation.cmake
            )
        endif()
    endif()

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
            NAME numerical_coverage_self_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/numerical_coverage.py --self-test
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
