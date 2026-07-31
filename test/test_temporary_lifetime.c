#include "semantic/semantic.h"

#include "codegen/codegen.h"
#include "internal/context.h"
#include "ir/statement.h"

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

int main(void) {
    test_character_temporary_plan();
    test_ordered_call_plan();
    test_statement_function_plan();
    test_emitter_rejects_unplanned_ir();
    if (failures != 0)
        fprintf(stderr, "%d temporary-lifetime test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
