#ifndef F2C_FRONTEND_PREPROCESSOR_H
#define F2C_FRONTEND_PREPROCESSOR_H

#include "internal/context.h"

typedef struct F2cPreprocessedSource {
    char *text;
    size_t length;
    F2cSourceMap source_map;
} F2cPreprocessedSource;

/* Apply the bounded preprocessing contract while retaining exact source
 * provenance for every emitted run. */
int f2c_preprocess_source(Context *context, const char *source, size_t length, F2cSourceForm form,
                          F2cPreprocessedSource *result);
void f2c_preprocessed_source_discard(F2cPreprocessedSource *source);

#endif
