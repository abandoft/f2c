cmake_minimum_required(VERSION 3.20)

set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/f2c/version.h"
)

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
    src/ast/declaration/access.c
    src/ast/declaration/access.h
    src/ast/declaration/designator.c
    src/ast/declaration/designator.h
    src/ast/declaration/procedure.c
    src/ast/declaration/bindings.c
    src/ast/declaration/bindings.h
    src/ast/declaration/bindings_private.h
    src/ast/declaration/storage_bindings.c
    src/ast/declaration/procedure.h
    src/ast/declaration/use.c
    src/ast/declaration/use.h
    src/ast/designator.c
    src/ast/expression.c
    src/ast/format.c
    src/ast/format.h
    src/ast/intrinsic.c
    src/ast/interface/header.c
    src/ast/interface/header.h
    src/ast/interface/specific.c
    src/ast/interface/specific.h
    src/ast/internal.h
    src/ast/parser.c
    src/ast/shape.c
    src/ast/statement.c
    src/ast/statement/action.c
    src/ast/statement/assignment.c
    src/ast/statement/case.c
    src/ast/statement/construct.c
    src/ast/statement/control.c
    src/ast/statement/data.c
    src/ast/statement/io_control.c
    src/ast/statement/io_item.c
    src/ast/statement/label.c
    src/ast/statement/parenthesis.c
    src/ast/statement/private.h
    src/ast/statement/where.c
    src/ast/token_stream.c
    src/ast/unit.c
    src/ast/unit.h
    src/ast/visit.c
    src/codegen/allocation/allocate.c
    src/codegen/allocation/context.c
    src/codegen/allocation/context.h
    src/codegen/allocation/controls.c
    src/codegen/allocation/deallocate.c
    src/codegen/allocation/model.c
    src/codegen/allocation/object.c
    src/codegen/allocation/private.h
    src/codegen/array.c
    src/codegen/array/assignment.c
    src/codegen/array/static_shape.c
    src/codegen/array/static_shape.h
    src/codegen/array/call.c
    src/codegen/array/character.c
    src/codegen/array/designator.c
    src/codegen/array/constructor.c
    src/codegen/array/copy.c
    src/codegen/array/copy.h
    src/codegen/array/element.c
    src/codegen/result/materialize.c
    src/codegen/result/retention.c
    src/codegen/result/retention.h
    src/codegen/array/inquiry.c
    src/codegen/array/ownership.c
    src/codegen/array/private.h
    src/codegen/array/temporary.c
    src/codegen/array/value.c
    src/codegen/array/value.h
    src/codegen/array/view.c
    src/codegen/array/view.h
    src/codegen/call.c
    src/codegen/call/actual.c
    src/codegen/call/expression.c
    src/codegen/call/private.h
    src/codegen/call/specification.c
    src/codegen/character.c
    src/codegen/character/deferred.c
    src/codegen/character/private.h
    src/codegen/character/length.c
    src/codegen/codegen.h
    src/codegen/data.c
    src/codegen/descriptor/association.c
    src/codegen/descriptor/access.c
    src/codegen/descriptor/designator.c
    src/codegen/descriptor/materialize.c
    src/codegen/descriptor/private.h
    src/codegen/descriptor/view.c
    src/codegen/expression.c
    src/codegen/expression/designator.c
    src/codegen/expression/access.c
    src/codegen/host/capture.c
    src/codegen/host/descriptor.c
    src/codegen/host/private.h
    src/codegen/expression/associated.c
    src/codegen/expression/bit.c
    src/codegen/expression/call.c
    src/codegen/expression/time.c
    src/codegen/expression/character.c
    src/codegen/expression/value.c
    src/codegen/expression/conversion.c
    src/codegen/expression/descriptor.c
    src/codegen/expression/derived_actual.c
    src/codegen/expression/inquiry.c
    src/codegen/expression/leaf.c
    src/codegen/expression/mathematical.c
    src/codegen/expression/model.c
    src/codegen/expression/numeric.c
    src/codegen/expression/private.h
    src/codegen/expression/representation.c
    src/codegen/expression/reduction.c
    src/codegen/expression/statement_function.c
    src/codegen/expression/transfer.c
    src/codegen/io.c
    src/codegen/io/access.c
    src/codegen/io/control/common.c
    src/codegen/io/control/inquire.c
    src/codegen/io/control/iolength.c
    src/codegen/io/control/open.c
    src/codegen/io/control/position.c
    src/codegen/io/control/print.c
    src/codegen/io/control/transfer.c
    src/codegen/io/control/private.h
    src/codegen/io/format.c
    src/codegen/io/format/parser.c
    src/codegen/io/format/program.c
    src/codegen/io/format/real.c
    src/codegen/io/format/integer.c
    src/codegen/io/format/round.c
    src/codegen/io/format/state.c
    src/codegen/io/format_control.c
    src/codegen/io/formatted.c
    src/codegen/io/loop.c
    src/codegen/io/namelist.c
    src/codegen/io/namelist/parser.c
    src/codegen/io/namelist/support.c
    src/codegen/io/namelist/transaction.c
    src/codegen/io/namelist/state.c
    src/codegen/io/namelist/state.h
    src/codegen/io/private.h
    src/codegen/io/program.c
    src/codegen/io/support.c
    src/codegen/io/transfer.c
    src/codegen/io/unformatted.c
    src/codegen/io/unformatted/array.c
    src/codegen/io/unformatted/private.h
    src/codegen/lowering/expression.c
    src/codegen/lowering/private.h
    src/codegen/literal/integer.c
    src/codegen/literal/integer.h
    src/codegen/literal/real.c
    src/codegen/literal/real.h
    src/codegen/lowering.c
    src/codegen/loop/control.c
    src/codegen/loop/control.h
    src/codegen/operator.c
    src/codegen/operator.h
    src/codegen/module.c
    src/codegen/names.c
    src/codegen/names.h
    src/codegen/ownership.c
    src/codegen/statement.c
    src/codegen/storage/state.c
    src/codegen/storage/shape.c
    src/codegen/storage/lifetime.c
    src/codegen/storage/transfer.c
    src/codegen/storage/element.c
    src/codegen/storage/private.h
    src/codegen/statement/assignment.c
    src/codegen/statement/bit.c
    src/codegen/statement/loop.c
    src/codegen/statement/pointer.c
    src/codegen/statement/select.c
    src/codegen/statement/private.h
    src/codegen/statement/random.c
    src/codegen/statement/temporary.c
    src/codegen/statement/time.c
    src/codegen/statement/target.c
    src/codegen/statement/type_select.c
    src/codegen/statement/where.c
    src/codegen/transform.c
    src/codegen/transform/findloc.c
    src/codegen/transform/inquiry.c
    src/codegen/transform/matrix.c
    src/codegen/transform/reduction.c
    src/codegen/transform/result.c
    src/codegen/transform/transfer.c
    src/codegen/transform/private.h
    src/codegen/transform/view.c
    src/codegen/type/derived.c
    src/codegen/type/snapshot.c
    src/codegen/call/address.c
    src/codegen/call/binding.c
    src/codegen/type/initialization.c
    src/codegen/type/initialization.h
    src/codegen/type/namelist.c
    src/codegen/unit.c
    src/codegen/unit/automatic.c
    src/codegen/unit/data.c
    src/codegen/unit/constant.c
    src/codegen/unit/declaration.c
    src/codegen/unit/equivalence.c
    src/codegen/unit/interface.c
    src/codegen/unit/private.h
    src/codegen/unit/scope.c
    src/codegen/unit/temporary.c
    src/codegen/unit/value.c
    src/codegen/value/derived.c
    src/codegen/value/private.h
    src/core/config.c
    src/core/cleanup.c
    src/core/generated/bit.c
    src/core/generated/character.c
    src/core/generated/conversion.c
    src/core/generated/descriptor.c
    src/core/generated/state.c
    src/core/generated/storage.c
    src/core/generated/io/control.c
    src/core/generated/io.c
    src/core/generated/io/number/input.c
    src/core/generated/io/list.c
    src/core/generated/io/list/complex.c
    src/core/generated/io/list/read.c
    src/core/generated/io/list/token.c
    src/core/generated/io/list/write.c
    src/core/generated/mathematical.c
    src/core/generated/extremum.c
    src/core/numeric/extremum.h
    src/core/numeric/power.h
    src/core/numeric/loop.h
    src/core/generated/loop.c
    src/core/generated/real_loop.c
    src/core/generated/model.c
    src/core/generated/numeric.c
    src/core/generated/power.c
    src/core/generated/complex.c
    src/core/generated/io/number/model.c
    src/core/generated/private.h
    src/core/generated/representation.c
    src/core/generated/record.c
    src/core/generated/reduction.c
    src/core/generated/relation.c
    src/core/generated/stream.c
    src/core/generated/time.c
    src/core/generated/process_time.c
    src/core/generated/transfer.c
    src/core/support.c
    src/core/transpile.c
    src/frontend/analyze.c
    src/frontend/attributes.c
    src/frontend/declaration/common.c
    src/frontend/declaration/attribute.c
    src/frontend/declaration/dimension.c
    src/frontend/declaration/entity.c
    src/frontend/declaration/namelist.c
    src/frontend/declaration/private.h
    src/frontend/declaration/syntax.c
    src/frontend/declaration/symbol.c
    src/frontend/declaration/symbol.h
    src/frontend/declaration/bindings.c
    src/frontend/declaration/intrinsic.c
    src/frontend/declaration/type.c
    src/frontend/derived.c
    src/frontend/host.c
    src/frontend/frontend.h
    src/frontend/interface.c
    src/frontend/lexer.c
    src/frontend/module/access.c
    src/frontend/module/access.h
    src/frontend/module/constant.c
    src/frontend/module/constant.h
    src/frontend/module/dependency.c
    src/frontend/module/dependency.h
    src/frontend/module/intrinsic.c
    src/frontend/module/intrinsic.h
    src/frontend/module/resolution.c
    src/frontend/module/resolution.h
    src/frontend/module_constants.h
    src/frontend/modules.c
    src/frontend/operator.c
    src/frontend/operator.h
    src/frontend/parser.c
    src/frontend/pipeline.c
    src/frontend/pipeline.h
    src/frontend/private.h
    src/frontend/procedure.c
    src/frontend/preprocessor.c
    src/frontend/preprocessor.h
    src/frontend/preprocessor/definition.c
    src/frontend/preprocessor/expansion.c
    src/frontend/preprocessor/condition/parse.c
    src/frontend/preprocessor/condition/literal.c
    src/frontend/preprocessor/condition/operation.c
    src/frontend/preprocessor/condition/expansion.c
    src/frontend/preprocessor/condition/private.h
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
    src/ir/call.c
    src/ir/call.h
    src/ir/designator.c
    src/ir/format.c
    src/ir/format.h
    src/ir/intrinsic.h
    src/ir/statement.h
    src/ir/type.h
    src/ir/storage.c
    src/ir/storage.h
    src/semantic/character/length.c
    src/semantic/character/literal.c
    src/semantic/control_flow.c
    src/semantic/control_flow.h
    src/semantic/loop.c
    src/semantic/control_flow/connectivity.c
    src/semantic/control_flow/private.h
    src/semantic/constant.c
    src/semantic/constant/bit.c
    src/semantic/constant/character.c
    src/semantic/constant/complex.c
    src/semantic/constant/conversion.c
    src/semantic/constant/loop.c
    src/semantic/constant/mathematical.c
    src/semantic/constant/model.c
    src/semantic/constant/numeric.c
    src/semantic/constant/operator.c
    src/semantic/constant/private.h
    src/semantic/constant/real.c
    src/semantic/data_flow.h
    src/semantic/data_flow/bitset.c
    src/semantic/data_flow/temporaries.c
    src/semantic/data_flow/variables.c
    src/semantic/access.c
    src/semantic/contiguity.c
    src/semantic/designator.c
    src/semantic/host.c
    src/semantic/implicit.c
    src/semantic/implicit/declaration.c
    src/semantic/implicit/private.h
    src/semantic/implicit/symbols.c
    src/semantic/intrinsic/catalog.c
    src/semantic/intrinsic/identity.c
    src/semantic/intrinsic/extremum.c
    src/semantic/intrinsic/extremum.h
    src/semantic/intrinsic/resolution.c
    src/semantic/intrinsic.h
    src/semantic/model.h
    src/semantic/scope.c
    src/semantic/scope.h
    src/semantic/symbol.h
    src/semantic/numeric_model.c
    src/semantic/numeric_model.h
    src/semantic/operator.c
    src/semantic/operator.h
    src/semantic/procedure.c
    src/semantic/result.c
    src/semantic/semantic.h
    src/semantic/signature.c
    src/semantic/storage.c
    src/semantic/temporary.c
    src/semantic/type.c
    src/semantic/validation.c
    src/semantic/validation/allocation.c
    src/semantic/validation/assigned_label.c
    src/semantic/validation/branch.c
    src/semantic/validation/case.c
    src/semantic/validation/construct.c
    src/semantic/validation/data.c
    src/semantic/validation/expression.c
    src/semantic/validation/io.c
    src/semantic/validation/intrinsic/arguments.c
    src/semantic/validation/intrinsic/arguments.h
    src/semantic/validation/intrinsic/bit.c
    src/semantic/validation/intrinsic/character.c
    src/semantic/validation/intrinsic/conversion.c
    src/semantic/validation/intrinsic/mathematical.c
    src/semantic/validation/intrinsic/model.c
    src/semantic/validation/intrinsic/numeric.c
    src/semantic/validation/intrinsic/random.c
    src/semantic/validation/intrinsic/reduction.c
    src/semantic/validation/intrinsic/representation.c
    src/semantic/validation/intrinsic/statement.c
    src/semantic/validation/intrinsic/time.c
    src/semantic/validation/intrinsic/transform.c
    src/semantic/validation/lifetime.c
    src/semantic/validation/private.h
    src/semantic/validation/procedure.c
    src/semantic/default_initialization.c
    src/semantic/derived_storage.c
    src/semantic/default_initialization.h
    src/semantic/validation/procedure/association.c
    src/semantic/validation/procedure/storage.c
    src/semantic/validation/procedure/binding.c
    src/semantic/validation/procedure/alternate.c
    src/semantic/validation/procedure/select.c
    src/semantic/validation/statement.c
    src/semantic/validation/loop.c
    src/semantic/validation/where.c
)

function(f2c_configure_core_target target)
    target_compile_features(${target} PUBLIC c_std_17)
    if(NOT MSVC)
        # Static consumers must inherit the C math library used by semantic
        # constant evaluation. Unix, MinGW, and Emscripten provide it as libm;
        # the MSVC CRT exposes the same functions without a separate library.
        target_link_libraries(${target} PUBLIC m)
    endif()
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
    add_test(
        NAME incremental_version_metadata
        COMMAND
            ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_SOURCE_DIR}/build/version-metadata-${CMAKE_C_COMPILER_ID}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DBUILD_GENERATOR=${CMAKE_GENERATOR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/version_metadata.cmake
    )
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
    add_test(
        NAME cli_module_resolution
        COMMAND
            ${CMAKE_COMMAND}
            -DF2C=$<TARGET_FILE:f2c>
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/cli_module_resolution.cmake
    )

    add_executable(f2c_module_resolution_tests test/test_module_resolution.c)
    f2c_configure_test_target(f2c_module_resolution_tests)
    add_test(NAME module_resolution COMMAND f2c_module_resolution_tests)

    add_executable(f2c_ast_tests test/test_ast.c)
    target_include_directories(f2c_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_ast_tests)
    add_test(NAME ast COMMAND f2c_ast_tests)

    add_executable(f2c_operator_tests test/test_operator.c)
    target_include_directories(f2c_operator_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_operator_tests)
    add_test(NAME operator_semantics COMMAND f2c_operator_tests)

    add_executable(f2c_loop_tests test/test_loop.c)
    target_include_directories(f2c_loop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_loop_tests)
    add_test(NAME integer_loop_semantics COMMAND f2c_loop_tests)

    add_executable(f2c_real_loop_tests test/test_real_loop.c)
    target_include_directories(f2c_real_loop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_real_loop_tests)
    add_test(NAME real_loop_semantics COMMAND f2c_real_loop_tests)

    add_executable(f2c_parenthesized_tests test/test_parenthesized.c)
    target_include_directories(f2c_parenthesized_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_parenthesized_tests)
    add_test(NAME parenthesized_value_semantics COMMAND f2c_parenthesized_tests)

    add_executable(f2c_access_tests test/test_access.c)
    target_include_directories(f2c_access_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_access_tests)
    add_test(NAME access_qualifiers COMMAND f2c_access_tests)

    add_executable(f2c_storage_state_tests test/test_storage_state.c)
    target_include_directories(f2c_storage_state_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_storage_state_tests)
    add_test(NAME object_state_references COMMAND f2c_storage_state_tests)

    add_executable(f2c_contiguity_tests test/test_contiguity.c)
    target_include_directories(f2c_contiguity_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_contiguity_tests)
    add_test(NAME contiguous_storage_association COMMAND f2c_contiguity_tests)

    add_executable(f2c_unit_header_tests test/test_unit_header.c)
    target_include_directories(f2c_unit_header_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_unit_header_tests)
    add_test(NAME unit_header_ast COMMAND f2c_unit_header_tests)

    add_executable(f2c_procedure_ast_tests test/test_procedure_ast.c)
    target_include_directories(f2c_procedure_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_procedure_ast_tests)
    add_test(NAME procedure_declaration_ast COMMAND f2c_procedure_ast_tests)

    add_executable(f2c_use_ast_tests test/test_use_ast.c)
    target_include_directories(f2c_use_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_use_ast_tests)
    add_test(NAME use_statement_ast COMMAND f2c_use_ast_tests)

    add_executable(f2c_access_ast_tests test/test_access_ast.c)
    target_include_directories(f2c_access_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_access_ast_tests)
    add_test(NAME access_statement_ast COMMAND f2c_access_ast_tests)

    add_executable(f2c_interface_ast_tests test/test_interface_ast.c)
    target_include_directories(
        f2c_interface_ast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    f2c_configure_internal_test_target(f2c_interface_ast_tests)
    add_test(NAME interface_ast COMMAND f2c_interface_ast_tests)

    add_executable(f2c_format_tests test/test_format.c)
    target_include_directories(f2c_format_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_format_tests)
    add_test(NAME format_ast COMMAND f2c_format_tests)

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

    add_executable(f2c_numeric_model_tests test/test_numeric_model.c)
    target_include_directories(
        f2c_numeric_model_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    f2c_configure_internal_test_target(f2c_numeric_model_tests)
    add_test(NAME numeric_model COMMAND f2c_numeric_model_tests)

    add_executable(f2c_numeric_model_intrinsic_tests test/test_numeric_model_intrinsic.c)
    f2c_configure_test_target(f2c_numeric_model_intrinsic_tests)
    add_test(NAME numeric_model_intrinsic_semantics COMMAND f2c_numeric_model_intrinsic_tests)

    add_executable(f2c_intrinsic_registry_tests test/test_intrinsic_registry.c)
    target_include_directories(
        f2c_intrinsic_registry_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    f2c_configure_internal_test_target(f2c_intrinsic_registry_tests)
    add_test(NAME intrinsic_registry COMMAND f2c_intrinsic_registry_tests)

    # Compile the real registry with hostile platform-style macros, without
    # linking its ordinary copy from f2c_core into this isolated executable.
    add_executable(
        f2c_intrinsic_macro_tests
        test/test_intrinsic_macro.c
        src/semantic/intrinsic/identity.c
    )
    target_include_directories(
        f2c_intrinsic_macro_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(
        f2c_intrinsic_macro_tests PRIVATE ETIME=137 FUNCTION=211 SUBROUTINE=212
        ABS=201 MAX=202 MIN=203
    )
    f2c_configure_test_compilation(f2c_intrinsic_macro_tests)
    add_test(NAME intrinsic_platform_macros COMMAND f2c_intrinsic_macro_tests)

    add_executable(f2c_numeric_operation_intrinsic_tests test/test_numeric_operation_intrinsic.c)
    f2c_configure_test_target(f2c_numeric_operation_intrinsic_tests)
    add_test(
        NAME numeric_operation_intrinsic_semantics
        COMMAND f2c_numeric_operation_intrinsic_tests
    )

    add_executable(
        f2c_mathematical_conversion_intrinsic_tests
        test/test_mathematical_conversion_intrinsic.c
    )
    f2c_configure_test_target(f2c_mathematical_conversion_intrinsic_tests)
    add_test(
        NAME mathematical_conversion_intrinsic_semantics
        COMMAND f2c_mathematical_conversion_intrinsic_tests
    )

    add_executable(f2c_reduction_intrinsic_tests test/test_reduction_intrinsic.c)
    f2c_configure_test_target(f2c_reduction_intrinsic_tests)
    add_test(NAME reduction_intrinsic_semantics COMMAND f2c_reduction_intrinsic_tests)

    add_executable(f2c_extremum_policy_tests test/test_extremum_policy.c)
    target_include_directories(f2c_extremum_policy_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_extremum_policy_tests)
    add_test(NAME extremum_processor_policy COMMAND f2c_extremum_policy_tests)

    add_executable(f2c_extremum_binding_tests test/test_extremum_binding.c)
    target_include_directories(f2c_extremum_binding_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_extremum_binding_tests)
    add_test(NAME extremum_argument_binding COMMAND f2c_extremum_binding_tests)

    add_executable(f2c_transform_intrinsic_tests test/test_transform_intrinsic.c)
    target_include_directories(
        f2c_transform_intrinsic_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    f2c_configure_internal_test_target(f2c_transform_intrinsic_tests)
    add_test(NAME transform_intrinsic_semantics COMMAND f2c_transform_intrinsic_tests)

    add_executable(f2c_random_intrinsic_tests test/test_random_intrinsic.c)
    f2c_configure_test_target(f2c_random_intrinsic_tests)
    add_test(NAME random_intrinsic_semantics COMMAND f2c_random_intrinsic_tests)

    add_executable(f2c_time_intrinsic_tests test/test_time_intrinsic.c)
    f2c_configure_test_target(f2c_time_intrinsic_tests)
    add_test(NAME time_intrinsic_semantics COMMAND f2c_time_intrinsic_tests)

    add_executable(f2c_alternate_return_tests test/test_alternate_return.c)
    f2c_configure_test_target(f2c_alternate_return_tests)
    add_test(NAME alternate_return_semantics COMMAND f2c_alternate_return_tests)

    add_executable(f2c_host_association_tests test/test_host_association.c)
    f2c_configure_test_target(f2c_host_association_tests)
    add_test(NAME host_association_semantics COMMAND f2c_host_association_tests)

    add_executable(f2c_scope_association_tests test/test_scope_association.c)
    target_include_directories(f2c_scope_association_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_scope_association_tests)
    add_test(NAME scope_association_semantics COMMAND f2c_scope_association_tests)

    add_executable(f2c_pointer_association_tests test/test_pointer_association.c)
    f2c_configure_test_target(f2c_pointer_association_tests)
    add_test(NAME pointer_association_semantics COMMAND f2c_pointer_association_tests)

    add_executable(f2c_pointer_allocation_tests test/test_pointer_allocation.c)
    f2c_configure_test_target(f2c_pointer_allocation_tests)
    add_test(NAME pointer_allocation_semantics COMMAND f2c_pointer_allocation_tests)

    add_executable(f2c_allocation_model_tests test/test_allocation_model.c)
    f2c_configure_test_target(f2c_allocation_model_tests)
    add_test(NAME allocation_model_semantics COMMAND f2c_allocation_model_tests)

    add_executable(f2c_function_result_tests test/test_function_result.c)
    f2c_configure_test_target(f2c_function_result_tests)
    add_test(NAME function_result_semantics COMMAND f2c_function_result_tests)

    add_executable(f2c_result_retention_tests test/test_result_retention.c)
    target_include_directories(f2c_result_retention_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_result_retention_tests)
    add_test(NAME result_instance_retention COMMAND f2c_result_retention_tests)

    add_executable(f2c_procedure_signature_tests test/test_procedure_signature.c)
    f2c_configure_test_target(f2c_procedure_signature_tests)
    add_test(NAME procedure_signature_semantics COMMAND f2c_procedure_signature_tests)

    add_executable(f2c_type_binding_tests test/test_type_binding.c)
    f2c_configure_test_target(f2c_type_binding_tests)
    add_test(NAME type_binding_semantics COMMAND f2c_type_binding_tests)

    add_executable(f2c_call_mapping_tests test/test_call_mapping.c)
    target_include_directories(f2c_call_mapping_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    f2c_configure_internal_test_target(f2c_call_mapping_tests)
    add_test(NAME call_mapping COMMAND f2c_call_mapping_tests)

    add_executable(f2c_bound_call_tests test/test_bound_calls.c)
    f2c_configure_test_target(f2c_bound_call_tests)
    add_test(NAME bound_call_semantics COMMAND f2c_bound_call_tests)

    add_executable(f2c_derived_initialization_tests test/test_derived_initialization.c)
    f2c_configure_test_target(f2c_derived_initialization_tests)
    add_test(NAME derived_initialization_semantics COMMAND f2c_derived_initialization_tests)

    add_executable(f2c_control_expression_tests test/test_control_expression.c)
    f2c_configure_test_target(f2c_control_expression_tests)
    add_test(NAME control_expression_semantics COMMAND f2c_control_expression_tests)

    add_executable(f2c_control_flow_tests test/test_control_flow.c)
    f2c_configure_test_target(f2c_control_flow_tests)
    add_test(NAME control_flow_semantics COMMAND f2c_control_flow_tests)

    add_executable(f2c_control_flow_graph_tests test/test_control_flow_graph.c)
    f2c_configure_internal_test_target(f2c_control_flow_graph_tests)
    target_include_directories(
        f2c_control_flow_graph_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME control_flow_graph COMMAND f2c_control_flow_graph_tests)

    add_executable(f2c_data_flow_tests test/test_data_flow.c)
    f2c_configure_internal_test_target(f2c_data_flow_tests)
    target_include_directories(
        f2c_data_flow_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME data_flow_worklist COMMAND f2c_data_flow_tests)

    add_executable(f2c_expression_lowering_tests test/test_expression_lowering.c)
    f2c_configure_internal_test_target(f2c_expression_lowering_tests)
    target_include_directories(
        f2c_expression_lowering_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME expression_lowering_overlay COMMAND f2c_expression_lowering_tests)

    add_executable(f2c_variable_flow_tests test/test_variable_flow.c)
    f2c_configure_internal_test_target(f2c_variable_flow_tests)
    target_include_directories(
        f2c_variable_flow_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME variable_liveness COMMAND f2c_variable_flow_tests)

    add_executable(f2c_temporary_lifetime_tests test/test_temporary_lifetime.c)
    f2c_configure_internal_test_target(f2c_temporary_lifetime_tests)
    target_include_directories(
        f2c_temporary_lifetime_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME temporary_lifetime_planning COMMAND f2c_temporary_lifetime_tests)

    add_executable(
        f2c_real_representation_intrinsic_tests
        test/test_real_representation_intrinsic.c
    )
    f2c_configure_test_target(f2c_real_representation_intrinsic_tests)
    add_test(
        NAME real_representation_intrinsic_semantics
        COMMAND f2c_real_representation_intrinsic_tests
    )

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

    add_executable(f2c_argument_attribute_tests test/test_argument_attribute.c)
    f2c_configure_test_target(f2c_argument_attribute_tests)
    add_test(NAME argument_attribute_semantics COMMAND f2c_argument_attribute_tests)

    add_executable(f2c_diagnostic_tests test/test_diagnostic.c)
    f2c_configure_test_target(f2c_diagnostic_tests)
    add_test(NAME diagnostic_spans COMMAND f2c_diagnostic_tests)

    add_executable(f2c_elemental_tests test/test_elemental.c)
    f2c_configure_test_target(f2c_elemental_tests)
    add_test(NAME elemental_semantics COMMAND f2c_elemental_tests)

    add_executable(f2c_bit_intrinsic_tests test/test_bit_intrinsic.c)
    f2c_configure_test_target(f2c_bit_intrinsic_tests)
    add_test(NAME bit_intrinsic_semantics COMMAND f2c_bit_intrinsic_tests)

    add_executable(f2c_character_intrinsic_tests test/test_character_intrinsic.c)
    f2c_configure_test_target(f2c_character_intrinsic_tests)
    add_test(NAME character_intrinsic_semantics COMMAND f2c_character_intrinsic_tests)

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

    add_executable(f2c_benchmark_workspace_tests test/test_benchmark_workspaces.c)
    f2c_configure_test_compilation(f2c_benchmark_workspace_tests)
    if(NOT MSVC)
        target_link_libraries(f2c_benchmark_workspace_tests PRIVATE m)
    endif()
    add_test(NAME benchmark_workspace_contract COMMAND f2c_benchmark_workspace_tests)

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
            add_test(
                NAME pointer_remapping_runtime_guard
                COMMAND
                    ${F2C_SH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/pointer_remapping_guard.sh
                    $<TARGET_FILE:f2c>
            )
            add_test(
                NAME array_section_conformance_guard
                COMMAND
                    ${F2C_SH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/array_section_conformance_guard.sh
                    $<TARGET_FILE:f2c>
            )
            add_test(
                NAME nested_transform_derived_ownership
                COMMAND
                    ${CMAKE_COMMAND} -E env
                    CC=${CMAKE_C_COMPILER}
                    ${F2C_SH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/transform_ownership.sh
                    $<TARGET_FILE:f2c>
            )
            add_test(
                NAME namelist_transaction
                COMMAND
                    ${CMAKE_COMMAND} -E env
                    CC=${CMAKE_C_COMPILER}
                    ${F2C_SH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/namelist_transaction.sh
                    $<TARGET_FILE:f2c>
            )
            find_program(F2C_GFORTRAN_EXECUTABLE gfortran)
            add_test(
                NAME constructor_result_contract
                COMMAND
                    ${CMAKE_COMMAND} -E env
                    CC=${CMAKE_C_COMPILER}
                    ${F2C_SH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/constructor_result_contract.sh
                    $<TARGET_FILE:f2c>
            )
            add_test(
                NAME allocation_result_contract
                COMMAND
                    ${CMAKE_COMMAND} -E env
                    CC=${CMAKE_C_COMPILER}
                    ${F2C_SH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/allocation_result_contract.sh
                    $<TARGET_FILE:f2c>
            )
            if(F2C_GFORTRAN_EXECUTABLE)
                add_test(
                    NAME parameter_array_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/parameter_array_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME loop_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/loop_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME operator_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/operator_differential.sh
                        $<TARGET_FILE:f2c>
                )
                foreach(io_differential IN ITEMS io_controls file_control format)
                    add_test(
                        NAME ${io_differential}_differential
                        COMMAND
                            ${CMAKE_COMMAND} -E env
                            CC=${CMAKE_C_COMPILER}
                            FC=${F2C_GFORTRAN_EXECUTABLE}
                            ${F2C_SH_EXECUTABLE}
                            ${CMAKE_CURRENT_SOURCE_DIR}/test/${io_differential}_differential.sh
                            $<TARGET_FILE:f2c>
                    )
                endforeach()
                add_test(
                    NAME namelist_ordered_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/namelist_ordered_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME namelist_advanced
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/namelist_advanced.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME unaligned_equivalence_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/unaligned_equivalence_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME array_section_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/array_section_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME allocation_model_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/allocation_model_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME pointer_association_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/pointer_association_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME host_association_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/host_association_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME host_dynamic_association_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/host_dynamic_association_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME scope_association_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/scope_association_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME mathematical_conversion_intrinsic_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/mathematical_conversion_intrinsic_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME reduction_intrinsic_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/reduction_intrinsic_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME parenthesized_value_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/parenthesized_value_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME qualified_descriptor_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/qualified_descriptor_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME scoped_storage_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/scoped_storage_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME live_object_state_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/live_object_state_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME designator_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/designator_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME character_intrinsic_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/character_intrinsic_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME function_result_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/function_result_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME control_expression_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/control_expression_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME assigned_label_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/assigned_label_differential.sh
                        $<TARGET_FILE:f2c>
                )
                add_test(
                    NAME block_finalization_differential
                    COMMAND
                        ${CMAKE_COMMAND} -E env
                        CC=${CMAKE_C_COMPILER}
                        FC=${F2C_GFORTRAN_EXECUTABLE}
                        ${F2C_SH_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/test/block_finalization_differential.sh
                        $<TARGET_FILE:f2c>
                )
            endif()
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
