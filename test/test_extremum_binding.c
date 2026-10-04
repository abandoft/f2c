#include "internal/f2c.h"
#include "semantic/intrinsic/extremum.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_compact_binding(void) {
    F2cExpr value = {.kind = F2C_EXPR_NAME};
    F2cExpr *value_children[] = {&value};
    F2cExpr first = {.kind = F2C_EXPR_KEYWORD_ARGUMENT,
                     .text = "a1",
                     .children = value_children,
                     .child_count = 1U};
    F2cExpr second = {.kind = F2C_EXPR_KEYWORD_ARGUMENT,
                      .text = "a2",
                      .children = value_children,
                      .child_count = 1U};
    char name[32];
    (void)snprintf(name, sizeof(name), "a%zu", SIZE_MAX);
    F2cExpr last = {.kind = F2C_EXPR_KEYWORD_ARGUMENT,
                    .text = name,
                    .children = value_children,
                    .child_count = 1U};
    F2cExpr *children[] = {&last, &second, &first};
    F2cExpr call = {.kind = F2C_EXPR_CALL, .children = children, .child_count = 3U};
    F2cExtremumBinding bound;
    expect(f2c_extremum_bind(&call, &bound) && bound.count == 3U &&
               bound.values == bound.inline_values && bound.values[0].actual == &first &&
               bound.values[1].actual == &second && bound.values[2].index == SIZE_MAX - 1U,
           "huge sparse dummy indices use compact inline storage");
    f2c_extremum_binding_clear(&bound);
    last.text = "a2";
    expect(!f2c_extremum_bind(&call, &bound) && bound.error == F2C_EXTREMUM_BINDING_DUPLICATE &&
               bound.offending == &second,
           "duplicate diagnostics retain deterministic source order after sorting");
    f2c_extremum_binding_clear(&bound);
    first.text = "a3";
    last.text = "a65";
    expect(!f2c_extremum_bind(&call, &bound) && bound.error == F2C_EXTREMUM_BINDING_MISSING &&
               bound.error_index == 0U,
           "optional sparse values cannot substitute for required A1");
    f2c_extremum_binding_clear(&bound);
    last.text = "a18446744073709551616";
    expect(!f2c_extremum_bind(&call, &bound) && bound.error == F2C_EXTREMUM_BINDING_UNKNOWN_NAME,
           "overflowing dummy indices are rejected without wraparound");
    f2c_extremum_binding_clear(&bound);
    call.child_count = SIZE_MAX;
    expect(!f2c_extremum_bind(&call, &bound) && bound.error == F2C_EXTREMUM_BINDING_ALLOCATION,
           "binding allocation multiplication cannot overflow");
    f2c_extremum_binding_clear(&bound);
}

static void test_large_binding(void) {
    F2cExpr value = {.kind = F2C_EXPR_NAME};
    F2cExpr *children[1024];
    for (size_t i = 0U; i < 1024U; ++i)
        children[i] = &value;
    F2cExpr call = {.kind = F2C_EXPR_CALL, .children = children, .child_count = 1024U};
    F2cExtremumBinding bound;
    expect(f2c_extremum_bind(&call, &bound) && bound.count == 1024U &&
               bound.values != bound.inline_values && bound.values[1023].index == 1023U,
           "large positional lists allocate by actual count without a fixed arity cap");
    f2c_extremum_binding_clear(&bound);
    expect(bound.values == NULL && bound.count == 0U, "heap binding cleanup clears borrowed state");
}

static void test_resource_budgets(void) {
    Buffer source = {0};
    f2c_buffer_append(&source, "subroutine many(x,y)\ninteger :: x,y\ny=max(");
    for (size_t i = 0U; i < 1024U; ++i)
        f2c_buffer_append(&source, i == 0U ? "x" : ",x");
    f2c_buffer_append(&source, ")\nend subroutine many\n");
    F2cOptions options = {"many.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source.data, source.length, &options);
    expect(result.error_count == 0U && result.code != NULL,
           "1024 runtime extremum actuals translate within ordinary budgets");
    if (result.code != NULL) {
        size_t depth = 0U, maximum = 0U;
        const char *body = strstr(result.code, "(*y) = ");
        expect(body != NULL, "large extremum has one typed assignment");
        if (body != NULL) {
            for (const char *p = body; *p != '\0' && *p != ';'; ++p) {
                if (*p == '(') {
                    ++depth;
                    if (depth > maximum)
                        maximum = depth;
                } else if (*p == ')' && depth != 0U)
                    --depth;
            }
            expect(maximum < 24U, "large output nesting is logarithmic rather than linear");
        }
    }
    f2c_result_free(&result);
    F2cConfig config = {0};
    config.structure_size = sizeof(config);
    config.limits.max_ast_nodes = 64U;
    F2cInput input = {source.data, source.length, options};
    result = f2c_transpile_project_config(&input, 1U, &config);
    expect(result.code == NULL && result.error_count != 0U && result.diagnostics != NULL &&
               strstr(result.diagnostics, "AST-node limit of 64 exceeded") != NULL,
           "request AST quotas still reject oversized extremum lists without partial C");
    f2c_result_free(&result);
    free(source.data);
}

int main(void) {
    test_compact_binding();
    test_large_binding();
    test_resource_budgets();
    return failures != 0;
}
