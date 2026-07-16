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
#define F2C_VERSION_MINOR 0
#define F2C_VERSION_PATCH 0

typedef enum F2cSourceForm { F2C_SOURCE_AUTO = 0, F2C_SOURCE_FREE, F2C_SOURCE_FIXED } F2cSourceForm;

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

/** Translate a UTF-8 Fortran source buffer into a self-contained C17 buffer. */
F2C_API F2cResult f2c_transpile(const char *source, size_t length, const F2cOptions *options);

/** Translate multiple Fortran sources as one project with shared procedure interfaces. */
F2C_API F2cResult f2c_transpile_project(const F2cInput *inputs, size_t input_count);

/** Release every allocation owned by a result. Safe for zero-initialized results. */
F2C_API void f2c_result_free(F2cResult *result);

/** Return the library version as a stable, static string. */
F2C_API const char *f2c_version(void);

#ifdef __cplusplus
}
#endif

#endif
