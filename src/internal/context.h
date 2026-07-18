#ifndef F2C_INTERNAL_CONTEXT_H
#define F2C_INTERNAL_CONTEXT_H

#include "ir/statement.h"

typedef struct Units {
    Unit *items;
    size_t count;
    size_t capacity;
} Units;

typedef enum F2cCompilationPhase {
    F2C_COMPILATION_SOURCE,
    F2C_COMPILATION_SYNTAX,
    F2C_COMPILATION_TYPED_IR,
    F2C_COMPILATION_EMITTED
} F2cCompilationPhase;

struct Context {
    F2cCompilationPhase phase;
    F2cResult result;
    Buffer output;
    Buffer header;
    Buffer diagnostics;
    const F2cOptions *options;
    Lines lines;
    Units units;
    Units modules;
    Procedures procedures;
    F2cLimits limits;
    F2cDiagnosticCallback diagnostic_callback;
    void *diagnostic_user_data;
    const F2cPreprocessorDefinition *preprocessor_definitions;
    size_t preprocessor_definition_count;
    F2cIncludeResolver include_resolver;
    F2cIncludeRelease include_release;
    void *include_user_data;
    char **source_names;
    size_t source_name_count;
    size_t source_name_capacity;
    size_t input_bytes;
    size_t preprocessed_bytes;
    size_t include_file_count;
    size_t token_count;
    size_t ast_node_count;
    size_t constant_evaluation_steps;
    size_t diagnostic_count;
    int ast_node_limit_reported;
    int parse_depth_limit_reported;
    int constant_depth_limit_reported;
    int constant_step_limit_reported;
    int diagnostic_limit_reported;
    int diagnostic_bytes_limit_reported;
};

int f2c_initialize_context_limits(Context *context, const F2cConfig *config);
const char *f2c_context_source_name(Context *context, const char *source_name);

#endif
