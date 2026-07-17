#include "internal/context.h"

static void apply_limit(size_t *target, size_t configured) {
    if (configured != 0U)
        *target = configured;
}

int f2c_initialize_context_limits(Context *context, const F2cConfig *config) {
    int valid = 1;
    context->limits.max_input_bytes = F2C_DEFAULT_MAX_INPUT_BYTES;
    context->limits.max_logical_lines = F2C_DEFAULT_MAX_LOGICAL_LINES;
    context->limits.max_tokens = F2C_DEFAULT_MAX_TOKENS;
    context->limits.max_ast_nodes = F2C_DEFAULT_MAX_AST_NODES;
    context->limits.max_parse_depth = F2C_DEFAULT_MAX_PARSE_DEPTH;
    context->limits.max_constant_steps = F2C_DEFAULT_MAX_CONSTANT_STEPS;
    context->limits.max_diagnostics = F2C_DEFAULT_MAX_DIAGNOSTICS;
    context->limits.max_diagnostic_bytes = F2C_DEFAULT_MAX_DIAGNOSTIC_BYTES;
    context->limits.max_output_bytes = F2C_DEFAULT_MAX_OUTPUT_BYTES;
    if (config != NULL) {
        if (config->structure_size != sizeof(*config)) {
            valid = 0;
        } else {
            apply_limit(&context->limits.max_input_bytes, config->limits.max_input_bytes);
            apply_limit(&context->limits.max_logical_lines, config->limits.max_logical_lines);
            apply_limit(&context->limits.max_tokens, config->limits.max_tokens);
            apply_limit(&context->limits.max_diagnostics, config->limits.max_diagnostics);
            apply_limit(&context->limits.max_diagnostic_bytes, config->limits.max_diagnostic_bytes);
            apply_limit(&context->limits.max_output_bytes, config->limits.max_output_bytes);
            apply_limit(&context->limits.max_ast_nodes, config->limits.max_ast_nodes);
            apply_limit(&context->limits.max_parse_depth, config->limits.max_parse_depth);
            apply_limit(&context->limits.max_constant_steps, config->limits.max_constant_steps);
            context->diagnostic_callback = config->diagnostic_callback;
            context->diagnostic_user_data = config->diagnostic_user_data;
        }
    }
    context->output.limit = context->limits.max_output_bytes;
    context->header.limit = context->limits.max_output_bytes;
    context->diagnostics.limit = context->limits.max_diagnostic_bytes;
    return valid;
}
