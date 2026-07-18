#include "internal/context.h"

#include <stdlib.h>
#include <string.h>

const char *f2c_context_source_name(Context *context, const char *source_name) {
    const char *name = source_name != NULL ? source_name : "<input>";
    size_t index;
    char **replacement;
    char *copy;
    size_t capacity;
    for (index = 0U; index < context->source_name_count; ++index) {
        if (strcmp(context->source_names[index], name) == 0)
            return context->source_names[index];
    }
    if (context->source_name_count == context->source_name_capacity) {
        capacity = context->source_name_capacity == 0U ? 8U : context->source_name_capacity * 2U;
        if (capacity < context->source_name_capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return NULL;
        replacement = (char **)realloc(context->source_names, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return NULL;
        context->source_names = replacement;
        context->source_name_capacity = capacity;
    }
    copy = f2c_strdup(name);
    if (copy == NULL)
        return NULL;
    context->source_names[context->source_name_count++] = copy;
    return copy;
}

static void apply_limit(size_t *target, size_t configured) {
    if (configured != 0U)
        *target = configured;
}

int f2c_initialize_context_limits(Context *context, const F2cConfig *config) {
    int valid = 1;
    context->limits.max_input_bytes = F2C_DEFAULT_MAX_INPUT_BYTES;
    context->limits.max_preprocessed_bytes = F2C_DEFAULT_MAX_PREPROCESSED_BYTES;
    context->limits.max_logical_lines = F2C_DEFAULT_MAX_LOGICAL_LINES;
    context->limits.max_tokens = F2C_DEFAULT_MAX_TOKENS;
    context->limits.max_ast_nodes = F2C_DEFAULT_MAX_AST_NODES;
    context->limits.max_parse_depth = F2C_DEFAULT_MAX_PARSE_DEPTH;
    context->limits.max_preprocessor_definitions = F2C_DEFAULT_MAX_PREPROCESSOR_DEFINITIONS;
    context->limits.max_macro_expansion_depth = F2C_DEFAULT_MAX_MACRO_EXPANSION_DEPTH;
    context->limits.max_macro_arguments = F2C_DEFAULT_MAX_MACRO_ARGUMENTS;
    context->limits.max_include_depth = F2C_DEFAULT_MAX_INCLUDE_DEPTH;
    context->limits.max_include_files = F2C_DEFAULT_MAX_INCLUDE_FILES;
    context->limits.max_constant_steps = F2C_DEFAULT_MAX_CONSTANT_STEPS;
    context->limits.max_diagnostics = F2C_DEFAULT_MAX_DIAGNOSTICS;
    context->limits.max_diagnostic_bytes = F2C_DEFAULT_MAX_DIAGNOSTIC_BYTES;
    context->limits.max_output_bytes = F2C_DEFAULT_MAX_OUTPUT_BYTES;
    if (config != NULL) {
        if (config->structure_size != sizeof(*config)) {
            valid = 0;
        } else {
            apply_limit(&context->limits.max_input_bytes, config->limits.max_input_bytes);
            apply_limit(&context->limits.max_preprocessed_bytes,
                        config->limits.max_preprocessed_bytes);
            apply_limit(&context->limits.max_logical_lines, config->limits.max_logical_lines);
            apply_limit(&context->limits.max_tokens, config->limits.max_tokens);
            apply_limit(&context->limits.max_diagnostics, config->limits.max_diagnostics);
            apply_limit(&context->limits.max_diagnostic_bytes, config->limits.max_diagnostic_bytes);
            apply_limit(&context->limits.max_output_bytes, config->limits.max_output_bytes);
            apply_limit(&context->limits.max_ast_nodes, config->limits.max_ast_nodes);
            apply_limit(&context->limits.max_parse_depth, config->limits.max_parse_depth);
            apply_limit(&context->limits.max_preprocessor_definitions,
                        config->limits.max_preprocessor_definitions);
            apply_limit(&context->limits.max_macro_expansion_depth,
                        config->limits.max_macro_expansion_depth);
            apply_limit(&context->limits.max_macro_arguments, config->limits.max_macro_arguments);
            apply_limit(&context->limits.max_include_depth, config->limits.max_include_depth);
            apply_limit(&context->limits.max_include_files, config->limits.max_include_files);
            apply_limit(&context->limits.max_constant_steps, config->limits.max_constant_steps);
            context->diagnostic_callback = config->diagnostic_callback;
            context->diagnostic_user_data = config->diagnostic_user_data;
            context->preprocessor_definitions = config->preprocessor_definitions;
            context->preprocessor_definition_count = config->preprocessor_definition_count;
            context->include_resolver = config->include_resolver;
            context->include_release = config->include_release;
            context->include_user_data = config->include_user_data;
        }
    }
    context->output.limit = context->limits.max_output_bytes;
    context->header.limit = context->limits.max_output_bytes;
    context->diagnostics.limit = context->limits.max_diagnostic_bytes;
    return valid;
}
