#include "semantic/semantic.h"

#include "codegen/array/private.h"
#include "codegen/codegen.h"
#include "codegen/expression/private.h"
#include "codegen/lowering/private.h"
#include "internal/context.h"
#include "ir/statement.h"
#include "semantic/data_flow.h"

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

static void test_character_temporary_plan(void) {
    F2cExpr left;
    F2cExpr right;
    F2cExpr concatenation;
    F2cExpr *children[2];
    F2cStatement statement;
    Context context;
    Unit unit;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&concatenation, 0, sizeof(concatenation));
    memset(&statement, 0, sizeof(statement));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    left.kind = F2C_EXPR_STRING_LITERAL;
    left.type = TYPE_CHARACTER;
    right.kind = F2C_EXPR_STRING_LITERAL;
    right.type = TYPE_CHARACTER;
    concatenation.kind = F2C_EXPR_BINARY;
    concatenation.type = TYPE_CHARACTER;
    concatenation.text = (char *)"//";
    concatenation.children = children;
    concatenation.child_count = 2U;
    children[0] = &left;
    children[1] = &right;
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &concatenation;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_plan_expression_lifetimes(&context, &unit),
           "semantic planning accepts a typed character expression");
    expect(unit.expression_lifetimes_analyzed && unit.expression_temporary_count == 1U,
           "the unit owns one planned character temporary");
    expect(concatenation.temporary_index == 0U && concatenation.temporary_lifetime_analyzed,
           "the concatenation receives a semantic temporary index");
    expect(left.temporary_lifetime_analyzed && right.temporary_lifetime_analyzed,
           "every expression node receives a lifetime proof");
    expect(concatenation.lifetime_statement_index == 0U,
           "the temporary is owned by its typed statement");
}

static void test_ordered_call_plan(void) {
    F2cExpr first_call;
    F2cExpr second_call;
    F2cExpr addition;
    F2cExpr *children[2];
    F2cStatement statement;
    Context context;
    Unit unit;
    memset(&first_call, 0, sizeof(first_call));
    memset(&second_call, 0, sizeof(second_call));
    memset(&addition, 0, sizeof(addition));
    memset(&statement, 0, sizeof(statement));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    first_call.kind = F2C_EXPR_CALL;
    first_call.type = TYPE_INTEGER;
    first_call.text = (char *)"first";
    second_call.kind = F2C_EXPR_CALL;
    second_call.type = TYPE_INTEGER;
    second_call.text = (char *)"second";
    addition.kind = F2C_EXPR_BINARY;
    addition.type = TYPE_INTEGER;
    addition.text = (char *)"+";
    addition.children = children;
    addition.child_count = 2U;
    children[0] = &first_call;
    children[1] = &second_call;
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &addition;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_plan_expression_lifetimes(&context, &unit),
           "semantic planning accepts order-sensitive calls");
    expect(first_call.has_order_sensitive_call && second_call.has_order_sensitive_call,
           "user calls carry an explicit order-sensitive effect");
    expect(addition.ordered_temporary_index == 0U && unit.expression_temporary_count == 1U,
           "the binary expression materializes its first ordered operand");
}

static void test_statement_function_plan(void) {
    Symbol function;
    F2cExpr definition_body;
    F2cExpr invocation;
    F2cStatement statements[2];
    Context context;
    Unit unit;
    memset(&function, 0, sizeof(function));
    memset(&definition_body, 0, sizeof(definition_body));
    memset(&invocation, 0, sizeof(invocation));
    memset(statements, 0, sizeof(statements));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    function.name = (char *)"evaluate";
    function.statement_function = 1;
    function.statement_function_line = 10U;
    definition_body.kind = F2C_EXPR_INTEGER_LITERAL;
    invocation.kind = F2C_EXPR_CALL;
    invocation.type = TYPE_INTEGER;
    invocation.text = function.name;
    invocation.symbol = &function;
    statements[0].kind = F2C_STMT_ASSIGNMENT;
    statements[0].line = 10U;
    statements[0].right = &definition_body;
    statements[1].kind = F2C_STMT_ASSIGNMENT;
    statements[1].line = 20U;
    statements[1].right = &invocation;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.symbols = &function;
    unit.symbol_count = 1U;
    unit.statements = statements;
    unit.statement_count = 2U;
    expect(f2c_plan_expression_lifetimes(&context, &unit),
           "semantic planning accepts a statement-function invocation");
    expect(!definition_body.temporary_lifetime_analyzed,
           "the statement-function definition is not emitted as an action statement");
    expect(invocation.statement_temporary_index == 0U &&
               unit.statement_function_temporary_count == 1U,
           "the invocation receives its statement-function expansion storage");
}

static void test_emitter_rejects_unplanned_ir(void) {
    Line line;
    Context context;
    Unit unit;
    memset(&line, 0, sizeof(line));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    line.number = 1U;
    context.lines.items = &line;
    context.lines.count = 1U;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.kind = UNIT_PROGRAM;
    unit.begin = 0U;
    f2c_emit_unit(&context, &unit);
    expect(context.result.error_count != 0U,
           "the emitter rejects typed IR without a semantic lifetime proof");
    expect(context.output.data == NULL,
           "rejected typed IR does not produce a partial program unit");
    free(context.output.data);
    free(context.diagnostics.data);
}

static void test_owned_array_temporary_flow(void) {
    F2cExpr first;
    F2cExpr second;
    F2cStatement statement;
    F2cArrayCleanupList cleanup;
    Buffer output;
    Context context;
    Unit unit;
    const char *second_cleanup;
    const char *first_cleanup;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&statement, 0, sizeof(statement));
    memset(&cleanup, 0, sizeof(cleanup));
    memset(&output, 0, sizeof(output));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    first.kind = F2C_EXPR_CALL;
    first.intrinsic = F2C_INTRINSIC_RESHAPE;
    first.type = TYPE_INTEGER;
    first.type_kind = f2c_default_kind(TYPE_INTEGER);
    first.rank = 1U;
    second = first;
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &first;
    statement.limit = &second;
    unit.context = &context;
    unit.kind = UNIT_SUBROUTINE;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_lowering_copy_code(&unit, &first, "first_owned_array") &&
               f2c_lowering_copy_extent(&unit, &first, "2U") &&
               f2c_lowering_copy_code(&unit, &second, "second_owned_array") &&
               f2c_lowering_copy_extent(&unit, &second, "3U"),
           "the test fixture records code-generation ownership state");
    expect(f2c_plan_expression_lifetimes(&context, &unit),
           "semantic planning catalogs owned array expressions");
    expect(unit.owned_temporary_count == 2U && statement.temporary_plan.owned_temporary_count == 2U,
           "the statement owns both transformational results");
    expect(first.owned_temporary_index == 0U && second.owned_temporary_index == 1U &&
               first.owned_temporary_kind == F2C_OWNED_TEMPORARY_TRANSFORMATIONAL_RESULT &&
               second.owned_temporary_kind == F2C_OWNED_TEMPORARY_TRANSFORMATIONAL_RESULT,
           "owned array values receive stable typed-IR identities");
    expect(f2c_analyze_temporary_lifetimes(&context, &unit),
           "owned temporary data flow converges on a typed statement");
    expect(unit.temporary_flow.analyzed &&
               f2c_temporary_flow_is_created(&unit, 0U, first.owned_temporary_index) &&
               f2c_temporary_flow_is_released(&unit, 0U, first.owned_temporary_index) &&
               !f2c_temporary_flow_is_live_out(&unit, 0U, first.owned_temporary_index),
           "a statement-owned array result is created, released, and cannot escape its CFG node");
    expect(f2c_array_cleanup_append(&unit, &cleanup, &first, 1) &&
               f2c_array_cleanup_append(&unit, &cleanup, &second, 1),
           "code generation accepts cleanup actions backed by the semantic catalog");
    expect(!f2c_array_cleanup_append(&unit, &cleanup, &first, 1),
           "the typed cleanup list rejects duplicate ownership actions");
    expect(f2c_array_cleanup_emit(&output, &unit, &cleanup), "typed cleanup actions lower to C");
    second_cleanup = output.data != NULL ? strstr(output.data, "free(second_owned_array);") : NULL;
    first_cleanup = output.data != NULL ? strstr(output.data, "free(first_owned_array);") : NULL;
    expect(second_cleanup != NULL && first_cleanup != NULL && second_cleanup < first_cleanup,
           "owned array values are destroyed in reverse creation order");
    f2c_array_cleanup_clear(&cleanup);
    free(output.data);
    f2c_temporary_flow_clear(&unit);
    free(unit.owned_temporaries);
    free(statement.temporary_plan.owned_temporaries);
    f2c_lowering_clear(&context);
    free(context.diagnostics.data);
}

static void test_flat_constructor_storage(void) {
    Context context = {0};
    Unit unit = {0};
    F2cStatement statement = {0};
    F2cExpr constructor = {0};
    F2cExpr *children[] = {f2c_expr_new_integer_constant(1), f2c_expr_new_integer_constant(2)};
    F2cArrayCleanupList cleanup = {0};
    Buffer prelude = {0};
    size_t temporary = 0U;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    constructor.kind = F2C_EXPR_ARRAY_CONSTRUCTOR;
    constructor.type = TYPE_INTEGER;
    constructor.type_kind = f2c_default_kind(TYPE_INTEGER);
    constructor.rank = 1U;
    constructor.children = children;
    constructor.child_count = 2U;
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &constructor;
    expect(children[0] != NULL && children[1] != NULL &&
               f2c_plan_expression_lifetimes(&context, &unit) &&
               f2c_array_materialize_constructors(&context, &unit, &constructor, 0U, "condition",
                                                  &temporary, &prelude, &cleanup, 0),
           "a flat constructor is lowered from its typed lifetime plan");
    expect(prelude.data != NULL &&
               strstr(prelude.data,
                      "const int32_t f2c_array_condition_constructor_0_0[2] = {1, 2};") != NULL,
           "flat constructor snapshots use explicitly initialized named array storage");
    expect(cleanup.count == 0U,
           "fixed flat constructor storage needs neither heap allocation nor runtime retention");
    f2c_array_cleanup_clear(&cleanup);
    f2c_codegen_expression_free(&unit, children[0]);
    f2c_codegen_expression_free(&unit, children[1]);
    f2c_lowering_clear(&context);
    free(prelude.data);
    free(unit.owned_temporaries);
    free(statement.temporary_plan.owned_temporaries);
    free(context.diagnostics.data);
}

static void test_expression_call_lowering_is_immutable(void) {
    F2cExpr inner;
    F2cExpr outer;
    F2cExpr *children[1];
    F2cStatement statement;
    Context context;
    Unit unit;
    char *code;
    int supported = 1;
    memset(&inner, 0, sizeof(inner));
    memset(&outer, 0, sizeof(outer));
    memset(&statement, 0, sizeof(statement));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    inner.kind = F2C_EXPR_CALL;
    inner.type = TYPE_INTEGER;
    inner.type_kind = f2c_default_kind(TYPE_INTEGER);
    inner.text = (char *)"produce";
    outer.kind = F2C_EXPR_CALL;
    outer.type = TYPE_INTEGER;
    outer.type_kind = f2c_default_kind(TYPE_INTEGER);
    outer.text = (char *)"consume";
    outer.children = children;
    outer.child_count = 1U;
    children[0] = &inner;
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &outer;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_plan_expression_lifetimes(&context, &unit),
           "ordered call arguments receive semantic lowering storage");
    expect(inner.ordered_argument_temporary_index != SIZE_MAX,
           "the nested call is selected for ordered argument materialization");
    code = f2c_expression_call(&unit, &outer, &supported);
    expect(supported && code != NULL,
           "an ordered expression call lowers from a private expression clone");
    expect(f2c_lowering_code(&unit, &inner) == NULL &&
               !f2c_lowering_argument_materialized(&unit, &inner),
           "expression lowering leaves the original typed IR unchanged");
    free(code);
    free(unit.owned_temporaries);
    free(statement.temporary_plan.owned_temporaries);
    f2c_lowering_clear(&context);
    free(context.diagnostics.data);
}

static void test_statement_call_lowering_is_immutable(void) {
    Symbol source;
    F2cExpr array;
    F2cExpr conversion;
    F2cExpr lower_bound;
    F2cExpr upper_bound;
    F2cExpr *children[1];
    F2cExpr *arguments[1];
    Buffer output;
    Context context;
    Unit unit;
    memset(&source, 0, sizeof(source));
    memset(&array, 0, sizeof(array));
    memset(&conversion, 0, sizeof(conversion));
    memset(&lower_bound, 0, sizeof(lower_bound));
    memset(&upper_bound, 0, sizeof(upper_bound));
    memset(&output, 0, sizeof(output));
    memset(&context, 0, sizeof(context));
    memset(&unit, 0, sizeof(unit));
    source.name = (char *)"values";
    source.c_name = source.name;
    source.type = TYPE_INTEGER;
    source.kind = f2c_default_kind(TYPE_INTEGER);
    source.rank = 1U;
    source.dimensions[0].lower = (char *)"1";
    source.dimensions[0].upper = (char *)"2";
    lower_bound.kind = F2C_EXPR_INTEGER_LITERAL;
    lower_bound.parse_error_offset = SIZE_MAX;
    lower_bound.type = TYPE_INTEGER;
    lower_bound.type_kind = source.kind;
    lower_bound.text = (char *)"1";
    upper_bound.kind = F2C_EXPR_INTEGER_LITERAL;
    upper_bound.parse_error_offset = SIZE_MAX;
    upper_bound.type = TYPE_INTEGER;
    upper_bound.type_kind = source.kind;
    upper_bound.text = (char *)"2";
    source.dimensions[0].lower_expression = &lower_bound;
    source.dimensions[0].upper_expression = &upper_bound;
    array.kind = F2C_EXPR_NAME;
    array.parse_error_offset = SIZE_MAX;
    array.type = TYPE_INTEGER;
    array.type_kind = source.kind;
    array.rank = 1U;
    array.symbol = &source;
    array.text = source.name;
    conversion.kind = F2C_EXPR_CALL;
    conversion.parse_error_offset = SIZE_MAX;
    conversion.intrinsic = F2C_INTRINSIC_REAL;
    conversion.type = TYPE_REAL;
    conversion.type_kind = f2c_default_kind(TYPE_REAL);
    conversion.rank = 1U;
    conversion.text = (char *)"real";
    conversion.children = children;
    conversion.child_count = 1U;
    children[0] = &array;
    arguments[0] = &conversion;
    unit.context = &context;
    unit.symbols = &source;
    unit.symbol_count = 1U;
    f2c_emit_call(&output, &unit, "consume_array", arguments, 1U, 0);
    expect(output.data != NULL && strstr(output.data, "f2c_array_conversion_0") != NULL,
           "statement call lowering materializes an array conversion");
    expect(f2c_lowering_code(&unit, &conversion) == NULL,
           "statement call lowering leaves the original typed argument tree unchanged");
    free(output.data);
    f2c_lowering_clear(&context);
    free(context.diagnostics.data);
}

static void test_reduction_designator_ownership(void) {
    Context context = {0};
    Unit unit = {0};
    F2cExpr operand = {0};
    F2cExpr actual = {0};
    F2cExpr reduction = {0};
    F2cExpr call = {0};
    F2cExpr *operands[1] = {&operand};
    F2cExpr *actuals[1] = {&actual};
    F2cStatement statements[2] = {0};
    operand.kind = F2C_EXPR_COMPONENT;
    operand.type = TYPE_LOGICAL;
    operand.type_kind = f2c_default_kind(TYPE_LOGICAL);
    operand.rank = 1U;
    operand.definable = 1;
    actual = operand;
    reduction.kind = F2C_EXPR_CALL;
    reduction.type = TYPE_LOGICAL;
    reduction.text = (char *)"all";
    reduction.intrinsic = F2C_INTRINSIC_ALL;
    reduction.children = operands;
    reduction.child_count = 1U;
    call.kind = F2C_EXPR_CALL;
    call.text = (char *)"consume";
    call.children = actuals;
    call.child_count = 1U;
    statements[0].kind = F2C_STMT_ASSIGNMENT;
    statements[0].right = &reduction;
    statements[1].kind = F2C_STMT_CALL;
    statements[1].expression = &call;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = statements;
    unit.statement_count = 2U;
    expect(f2c_plan_expression_lifetimes(&context, &unit),
           "read-only reduction designators receive semantic lifetime plans");
    expect(unit.owned_temporary_count == 1U &&
               operand.owned_temporary_kind == F2C_OWNED_TEMPORARY_ELEMENTAL_ARRAY_VALUE &&
               operand.owned_temporary_index == 0U && operand.lifetime_statement_index == 0U &&
               operand.temporary_ownership_analyzed,
           "the reduction snapshot is a statement-owned typed array value");
    expect(actual.owned_temporary_kind == F2C_OWNED_TEMPORARY_NONE,
           "user procedure designator arguments retain original object identity");
    expect(f2c_plan_expression_lifetimes(&context, &unit) && unit.owned_temporary_count == 1U,
           "replanning reduction lifetimes does not duplicate owned storage");
    free(unit.owned_temporaries);
    free(statements[0].temporary_plan.owned_temporaries);
    free(statements[1].temporary_plan.owned_temporaries);
    free(context.diagnostics.data);
}

static void test_call_lowering_failure_is_atomic(void) {
    Context context = {0};
    Unit unit = {0};
    F2cExpr invalid = {0};
    F2cExpr *arguments[1] = {&invalid};
    Buffer output = {0};
    invalid.kind = F2C_EXPR_INVALID;
    invalid.type = TYPE_UNKNOWN;
    invalid.span.begin.line = 27U;
    invalid.span.begin.column = 8U;
    invalid.span.end = invalid.span.begin;
    unit.context = &context;
    f2c_buffer_append(&output, "/* retained output */\n");
    expect(!f2c_emit_call(&output, &unit, "consume_invalid", arguments, 1U, 0),
           "unsupported actual lowering returns an explicit failure");
    expect(output.data != NULL && strcmp(output.data, "/* retained output */\n") == 0,
           "failed call lowering atomically restores the previous output");
    expect(context.result.error_count == 1U && context.diagnostics.data != NULL &&
               strstr(context.diagnostics.data, "consume_invalid") != NULL,
           "failed call lowering emits a hard diagnostic rather than silently omitting CALL");
    free(output.data);
    free(context.diagnostics.data);
    f2c_lowering_clear(&context);
}

static void test_scalar_result_ownership(void) {
    Context context = {0};
    Unit unit = {0};
    Symbol procedure = {0};
    F2cExpr expression = {0};
    F2cStatement statement = {0};
    F2cArrayCleanupList cleanup = {0};
    Buffer output = {0};
    expression.kind = F2C_EXPR_CALL;
    expression.type = TYPE_INTEGER;
    expression.type_kind = 4;
    expression.symbol = &procedure;
    procedure.external_result_allocatable = 1;
    f2c_bind_expression_result(&expression);
    expect(expression.result_kind == F2C_FUNCTION_RESULT_ALLOCATABLE &&
               f2c_expression_has_descriptor_result(&expression) &&
               f2c_result_kind_owns_storage(expression.result_kind),
           "rank-zero allocatable results retain their typed storage ownership");
    statement.kind = F2C_STMT_ASSIGNMENT;
    statement.right = &expression;
    unit.context = &context;
    unit.phase = F2C_UNIT_TYPED_IR;
    unit.statements = &statement;
    unit.statement_count = 1U;
    expect(f2c_plan_expression_lifetimes(&context, &unit) && unit.owned_temporary_count == 1U &&
               expression.owned_temporary_kind == F2C_OWNED_TEMPORARY_FUNCTION_RESULT,
           "rank-zero result storage participates in the semantic temporary catalog");
    expect(f2c_lowering_copy_code(&unit, &expression, "(*result_storage)") &&
               f2c_lowering_copy_owned_storage(&unit, &expression, "result_storage") &&
               f2c_lowering_copy_result_descriptor(&unit, &expression, "result_descriptor") &&
               f2c_array_cleanup_append(&unit, &cleanup, &expression, 0) &&
               f2c_array_cleanup_emit(&output, &unit, &cleanup),
           "scalar result cleanup is backed by its semantic ownership proof");
    expect(output.data != NULL && strcmp(output.data, "free(result_storage);\n") == 0,
           "cleanup releases storage rather than the dereferenced scalar value");
    F2cExpr *clone = f2c_array_clone_expression(&unit, &expression);
    expect(clone != NULL && clone->result_kind == expression.result_kind &&
               strcmp(f2c_lowering_owned_storage(&unit, clone), "result_storage") == 0 &&
               strcmp(f2c_lowering_result_descriptor(&unit, clone), "result_descriptor") == 0,
           "lowering clones retain descriptor and ownership views separately from typed IR");
    f2c_codegen_expression_free(&unit, clone);
    f2c_array_cleanup_clear(&cleanup);
    free(output.data);
    free(unit.owned_temporaries);
    free(statement.temporary_plan.owned_temporaries);
    f2c_lowering_clear(&context);
    procedure.external_result_allocatable = 0;
    procedure.external_result_pointer = 1;
    f2c_bind_expression_result(&expression);
    expect(expression.result_kind == F2C_FUNCTION_RESULT_POINTER && expression.definable &&
               expression.value_category == F2C_VALUE_VARIABLE &&
               !f2c_result_kind_owns_storage(expression.result_kind),
           "pointer function results designate targets but never own those targets");
    expression.result_use = F2C_FUNCTION_RESULT_REFERENCE;
    expect(f2c_expression_temporary_release_kind(&expression) == F2C_TEMPORARY_BORROWED_REFERENCE,
           "a pointer reference has no owned-storage cleanup action");
    expression.result_use = F2C_FUNCTION_RESULT_METADATA;
    expect(f2c_expression_temporary_release_kind(&expression) == F2C_TEMPORARY_BORROWED_REFERENCE,
           "a pointer MOLD consumes metadata without reading or owning target values");
    Symbol scalar_symbol = {0};
    char *scalar_count = f2c_symbol_element_count(&unit, &scalar_symbol);
    expect(scalar_count != NULL && strcmp(scalar_count, "1U") == 0,
           "rank-zero managed storage has one element for cleanup, not an empty count");
    free(scalar_count);
}

int main(void) {
    test_character_temporary_plan();
    test_ordered_call_plan();
    test_statement_function_plan();
    test_emitter_rejects_unplanned_ir();
    test_owned_array_temporary_flow();
    test_flat_constructor_storage();
    test_expression_call_lowering_is_immutable();
    test_statement_call_lowering_is_immutable();
    test_reduction_designator_ownership();
    test_call_lowering_failure_is_atomic();
    test_scalar_result_ownership();
    if (failures != 0)
        fprintf(stderr, "%d temporary-lifetime test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
