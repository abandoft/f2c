#ifndef F2C_F2C_H
#define F2C_F2C_H

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

#define F2C_VERSION_MAJOR 1
#define F2C_VERSION_MINOR 1
#define F2C_VERSION_PATCH 0

#define F2C_DEFAULT_MAX_INPUT_BYTES ((size_t)512U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_LOGICAL_LINES ((size_t)4U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_TOKENS ((size_t)64U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_AST_NODES ((size_t)32U * 1024U * 1024U)
#define F2C_DEFAULT_MAX_PARSE_DEPTH ((size_t)512U)
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
    F2cDiagnosticCode code;
    F2cDiagnosticSeverity severity;
    F2cSourceLocation begin;
    F2cSourceLocation end;
    /** Valid only for the duration of the callback. */
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

/** Per-request resource budgets. A zero field selects the documented default. */
typedef struct F2cLimits {
    /** Total source bytes across all project inputs. */
    size_t max_input_bytes;
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
