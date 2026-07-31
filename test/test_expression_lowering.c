#include "ast/internal.h"
#include "codegen/lowering/private.h"

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

static Unit test_unit(Context *context) {
    Unit unit;
    memset(&unit, 0, sizeof(unit));
    unit.context = context;
    return unit;
}

static void test_values_and_replacement(void) {
    Context context;
    Unit unit;
    F2cExpr expression;
    memset(&context, 0, sizeof(context));
    memset(&expression, 0, sizeof(expression));
    unit = test_unit(&context);

    expect(f2c_lowering_code(&unit, &expression) == NULL,
           "an expression starts without code-generation state");
    expect(f2c_lowering_take_code(&unit, &expression, NULL),
           "clearing absent code-generation state succeeds");
    expect(context.expression_lowering == NULL,
           "clearing absent state does not allocate the overlay");
    expect(f2c_lowering_copy_code(&unit, &expression, "first_value"),
           "the overlay accepts emitted code");
    expect(f2c_lowering_copy_extent(&unit, &expression, "17U"),
           "the overlay accepts an emitted extent");
    expect(f2c_lowering_copy_character_length(&unit, &expression, "9U"),
           "the overlay accepts an emitted character length");
    expect(f2c_lowering_set_array_temporary(&unit, &expression, 1),
           "the overlay records array temporary state");
    expect(f2c_lowering_set_argument_materialized(&unit, &expression, 1),
           "the overlay records ordered argument materialization");
    expect(strcmp(f2c_lowering_code(&unit, &expression), "first_value") == 0 &&
               strcmp(f2c_lowering_extent(&unit, &expression), "17U") == 0 &&
               strcmp(f2c_lowering_character_length(&unit, &expression), "9U") == 0,
           "all emitted strings can be retrieved");
    expect(f2c_lowering_is_array_temporary(&unit, &expression) &&
               f2c_lowering_argument_materialized(&unit, &expression),
           "all emitted flags can be retrieved");

    expect(f2c_lowering_copy_code(&unit, &expression, "replacement"),
           "emitted code can be replaced");
    expect(strcmp(f2c_lowering_code(&unit, &expression), "replacement") == 0,
           "replacement discards the previous emitted code");
    expect(f2c_lowering_take_extent(&unit, &expression, NULL),
           "an individual emitted string can be cleared");
    expect(f2c_lowering_extent(&unit, &expression) == NULL,
           "cleared emitted state is no longer visible");
    expect(f2c_lowering_set_array_temporary(&unit, &expression, 0) &&
               !f2c_lowering_is_array_temporary(&unit, &expression),
           "an emitted flag can be cleared");

    f2c_lowering_clear(&context);
    expect(context.expression_lowering == NULL, "clearing the overlay releases its store");
}

static void test_clone_and_forget_tree(void) {
    Context context;
    Unit unit;
    F2cExpr source;
    F2cExpr target;
    F2cExpr child;
    F2cExpr *children[] = {&child};
    const char *source_code;
    const char *target_code;
    memset(&context, 0, sizeof(context));
    memset(&source, 0, sizeof(source));
    memset(&target, 0, sizeof(target));
    memset(&child, 0, sizeof(child));
    unit = test_unit(&context);
    target.children = children;
    target.child_count = 1U;

    expect(f2c_lowering_copy_code(&unit, &source, "source_code") &&
               f2c_lowering_copy_extent(&unit, &source, "source_extent") &&
               f2c_lowering_set_argument_materialized(&unit, &source, 1),
           "source state is available for cloning");
    expect(f2c_lowering_clone(&unit, &target, &source), "code-generation state can be cloned");
    source_code = f2c_lowering_code(&unit, &source);
    target_code = f2c_lowering_code(&unit, &target);
    expect(source_code != NULL && target_code != NULL && source_code != target_code &&
               strcmp(source_code, target_code) == 0,
           "cloned strings have independent ownership");
    expect(f2c_lowering_argument_materialized(&unit, &target), "cloning preserves emitted flags");
    expect(f2c_lowering_clone(&unit, &source, &source),
           "self-cloning preserves the existing state");
    expect(strcmp(f2c_lowering_code(&unit, &source), "source_code") == 0,
           "self-cloning does not erase emitted code");

    expect(f2c_lowering_copy_code(&unit, &child, "child_code"),
           "child state is available before tree cleanup");
    f2c_lowering_forget_tree(&unit, &target);
    expect(f2c_lowering_code(&unit, &target) == NULL && f2c_lowering_code(&unit, &child) == NULL,
           "tree cleanup removes parent and descendant state");
    expect(strcmp(f2c_lowering_code(&unit, &source), "source_code") == 0,
           "tree cleanup leaves unrelated state intact");
    f2c_lowering_clear(&context);
}

static void test_growth_and_tombstone_reuse(void) {
    enum { EXPRESSION_COUNT = 1024 };
    Context context;
    Unit unit;
    F2cExpr *expressions;
    size_t index;
    memset(&context, 0, sizeof(context));
    unit = test_unit(&context);
    expressions = (F2cExpr *)calloc(EXPRESSION_COUNT, sizeof(*expressions));
    expect(expressions != NULL, "the stress fixture can be allocated");
    if (expressions == NULL)
        return;

    for (index = 0U; index < EXPRESSION_COUNT; ++index) {
        char code[32];
        (void)snprintf(code, sizeof(code), "value_%zu", index);
        expect(f2c_lowering_copy_code(&unit, &expressions[index], code),
               "the overlay grows without losing entries");
    }
    for (index = 0U; index < EXPRESSION_COUNT; index += 2U)
        f2c_lowering_forget(&unit, &expressions[index]);
    for (index = 0U; index < EXPRESSION_COUNT; index += 2U) {
        char code[32];
        (void)snprintf(code, sizeof(code), "replacement_%zu", index);
        expect(f2c_lowering_copy_code(&unit, &expressions[index], code),
               "tombstone slots can be reused");
    }
    for (index = 0U; index < EXPRESSION_COUNT; ++index) {
        char expected[32];
        if ((index & 1U) == 0U)
            (void)snprintf(expected, sizeof(expected), "replacement_%zu", index);
        else
            (void)snprintf(expected, sizeof(expected), "value_%zu", index);
        expect(f2c_lowering_code(&unit, &expressions[index]) != NULL &&
                   strcmp(f2c_lowering_code(&unit, &expressions[index]), expected) == 0,
               "growth and deletion preserve the exact expression mapping");
    }

    f2c_lowering_clear(&context);
    free(expressions);
}

static void test_clone_across_growth(void) {
    Context context;
    Unit unit;
    F2cExpr expressions[9];
    size_t index;
    memset(&context, 0, sizeof(context));
    memset(expressions, 0, sizeof(expressions));
    unit = test_unit(&context);
    expect(f2c_lowering_copy_code(&unit, &expressions[0], "growth_source") &&
               f2c_lowering_set_array_temporary(&unit, &expressions[0], 1) &&
               f2c_lowering_set_argument_materialized(&unit, &expressions[0], 1),
           "the growth clone source is initialized");
    for (index = 1U; index < 8U; ++index)
        expect(f2c_lowering_copy_code(&unit, &expressions[index], "occupied"),
               "the initial overlay table reaches its growth threshold");
    expect(f2c_lowering_clone(&unit, &expressions[8], &expressions[0]),
           "cloning remains valid when target insertion grows the overlay");
    expect(f2c_lowering_code(&unit, &expressions[8]) != NULL &&
               strcmp(f2c_lowering_code(&unit, &expressions[8]), "growth_source") == 0 &&
               f2c_lowering_is_array_temporary(&unit, &expressions[8]) &&
               f2c_lowering_argument_materialized(&unit, &expressions[8]),
           "growth-safe cloning preserves strings and flags");
    f2c_lowering_clear(&context);
}

static void test_codegen_expression_free(void) {
    Context context;
    Unit unit;
    F2cExpr *parent;
    F2cExpr *child;
    int constructed;
    memset(&context, 0, sizeof(context));
    unit = test_unit(&context);
    parent = f2c_expr_new(F2C_EXPR_BINARY, TYPE_INTEGER, NULL, 0U);
    child = f2c_expr_new_integer_constant(1);
    constructed = parent != NULL && child != NULL && f2c_expr_push(parent, child);
    expect(constructed, "the owned expression tree can be constructed");
    if (!constructed) {
        f2c_expr_free(parent);
        if (parent == NULL || parent->child_count == 0U)
            f2c_expr_free(child);
        return;
    }
    expect(f2c_lowering_copy_code(&unit, parent, "parent") &&
               f2c_lowering_copy_code(&unit, child, "child"),
           "owned expression state is recorded");
    f2c_codegen_expression_free(&unit, parent);
    f2c_lowering_clear(&context);
    expect(context.expression_lowering == NULL,
           "expression destruction and context cleanup compose safely");
}

int main(void) {
    test_values_and_replacement();
    test_clone_and_forget_tree();
    test_growth_and_tombstone_reuse();
    test_clone_across_growth();
    test_codegen_expression_free();
    if (failures != 0)
        fprintf(stderr, "%d expression lowering test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
