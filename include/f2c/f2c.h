#ifndef F2C_F2C_H
#define F2C_F2C_H

#include "f2c/version.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(F2C_SHARED)
#if defined(F2C_BUILDING_LIBRARY)
#define F2C_API __declspec(dllexport)
#else
#define F2C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define F2C_API __attribute__((visibility("default")))
#else
#define F2C_API
#endif

#define F2C_DEFAULT_MAX_INPUT_BYTES ((size_t)512U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_PREPROCESSED_BYTES ((size_t)512U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_LOGICAL_LINES ((size_t)4U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_TOKENS ((size_t)64U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_AST_NODES ((size_t)32U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_PARSE_DEPTH ((size_t)512U)
#define F2C_DEFAULT_MAX_PREPROCESSOR_DEFINITIONS ((size_t)1024U * 1024U)
#define F2C_DEFAULT_MAX_MACRO_EXPANSION_DEPTH ((size_t)256U)
#define F2C_DEFAULT_MAX_INCLUDE_DEPTH ((size_t)256U)
#define F2C_DEFAULT_MAX_INCLUDE_FILES ((size_t)65536U)
#define F2C_DEFAULT_MAX_CONSTANT_STEPS ((size_t)16U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_DIAGNOSTICS ((size_t)10000U)
#define F2C_DEFAULT_MAX_DIAGNOSTIC_BYTES ((size_t)16U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_OUTPUT_BYTES ((size_t)1024U * 1024U * 1024U)

typedef enum F2cSourceForm { F2C_SOURCE_AUTO = 0, F2C_SOURCE_FREE, F2C_SOURCE_FIXED } F2cSourceForm;

typedef enum F2cDiagnosticSeverity {
    F2C_DIAGNOSTIC_NOTE = 0,
    F2C_DIAGNOSTIC_WARNING = 1,
    F2C_DIAGNOSTIC_ERROR = 2,
    F2C_DIAGNOSTIC_FATAL = 3
} F2cDiagnosticSeverity;

/** Structured diagnostic categories for the current public API. */
typedef enum F2cDiagnosticCode {
    F2C_DIAGNOSTIC_GENERAL = 1,
    F2C_DIAGNOSTIC_INVALID_ARGUMENT = 1000,
    F2C_DIAGNOSTIC_RESOURCE_LIMIT = 1001,
    F2C_DIAGNOSTIC_OUT_OF_MEMORY = 1002,
    F2C_DIAGNOSTIC_INVALID_TOKEN = 2000,
    F2C_DIAGNOSTIC_SYNTAX = 2001,
    F2C_DIAGNOSTIC_INCLUDE = 2002,
    F2C_DIAGNOSTIC_SEMANTIC = 3000,
    F2C_DIAGNOSTIC_UNSUPPORTED = 4000,
    F2C_DIAGNOSTIC_INTERNAL = 9000
} F2cDiagnosticCode;

typedef struct F2cSourceLocation {
    const char *source_name;
    size_t line;
    size_t column;
} F2cSourceLocation;

typedef struct F2cDiagnostic {
    /** All pointed-to strings are valid only for the duration of the callback. */
    F2cDiagnosticCode code;
    F2cDiagnosticSeverity severity;
    F2cSourceLocation begin;
    F2cSourceLocation end;
    /** Macro-definition spelling range when the primary range came from expansion. */
    F2cSourceLocation spelling_begin;
    F2cSourceLocation spelling_end;
    int has_spelling_location;
    const char *message;
    size_t message_length;
} F2cDiagnostic;

typedef void (*F2cDiagnosticCallback)(const F2cDiagnostic *diagnostic, void *user_data);

typedef struct F2cOptions {
    const char *source_name;
    F2cSourceForm source_form;
    int emit_source_comments;
} F2cOptions;

typedef struct F2cResult {
    /** Self-contained generated C17 implementation. */
    char *code;
    /** Shared C17 declarations for the procedures defined by the project. */
    char *header;
    /** Newline-delimited diagnostics; never owned by the caller separately. */
    char *diagnostics;
    size_t error_count;
    size_t warning_count;
} F2cResult;

typedef struct F2cInput {
    const char *source;
    size_t length;
    F2cOptions options;
} F2cInput;

/**
 * A case-sensitive object-like definition used by conditional preprocessing.
 * A null value is equivalent to "1". Definitions may be referenced by #if,
 * #ifdef, #ifndef and #elif, and are expanded as object-like macros in active
 * Fortran source. Function-like macros are not part of this contract.
 */
typedef struct F2cPreprocessorDefinition {
    const char *name;
    const char *value;
} F2cPreprocessorDefinition;

typedef enum F2cIncludeKind { F2C_INCLUDE_QUOTED = 0, F2C_INCLUDE_SYSTEM = 1 } F2cIncludeKind;

typedef enum F2cIncludeStatus {
    F2C_INCLUDE_NOT_FOUND = 0,
    F2C_INCLUDE_FOUND = 1,
    F2C_INCLUDE_ERROR = 2
} F2cIncludeStatus;

typedef struct F2cIncludeRequest {
    const char *including_source_name;
    const char *requested_name;
    F2cIncludeKind kind;
} F2cIncludeRequest;

/**
 * A resolver-owned source buffer. On F2C_INCLUDE_FOUND, source and source_name
 * remain valid until the matching release callback. source_name should be a
 * stable, normalized identity so recursive include cycles can be diagnosed.
 */
typedef struct F2cIncludeSource {
    const char *source_name;
    const char *source;
    size_t length;
    F2cSourceForm source_form;
    void *handle;
} F2cIncludeSource;

typedef F2cIncludeStatus (*F2cIncludeResolver)(const F2cIncludeRequest *request,
                                               F2cIncludeSource *result, void *user_data);
/** Called exactly once for each F2C_INCLUDE_FOUND result when non-null. */
typedef void (*F2cIncludeRelease)(F2cIncludeSource *source, void *user_data);

/** Per-request resource budgets. A zero field selects the documented default. */
typedef struct F2cLimits {
    /** Total source bytes across all project inputs. */
    size_t max_input_bytes;
    /** Total bytes retained after conditional preprocessing. */
    size_t max_preprocessed_bytes;
    /** Total normalized logical lines across all project inputs. */
    size_t max_logical_lines;
    /** Total canonical lexical tokens across all project inputs. */
    size_t max_tokens;
    size_t max_diagnostics;
    size_t max_diagnostic_bytes;
    /** Maximum size of each generated code and header artifact. */
    size_t max_output_bytes;
    /** Total expression AST nodes constructed across the request. */
    size_t max_ast_nodes;
    /** Maximum recursive expression parsing and constant-evaluation depth. */
    size_t max_parse_depth;
    /** Maximum number of simultaneously defined conditional macros. */
    size_t max_preprocessor_definitions;
    /** Maximum recursive expansion depth while evaluating #if expressions. */
    size_t max_macro_expansion_depth;
    /** Maximum recursive #include depth. */
    size_t max_include_depth;
    /** Maximum number of resolved include files across the request. */
    size_t max_include_files;
    /** Total integer constant-evaluation steps across the request. */
    size_t max_constant_steps;
} F2cLimits;

/** Current project configuration. structure_size must equal sizeof(F2cConfig). */
typedef struct F2cConfig {
    size_t structure_size;
    F2cLimits limits;
    /** Optional synchronous structured-diagnostic sink. */
    F2cDiagnosticCallback diagnostic_callback;
    void *diagnostic_user_data;
    /** Request-wide initial definitions; in-source definitions remain input-local. */
    const F2cPreprocessorDefinition *preprocessor_definitions;
    size_t preprocessor_definition_count;
    /** Optional request-local include provider; the core library never opens files itself. */
    F2cIncludeResolver include_resolver;
    F2cIncludeRelease include_release;
    void *include_user_data;
} F2cConfig;

/** Translate a UTF-8 Fortran source buffer into a self-contained C17 buffer. */
F2C_API F2cResult f2c_transpile(const char *source, size_t length, const F2cOptions *options);

/** Translate multiple Fortran sources as one project with shared procedure interfaces. */
F2C_API F2cResult f2c_transpile_project(const F2cInput *inputs, size_t input_count);

/** Translate a project with explicit, request-local resource budgets. */
F2C_API F2cResult f2c_transpile_project_config(const F2cInput *inputs, size_t input_count,
                                               const F2cConfig *config);

/** Release every allocation owned by a result. Safe for zero-initialized results. */
F2C_API void f2c_result_free(F2cResult *result);

/** Return the library version as a stable, static string. */
F2C_API const char *f2c_version(void);

#ifdef __cplusplus
}
#endif

#endif
