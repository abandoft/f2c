if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(
    GLOB_RECURSE
    PRODUCTION_FILES
    "${SOURCE_DIR}/src/*.c"
    "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.h"
)

foreach(PRODUCTION_FILE IN LISTS PRODUCTION_FILES)
    file(READ "${PRODUCTION_FILE}" CONTENT)
    file(RELATIVE_PATH RELATIVE_FILE "${SOURCE_DIR}" "${PRODUCTION_FILE}")
    string(REGEX MATCHALL "\n" LINE_BREAKS "${CONTENT}")
    list(LENGTH LINE_BREAKS LINE_COUNT)
    math(EXPR LINE_COUNT "${LINE_COUNT} + 1")
    if(LINE_COUNT GREATER 1000)
        message(FATAL_ERROR "${RELATIVE_FILE} has ${LINE_COUNT} lines; split it by responsibility")
    endif()
    if(
        PRODUCTION_FILE MATCHES "/src/codegen/"
        AND CONTENT MATCHES "f2c_parse_expression_ast[ \t\r\n]*\\("
    )
        message(FATAL_ERROR "${RELATIVE_FILE} reparses source expressions in the emitter")
    endif()
    if(CONTENT MATCHES "f2c_parse_expression_ast[ \t\r\n]*\\(")
        if(
            NOT RELATIVE_FILE STREQUAL "src/ast/parser.c"
            AND NOT RELATIVE_FILE STREQUAL "src/ir/expression.h"
        )
            message(
                FATAL_ERROR
                "${RELATIVE_FILE} bypasses the canonical source-token expression parser"
            )
        endif()
    endif()
    if(
        CONTENT MATCHES
            "f2c_(identifier|split_arguments|split_actual_arguments|split_comma_list|starts_word|evaluate_integer_text|expression_type|expression_is_designator)[ \t\r\n]*\\("
    )
        message(FATAL_ERROR "${RELATIVE_FILE} restores a removed source-text parser")
    endif()
    if(CONTENT MATCHES "f2c_parse_unit_header_tokens[ \t\r\n]*\\(")
        message(FATAL_ERROR "${RELATIVE_FILE} bypasses the program-unit syntax AST")
    endif()
    if(CONTENT MATCHES "f2c_module_header_tokens[ \t\r\n]*\\(")
        message(FATAL_ERROR "${RELATIVE_FILE} bypasses the module-header syntax AST")
    endif()
    if(CONTENT MATCHES "f2c_(program_unit|module)_end_tokens[ \t\r\n]*\\(")
        message(FATAL_ERROR "${RELATIVE_FILE} bypasses the program-unit END syntax AST")
    endif()
    if(
        NOT RELATIVE_FILE STREQUAL "src/ast/declaration/use.c"
        AND CONTENT MATCHES "f2c_line_token_equals[^\n]*\"use\""
    )
        message(FATAL_ERROR "${RELATIVE_FILE} guesses USE statements outside the canonical AST")
    endif()
    if(CONTENT MATCHES "netlib-f2c")
        message(FATAL_ERROR "${RELATIVE_FILE} references the archived netlib-f2c tree")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/CMakeLists.txt" ROOT_CMAKE)
if(ROOT_CMAKE MATCHES "netlib-f2c")
    message(FATAL_ERROR "CMakeLists.txt references the archived netlib-f2c tree")
endif()
if(ROOT_CMAKE MATCHES "install[ \t\r\n]*\\(")
    message(FATAL_ERROR "CMakeLists.txt must not define installation rules")
endif()
if(ROOT_CMAKE MATCHES "modern_f2c" OR ROOT_CMAKE MATCHES "src/cli/main\\.c[ \t\r\n]+src/")
    message(FATAL_ERROR "CMake target naming or source ownership violates the f2c architecture")
endif()

file(READ "${SOURCE_DIR}/src/semantic/model.h" SEMANTIC_MODEL)
file(READ "${SOURCE_DIR}/src/frontend/procedure.c" PROCEDURE_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/modules.c" USE_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/module/dependency.c" MODULE_DEPENDENCIES)
file(READ "${SOURCE_DIR}/src/frontend/pipeline.c" FRONTEND_PIPELINE)
file(READ "${SOURCE_DIR}/src/frontend/module/access.c" ACCESS_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/interface.c" INTERFACE_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/declaration/syntax.c" DECLARATION_CLASSIFICATION)
file(READ "${SOURCE_DIR}/src/codegen/module.c" MODULE_CODEGEN)
if(SEMANTIC_MODEL MATCHES "external_parameter_[a-z_]+[ \t\r\n]*\\[[0-9]+\\]")
    message(FATAL_ERROR "procedure signatures must use dynamic parameter storage")
endif()
if(
    NOT PROCEDURE_LOWERING MATCHES "f2c_parse_procedure_declaration_syntax[ \t\r\n]*\\("
    OR PROCEDURE_LOWERING MATCHES "f2c_(line_find_token|token_matching_delimiter)[ \t\r\n]*\\("
)
    message(FATAL_ERROR "PROCEDURE declarations must lower from their canonical syntax AST")
endif()
if(
    NOT ACCESS_LOWERING MATCHES "f2c_parse_access_statement_syntax[ \t\r\n]*\\("
    OR NOT DECLARATION_CLASSIFICATION MATCHES "f2c_access_statement_candidate[ \t\r\n]*\\("
)
    message(FATAL_ERROR
            "PUBLIC/PRIVATE statements must classify and lower through their canonical syntax AST")
endif()
if(
    NOT INTERFACE_LOWERING MATCHES
        "f2c_parse_module_procedure_statement_syntax[ \t\r\n]*\\("
    OR INTERFACE_LOWERING MATCHES "f2c_module_procedure_tokens[ \t\r\n]*\\("
)
    message(FATAL_ERROR
            "MODULE PROCEDURE bindings must lower exclusively from their canonical syntax AST")
endif()
if(
    NOT SEMANTIC_MODEL MATCHES "Unit[ \t]*\\*\\*[ \t]*generic_candidates"
    OR NOT INTERFACE_LOWERING MATCHES "rebuild_generic_candidates[ \t\r\n]*\\("
    OR NOT USE_LOWERING MATCHES "generic_candidates"
)
    message(FATAL_ERROR
            "named generic interfaces must retain and import their complete candidate sets")
endif()
if(
    NOT SEMANTIC_MODEL MATCHES "int[ \t]+use_associated"
    OR NOT USE_LOWERING MATCHES "use_associated[ \t]*=[ \t]*1"
    OR NOT MODULE_CODEGEN MATCHES "symbol->external[ \t]*\\|\\|[ \t]*symbol->use_associated"
)
    message(FATAL_ERROR
            "USE-associated module entities must preserve provider storage ownership")
endif()
if(
    NOT USE_LOWERING MATCHES "f2c_parse_use_statement_syntax[ \t\r\n]*\\("
    OR USE_LOWERING MATCHES "(parse_use_syntax|next_use_association|UseSyntax)"
)
    message(FATAL_ERROR "USE statements must lower exclusively from their canonical syntax AST")
endif()
if(
    NOT MODULE_DEPENDENCIES MATCHES "f2c_parse_use_statement_syntax[ \t\r\n]*\\("
    OR NOT FRONTEND_PIPELINE MATCHES "f2c_build_module_analysis_order[ \t\r\n]*\\("
)
    message(FATAL_ERROR "project modules must be analyzed from structured USE dependencies")
endif()
if(
    SEMANTIC_MODEL MATCHES "F2cDerivedType[ \t]*\\*[ \t]*\\*[ \t]*imported_derived_types"
    OR NOT SEMANTIC_MODEL MATCHES "F2cImportedDerivedType[ \t]*\\*[ \t]*imported_derived_types"
)
    message(FATAL_ERROR "imported derived types must retain explicit local association names")
endif()

file(READ "${SOURCE_DIR}/include/f2c/f2c.h" PUBLIC_API)
file(READ "${SOURCE_DIR}/src/core/config.c" CONFIG_IMPLEMENTATION)
file(READ "${SOURCE_DIR}/src/frontend/token.h" TOKEN_API)
file(READ "${SOURCE_DIR}/src/frontend/source.c" SOURCE_NORMALIZATION)
file(READ "${SOURCE_DIR}/src/frontend/preprocessor.c" PREPROCESSOR_IMPLEMENTATION)
if(PUBLIC_API MATCHES "F2C_CONFIG_V[0-9]" OR CONFIG_IMPLEMENTATION MATCHES "offsetof[ \\t\\r\\n]*\\([ \\t\\r\\n]*F2cConfig")
    message(FATAL_ERROR "the unfinished public API must not preserve historical configuration layouts")
endif()
if(NOT CONFIG_IMPLEMENTATION MATCHES "structure_size[ \\t\\r\\n]*!=[ \\t\\r\\n]*sizeof[ \\t\\r\\n]*\\([ \\t\\r\\n]*\\*config[ \\t\\r\\n]*\\)")
    message(FATAL_ERROR "F2cConfig must require the exact current structure size")
endif()
if(TOKEN_API MATCHES "F2cLexer" OR TOKEN_API MATCHES "f2c_lexer_(init|next)")
    message(FATAL_ERROR "the canonical token stream must not expose the removed lexer aliases")
endif()
if(
    SOURCE_NORMALIZATION MATCHES "pp_(parent|taken|depth)"
    OR SOURCE_NORMALIZATION MATCHES "use_isnan"
    OR PREPROCESSOR_IMPLEMENTATION MATCHES "use_isnan"
)
    message(FATAL_ERROR "conditional preprocessing must not restore fixed stacks or LAPACK macro guesses")
endif()

foreach(SUBSTITUTION_FILE IN ITEMS src/codegen/data.c src/codegen/array/constructor.c)
    file(READ "${SOURCE_DIR}/${SUBSTITUTION_FILE}" SUBSTITUTION_CONTENT)
    if(SUBSTITUTION_CONTENT MATCHES "[Ss]ubstitutions[ \t\r\n]*\\[64\\]")
        message(FATAL_ERROR "${SUBSTITUTION_FILE} restores a fixed implied-DO nesting limit")
    endif()
endforeach()

file(GLOB ROOT_BUILD_PATHS "${SOURCE_DIR}/build*")
foreach(ROOT_BUILD_PATH IN LISTS ROOT_BUILD_PATHS)
    get_filename_component(ROOT_BUILD_NAME "${ROOT_BUILD_PATH}" NAME)
    if(NOT ROOT_BUILD_NAME STREQUAL "build")
        message(FATAL_ERROR "build artifacts must stay under the single root build/ directory")
    endif()
endforeach()
