#include "codegen/lowering/private.h"
#include "codegen/result/retention.h"
#include "semantic/semantic.h"

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

static void test_runtime_instances(void) {
    Context context = {0};
    Unit unit = {0};
    Symbol procedure = {0};
    F2cExpr result = {0};
    F2cExpr constructor = {0};
    F2cExpr *children[] = {&result};
    F2cStatement statement = {0};
    F2cResultRetentionScope scope = {0};
    F2cArrayCleanupList cleanup = {0};
    Buffer output = {0};
    Buffer release = {0};
    procedure.external_result_allocatable = 1;
    result.kind = F2C_EXPR_CALL;
    result.type = TYPE_INTEGER;
    result.type_kind = 4;
    result.symbol = &procedure;
    f2c_bind_expression_result(&result);
    constructor.kind = F2C_EXPR_ARRAY_CONSTRUCTOR;
    constructor.type = TYPE_INTEGER;
    constructor.type_kind = 4;
    constructor.rank = 1U;
    constructor.children = children;
    constructor.child_count = 1U;
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &constructor;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_plan_expression_lifetimes(&context, &unit), "retain planned constructor results");
    expect(f2c_result_retention_begin(&scope, &unit, &constructor, "values", &output, 0),
           "declare a retention scope outside implied-DO evaluations");
    expect(scope.active && output.data != NULL && strstr(output.data, "retained_inline[8]") != NULL,
           "small constructors retain metadata without allocating it on the heap");
    expect(f2c_lowering_copy_code(&unit, &result, "(*owned_value)") &&
               f2c_lowering_copy_owned_storage(&unit, &result, "owned_value") &&
               f2c_array_cleanup_append(&unit, &cleanup, &result, 0),
           "capture a proved owning scalar result");
    expect(f2c_result_retention_capture(&scope, &cleanup, &output, 1) &&
               f2c_result_retention_capture(&scope, &cleanup, &output, 1),
           "two evaluations of one IR node retain two independent storage instances");
    expect(scope.count == 1U && output.data != NULL &&
               strstr(output.data, "{owned_value, 1U, 0U}") != NULL,
           "the release-policy catalog is unique but runtime captures are not deduplicated");
    expect(f2c_result_retention_release(&scope, &release, 0) && release.data != NULL &&
               strstr(release.data, "--values_retained_count") != NULL &&
               strstr(release.data, "free(values_value.data)") != NULL &&
               strstr(release.data, "free((*owned_value))") == NULL,
           "release actual captured storage in reverse evaluation order");
    result.temporary_ownership_analyzed = 0;
    expect(!f2c_result_retention_capture(&scope, &cleanup, &output, 1),
           "reject a result whose ownership proof has been lost");
    result.temporary_ownership_analyzed = 1;
    F2cResultRetentionScope *transferred = (F2cResultRetentionScope *)malloc(sizeof(*transferred));
    expect(transferred != NULL, "allocate an independently owned cleanup scope");
    if (transferred != NULL) {
        *transferred = scope;
        memset(&scope, 0, sizeof(scope));
    }
    expect(f2c_lowering_copy_code(&unit, &constructor, "values") &&
               f2c_lowering_copy_extent(&unit, &constructor, "value_count") &&
               f2c_array_cleanup_append(&unit, &cleanup, &constructor, 0) &&
               f2c_array_cleanup_take_retention(&cleanup, &constructor, transferred),
           "attach deferred instance release to the constructor's proved cleanup action");
    expect(!f2c_array_cleanup_take_retention(&cleanup, &result, NULL),
           "reject retention code attached to the wrong ownership domain");
    f2c_result_retention_clear(&scope);
    f2c_array_cleanup_clear(&cleanup);
    f2c_lowering_clear(&context);
    free(output.data);
    free(release.data);
    free(unit.owned_temporaries);
    free(statement.temporary_plan.owned_temporaries);
    free(context.diagnostics.data);
}

static void test_borrowed_scalar(void) {
    Context context = {0};
    Unit unit = {0};
    F2cExpr result = {0};
    F2cExpr constructor = {0};
    F2cExpr *children[] = {&result};
    F2cResultRetentionScope scope = {0};
    F2cArrayCleanupList cleanup = {0};
    Buffer output = {0};
    unit.context = &context;
    result.kind = F2C_EXPR_CALL;
    result.type = TYPE_INTEGER;
    result.result_kind = F2C_FUNCTION_RESULT_POINTER;
    result.owned_temporary_kind = F2C_OWNED_TEMPORARY_FUNCTION_RESULT;
    constructor.kind = F2C_EXPR_ARRAY_CONSTRUCTOR;
    constructor.children = children;
    constructor.child_count = 1U;
    expect(f2c_result_retention_begin(&scope, &unit, &constructor, "borrowed", &output, 0) &&
               !scope.active && output.length == 0U &&
               f2c_result_retention_capture(&scope, &cleanup, &output, 0) &&
               f2c_result_retention_release(&scope, &output, 0) && output.length == 0U,
           "a scalar pointer value uses a stack snapshot without retaining or freeing its target");
    f2c_result_retention_clear(&scope);
    free(output.data);
}

static void test_derived_release_policy(void) {
    F2cDerivedType derived = {0};
    F2cExpr expression = {0};
    expression.type = TYPE_DERIVED;
    expression.derived_type = &derived;
    expression.rank = 1U;
    expression.kind = F2C_EXPR_ARRAY_CONSTRUCTOR;
    expect(f2c_expression_temporary_release_kind(&expression) == F2C_TEMPORARY_DISCARD_SNAPSHOT,
           "constructor deep copies do not create additional Fortran FINAL events");
    expression.kind = F2C_EXPR_CALL;
    expression.result_kind = F2C_FUNCTION_RESULT_ALLOCATABLE;
    expect(f2c_expression_temporary_release_kind(&expression) == F2C_TEMPORARY_FINALIZE_VALUE,
           "an owning nonpointer function result still requires language finalization");
    expression.result_kind = F2C_FUNCTION_RESULT_POINTER;
    expect(f2c_expression_temporary_release_kind(&expression) == F2C_TEMPORARY_DISCARD_SNAPSHOT,
           "a borrowed derived result value releases only its compiler-owned copy");
}

static void test_statement_roots(void) {
    Context context = {0};
    Unit unit = {0};
    F2cExpr result = {0};
    F2cExpr *roots[] = {NULL, &result};
    F2cResultRetentionScope scope = {0};
    Buffer output = {0};
    unit.context = &context;
    result.kind = F2C_EXPR_CALL;
    result.type = TYPE_INTEGER;
    result.result_kind = F2C_FUNCTION_RESULT_ALLOCATABLE;
    result.owned_temporary_kind = F2C_OWNED_TEMPORARY_FUNCTION_RESULT;
    expect(f2c_result_retention_begin(&scope, &unit, &result, "constructor", &output, 0) &&
               !scope.active && output.length == 0U,
           "constructor retention excludes its independently managed root value");
    f2c_result_retention_clear(&scope);
    expect(f2c_result_retention_begin_values(&scope, &unit, roots, 2U, "statement", &output, 0) &&
               scope.active && output.data != NULL,
           "an action retains owning root expressions as well as nested values");
    f2c_result_retention_clear(&scope);
    free(output.data);
    output = (Buffer){0};
    expect(!f2c_result_retention_begin_values(&scope, &unit, NULL, 1U, "invalid", &output, 0),
           "reject missing statement root storage");
    expect(f2c_result_retention_begin_values(&scope, &unit, NULL, 0U, "empty", &output, 0) &&
               !scope.active && output.length == 0U,
           "empty actions need no runtime retention frame");
    f2c_result_retention_clear(&scope);
    free(output.data);
}

int main(void) {
    test_runtime_instances();
    test_borrowed_scalar();
    test_derived_release_policy();
    test_statement_roots();
    return failures == 0 ? 0 : 1;
}
