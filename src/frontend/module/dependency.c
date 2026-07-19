#include "frontend/module/dependency.h"

#include "ast/declaration/use.h"
#include "internal/f2c.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct F2cModuleDependency {
    size_t provider;
    size_t consumer;
    F2cSourceSpan module_name_span;
} F2cModuleDependency;

static size_t find_project_module(const Context *context, const F2cToken *name) {
    size_t index;
    for (index = 0U; index < context->modules.count; ++index) {
        if (f2c_token_equals(name, context->modules.items[index].name))
            return index;
    }
    return SIZE_MAX;
}

static int append_dependency(F2cModuleDependency **dependencies, size_t *count, size_t *capacity,
                             F2cModuleDependency dependency) {
    F2cModuleDependency *replacement;
    if (*count == *capacity) {
        const size_t next = *capacity == 0U ? 8U : *capacity * 2U;
        if (next < *capacity || next > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement = (F2cModuleDependency *)realloc(*dependencies, next * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        *dependencies = replacement;
        *capacity = next;
    }
    (*dependencies)[(*count)++] = dependency;
    return 1;
}

static int collect_dependencies(Context *context, F2cModuleDependency **dependencies,
                                size_t *dependency_count, size_t *dependency_capacity,
                                size_t *indegree) {
    size_t *seen;
    size_t consumer;
    int success = 0;
    seen = (size_t *)calloc(context->modules.count, sizeof(*seen));
    if (seen == NULL) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                            "out of memory collecting module dependencies");
        return 0;
    }
    for (consumer = 0U; consumer < context->modules.count; ++consumer) {
        const Unit *module = &context->modules.items[consumer];
        size_t line_index;
        for (line_index = module->begin + 1U; line_index < module->end; ++line_index) {
            F2cUseStatementSyntax syntax;
            const F2cUseStatementStatus status =
                f2c_parse_use_statement_syntax(&context->lines.items[line_index], &syntax);
            size_t provider;
            F2cModuleDependency dependency;
            if (status == F2C_USE_STATEMENT_NOT_MATCHED)
                continue;
            if (status == F2C_USE_STATEMENT_NO_MEMORY) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax.span, 1,
                                         "out of memory resolving module dependencies");
                f2c_use_statement_syntax_discard(&syntax);
                goto cleanup;
            }
            if (status == F2C_USE_STATEMENT_INVALID || syntax.nature == F2C_USE_NATURE_INTRINSIC) {
                f2c_use_statement_syntax_discard(&syntax);
                continue;
            }
            provider = find_project_module(context, syntax.module_name);
            if (provider == SIZE_MAX || seen[provider] == consumer + 1U) {
                f2c_use_statement_syntax_discard(&syntax);
                continue;
            }
            seen[provider] = consumer + 1U;
            dependency.provider = provider;
            dependency.consumer = consumer;
            dependency.module_name_span = syntax.module_name->span;
            if (!append_dependency(dependencies, dependency_count, dependency_capacity,
                                   dependency)) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                         &dependency.module_name_span, 1,
                                         "out of memory recording module dependency");
                f2c_use_statement_syntax_discard(&syntax);
                goto cleanup;
            }
            ++indegree[consumer];
            f2c_use_statement_syntax_discard(&syntax);
        }
    }
    success = 1;

cleanup:
    free(seen);
    return success;
}

static const F2cModuleDependency *find_cycle_edge(const F2cModuleDependency *dependencies,
                                                  size_t dependency_count, const size_t *indegree) {
    size_t index;
    for (index = 0U; index < dependency_count; ++index) {
        const F2cModuleDependency *dependency = &dependencies[index];
        if (indegree[dependency->provider] != SIZE_MAX &&
            indegree[dependency->consumer] != SIZE_MAX)
            return dependency;
    }
    return NULL;
}

int f2c_build_module_analysis_order(Context *context, size_t **order) {
    F2cModuleDependency *dependencies = NULL;
    size_t dependency_count = 0U;
    size_t dependency_capacity = 0U;
    size_t *indegree = NULL;
    size_t *result = NULL;
    size_t position;
    int success = 0;
    if (context == NULL || order == NULL)
        return 0;
    *order = NULL;
    if (context->modules.count == 0U)
        return 1;
    if (context->modules.count > SIZE_MAX / sizeof(*indegree))
        goto out_of_memory;
    indegree = (size_t *)calloc(context->modules.count, sizeof(*indegree));
    result = (size_t *)malloc(context->modules.count * sizeof(*result));
    if (indegree == NULL || result == NULL)
        goto out_of_memory;
    if (!collect_dependencies(context, &dependencies, &dependency_count, &dependency_capacity,
                              indegree))
        goto cleanup;
    for (position = 0U; position < context->modules.count; ++position) {
        size_t selected;
        size_t edge;
        for (selected = 0U; selected < context->modules.count; ++selected) {
            if (indegree[selected] == 0U)
                break;
        }
        if (selected == context->modules.count) {
            const F2cModuleDependency *cycle =
                find_cycle_edge(dependencies, dependency_count, indegree);
            if (cycle != NULL) {
                context->options = &context->modules.items[cycle->consumer].options;
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &cycle->module_name_span, 1,
                    "cyclic project module dependency: module '%s' USEs module '%s'",
                    context->modules.items[cycle->consumer].name,
                    context->modules.items[cycle->provider].name);
            } else {
                f2c_diagnostic_code(context, F2C_DIAGNOSTIC_INTERNAL, 1U, 1,
                                    "unable to order project module analysis");
            }
            goto cleanup;
        }
        result[position] = selected;
        indegree[selected] = SIZE_MAX;
        for (edge = 0U; edge < dependency_count; ++edge) {
            if (dependencies[edge].provider == selected &&
                indegree[dependencies[edge].consumer] != SIZE_MAX)
                --indegree[dependencies[edge].consumer];
        }
    }
    *order = result;
    result = NULL;
    success = 1;
    goto cleanup;

out_of_memory:
    f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                        "out of memory ordering project modules");

cleanup:
    free(dependencies);
    free(indegree);
    free(result);
    return success;
}
