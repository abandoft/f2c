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
    if(
        PRODUCTION_FILE MATCHES "/src/codegen/"
        AND CONTENT MATCHES
            "(temporary_index|contiguous_temporary_index|host_descriptor_temporary_(begin|count)|ordered_temporary_index|ordered_argument_temporary_index|statement_temporary_index|statement_nested_temporary_begin|lifetime_statement_index|temporary_lifetime_analyzed|owned_temporary_index|owned_temporary_kind|temporary_ownership_analyzed)[ \t]*=[^=]"
    )
        message(
            FATAL_ERROR
            "${RELATIVE_FILE} mutates semantic temporary planning during code generation"
        )
    endif()
    if(
        PRODUCTION_FILE MATCHES "/src/(ast|frontend|ir|semantic)/"
        AND CONTENT MATCHES "f2c_lowering_[a-z_]+[ \t\r\n]*\\("
    )
        message(
            FATAL_ERROR
            "${RELATIVE_FILE} couples typed source/semantic state to code-generation lowering"
        )
    endif()
    if(
        PRODUCTION_FILE MATCHES "/src/codegen/"
        AND NOT RELATIVE_FILE STREQUAL "src/codegen/lowering/expression.c"
        AND CONTENT MATCHES "f2c_expr_free[ \t\r\n]*\\("
    )
        message(
            FATAL_ERROR
            "${RELATIVE_FILE} frees an emitted expression without clearing lowering state"
        )
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
    if(CONTENT MATCHES "f2c_validation_bind_intrinsic_arguments[ \t\r\n]*\\(")
        message(
            FATAL_ERROR
            "${RELATIVE_FILE} bypasses registered intrinsic argument schemas"
        )
    endif()
    if(
        CONTENT MATCHES
            "strcmp[ \t\r\n]*\\([^\\n]*,[ \t\r\n]*\"null\"[ \t\r\n]*\\)"
    )
        message(FATAL_ERROR "${RELATIVE_FILE} dispatches NULL by source spelling")
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
file(READ "${SOURCE_DIR}/src/ir/expression.h" EXPRESSION_IR)
file(READ "${SOURCE_DIR}/src/frontend/procedure.c" PROCEDURE_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/modules.c" USE_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/module/dependency.c" MODULE_DEPENDENCIES)
file(READ "${SOURCE_DIR}/src/frontend/module/intrinsic.c" INTRINSIC_MODULES)
file(READ "${SOURCE_DIR}/src/frontend/module/resolution.c" MODULE_RESOLUTION)
file(READ "${SOURCE_DIR}/src/frontend/pipeline.c" FRONTEND_PIPELINE)
file(READ "${SOURCE_DIR}/src/frontend/module/access.c" ACCESS_LOWERING)
file(READ "${SOURCE_DIR}/src/frontend/interface.c" INTERFACE_LOWERING)
file(READ "${SOURCE_DIR}/src/cli/main.c" CLI_MAIN)
file(READ "${SOURCE_DIR}/src/semantic/validation/expression.c" EXPRESSION_VALIDATION)
file(READ "${SOURCE_DIR}/src/semantic/validation/statement.c" STATEMENT_VALIDATION)
file(READ "${SOURCE_DIR}/src/codegen/expression.c" EXPRESSION_CODEGEN)
file(READ "${SOURCE_DIR}/src/codegen/statement/assignment.c" ASSIGNMENT_CODEGEN)
file(READ "${SOURCE_DIR}/src/frontend/declaration/syntax.c" DECLARATION_CLASSIFICATION)
file(READ "${SOURCE_DIR}/src/codegen/module.c" MODULE_CODEGEN)
file(READ "${SOURCE_DIR}/src/semantic/temporary.c" TEMPORARY_PLANNING)
file(READ "${SOURCE_DIR}/src/codegen/unit.c" UNIT_CODEGEN)
file(READ "${SOURCE_DIR}/src/codegen/unit/temporary.c" TEMPORARY_DECLARATIONS)
file(READ "${SOURCE_DIR}/src/codegen/array/function.c" ARRAY_FUNCTION_RESULTS)
file(READ "${SOURCE_DIR}/src/codegen/array/temporary.c" ARRAY_TEMPORARIES)
file(READ "${SOURCE_DIR}/src/codegen/array/ownership.c" ARRAY_OWNERSHIP)
file(READ "${SOURCE_DIR}/src/codegen/call.c" CALL_LOWERING)
file(READ "${SOURCE_DIR}/src/codegen/expression/call.c" EXPRESSION_CALL_LOWERING)
file(READ "${SOURCE_DIR}/src/codegen/lowering.c" INTRINSIC_EMISSION)
file(READ "${SOURCE_DIR}/src/semantic/intrinsic.h" INTRINSIC_API)
file(READ "${SOURCE_DIR}/src/semantic/intrinsic/catalog.c" INTRINSIC_CATALOG)
file(READ "${SOURCE_DIR}/src/semantic/intrinsic/identity.c" INTRINSIC_IDENTITY)
file(READ
     "${SOURCE_DIR}/src/semantic/validation/intrinsic/statement.c"
     INTRINSIC_STATEMENT_VALIDATION
)
if(
    EXPRESSION_IR MATCHES
        "(lowered_c|lowered_extent_c|lowered_character_length_c|lowered_array_temporary|ordered_argument_materialized)"
)
    message(FATAL_ERROR "typed expression IR must not contain code-generation lowering state")
endif()
if(
    INTRINSIC_EMISSION MATCHES
        "strcmp[ \t\r\n]*\\([^\\n]*,[ \t\r\n]*\"(abs|dabs|dsqrt|dexp|dlog|dsin|dcos|conjg|dconjg|aimag|dimag|dreal|cabs|cdabs|sqrt|sin|cos|tan|exp|log|log10|atan|asin|acos|atan2|max|min|real|dble|float|int|cmplx|dcmplx|ichar|char|len|len_trim|alog|maxloc|maxval)\"[ \t\r\n]*\\)"
)
    message(FATAL_ERROR "standard intrinsic emission must dispatch by typed identity")
endif()
if(
    INTRINSIC_CATALOG MATCHES
        "f2c_resolve_intrinsic_(type|rank|kind)[ \t\r\n]*\\("
)
    message(FATAL_ERROR "intrinsic catalog must not absorb typed result resolution")
endif()
if(
    NOT INTRINSIC_API MATCHES "typedef[ \t\r\n]+struct[ \t\r\n]+F2cIntrinsicSpecification"
    OR NOT INTRINSIC_IDENTITY MATCHES
        "static[ \t\r\n]+const[ \t\r\n]+F2cIntrinsicSpecification[ \t\r\n]+specifications"
    OR INTRINSIC_IDENTITY MATCHES
        "static[ \t\r\n]+const[ \t\r\n]+F2cIntrinsic(Descriptor|ArgumentSchema)[ \t\r\n]+[a-z_]+[ \t\r\n]*\\["
    OR INTRINSIC_CATALOG MATCHES
        "static[ \t\r\n]+const[ \t\r\n]+F2cIntrinsicSignature[ \t\r\n]+intrinsic_signatures"
)
    message(
        FATAL_ERROR
        "canonical intrinsic identity, argument schema, and signature must share one specification table"
    )
endif()
if(
    NOT INTRINSIC_STATEMENT_VALIDATION MATCHES
        "f2c_find_intrinsic_specification[ \t\r\n]*\\("
    OR NOT INTRINSIC_STATEMENT_VALIDATION MATCHES
        "statement->intrinsic[ \t]*=[ \t]*specification->descriptor\\.id"
    OR NOT INTRINSIC_STATEMENT_VALIDATION MATCHES
        "switch[ \t\r\n]*\\([ \t\r\n]*statement->intrinsic[ \t\r\n]*\\)"
)
    message(FATAL_ERROR "intrinsic CALL validation must bind and dispatch by typed identity")
endif()
foreach(
    INTRINSIC_CALL_FILE
    IN ITEMS
        src/semantic/validation/intrinsic/statement.c
        src/semantic/validation/intrinsic/bit.c
        src/semantic/validation/intrinsic/random.c
        src/semantic/validation/intrinsic/time.c
)
    file(READ "${SOURCE_DIR}/${INTRINSIC_CALL_FILE}" INTRINSIC_CALL_CONTENT)
    if(
        INTRINSIC_CALL_CONTENT MATCHES
            "strcmp[ \t\r\n]*\\([^\\n]*\"(mvbits|random_number|random_seed|cpu_time|date_and_time|system_clock)\""
    )
        message(FATAL_ERROR "${INTRINSIC_CALL_FILE} dispatches an intrinsic CALL by source spelling")
    endif()
    if(
        NOT INTRINSIC_CALL_FILE STREQUAL "src/semantic/validation/intrinsic/statement.c"
        AND INTRINSIC_CALL_CONTENT MATCHES "statement->intrinsic[ \t]*=[^=]"
    )
        message(
            FATAL_ERROR
            "${INTRINSIC_CALL_FILE} rebinds an intrinsic CALL outside the central dispatcher"
        )
    endif()
endforeach()
if(SEMANTIC_MODEL MATCHES "external_parameter_[a-z_]+[ \t\r\n]*\\[[0-9]+\\]")
    message(FATAL_ERROR "procedure signatures must use dynamic parameter storage")
endif()
if(
    NOT CALL_LOWERING MATCHES
        "lowering_arguments\\[i\\][ \t]*=[ \t]*f2c_array_clone_expression[ \t\r\n]*\\(unit,[ \t\r\n]*argument_expressions\\[i\\]\\)"
    OR NOT CALL_LOWERING MATCHES
        "argument_expressions[ \t]*=[ \t]*lowering_arguments"
    OR NOT EXPRESSION_CALL_LOWERING MATCHES
        "lowering_expression[ \t]*=[ \t]*f2c_array_clone_expression[ \t\r\n]*\\(unit,[ \t\r\n]*expression\\)"
    OR EXPRESSION_CALL_LOWERING MATCHES "restore_ordered_arguments"
)
    message(
        FATAL_ERROR
        "call lowering must mutate private lowering clones rather than the typed expression IR"
    )
endif()
if(
    ARRAY_FUNCTION_RESULTS MATCHES "Buffer[ \t]*\\*cleanup"
    OR ARRAY_TEMPORARIES MATCHES "Buffer[ \t]*\\*cleanup"
    OR ARRAY_FUNCTION_RESULTS MATCHES
        "f2c_buffer_(append|printf)[ \t\r\n]*\\([^,]*cleanup"
    OR ARRAY_TEMPORARIES MATCHES
        "f2c_buffer_(append|printf)[ \t\r\n]*\\([^,]*cleanup"
    OR NOT ARRAY_OWNERSHIP MATCHES "F2cArrayCleanupAction"
    OR NOT ARRAY_OWNERSHIP MATCHES "f2c_array_cleanup_append[ \t\r\n]*\\("
)
    message(
        FATAL_ERROR
        "owned array results must lower through typed cleanup actions, not cleanup strings"
    )
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
        "f2c_parse_interface_header_syntax[ \t\r\n]*\\("
    OR NOT INTERFACE_LOWERING MATCHES
        "f2c_parse_interface_specific_syntax[ \t\r\n]*\\("
)
    message(FATAL_ERROR
            "INTERFACE headers and PROCEDURE bindings must lower from their canonical syntax AST")
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
    NOT EXPRESSION_VALIDATION MATCHES "f2c_validation_generic_specific[ \t\r\n]*\\("
    OR NOT EXPRESSION_VALIDATION MATCHES "resolved_procedure[ \t]*=[ \t]*definition"
    OR NOT EXPRESSION_CODEGEN MATCHES "expression->resolved_procedure"
)
    message(FATAL_ERROR
            "defined and extended operators must lower through typed generic resolution")
endif()
if(
    NOT STATEMENT_VALIDATION MATCHES "assignment\\(=\\)"
    OR NOT STATEMENT_VALIDATION MATCHES "resolved_procedure[ \t]*=[ \t]*definition"
    OR NOT ASSIGNMENT_CODEGEN MATCHES "statement->resolved_procedure"
)
    message(FATAL_ERROR
            "defined assignment must lower through typed generic resolution")
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
    NOT USE_LOWERING MATCHES "f2c_permitted_external_module[ \t\r\n]*\\("
    OR NOT USE_LOWERING MATCHES
        "non-intrinsic module '%s' is not present in this project request"
    OR NOT INTRINSIC_MODULES MATCHES "f2c_supported_intrinsic_module[ \t\r\n]*\\("
    OR NOT INTRINSIC_MODULES MATCHES "f2c_import_constant_module[ \t\r\n]*\\("
    OR NOT MODULE_RESOLUTION MATCHES "context->external_module_count"
)
    message(
        FATAL_ERROR
        "USE resolution must distinguish supported intrinsic, project, and explicitly external modules"
    )
endif()
if(
    NOT CLI_MAIN MATCHES "--external-module"
    OR NOT CLI_MAIN MATCHES
        "config\\.external_module_names[ \t]*=[ \t]*external_modules"
    OR NOT CLI_MAIN MATCHES
        "config\\.external_module_count[ \t]*=[ \t]*external_module_count"
)
    message(FATAL_ERROR "the CLI must forward explicit external-module providers to the API")
endif()
if(
    NOT FRONTEND_PIPELINE MATCHES "f2c_plan_expression_lifetimes[ \t\r\n]*\\("
    OR NOT TEMPORARY_PLANNING MATCHES "expression_lifetimes_analyzed[ \t]*=[ \t]*1"
    OR NOT UNIT_CODEGEN MATCHES "unit->expression_lifetimes_analyzed"
    OR TEMPORARY_DECLARATIONS MATCHES "f2c_unit_prepare_expression_temporaries"
)
    message(
        FATAL_ERROR
        "temporary lifetimes must be planned in semantic typed IR before code generation"
    )
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
if(
    NOT PUBLIC_API MATCHES "size_t[ \t]+max_external_modules"
    OR NOT PUBLIC_API MATCHES "external_module_names"
    OR NOT PUBLIC_API MATCHES "size_t[ \t]+external_module_count"
    OR NOT CONFIG_IMPLEMENTATION MATCHES "f2c_validate_context_configuration"
    OR NOT CONFIG_IMPLEMENTATION MATCHES "is not a valid Fortran name"
)
    message(FATAL_ERROR "external-module permissions must be validated request-local API state")
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
