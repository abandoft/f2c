#include "ir/call.h"

#include <stdio.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_ordinary_call(void) {
    F2cExpr value = {0};
    F2cExpr *keyword_children[] = {&value};
    F2cExpr keyword = {
        .kind = F2C_EXPR_KEYWORD_ARGUMENT, .children = keyword_children, .child_count = 1U};
    F2cExpr absent = {.kind = F2C_EXPR_INVALID};
    F2cExpr *children[] = {&keyword, &absent};
    F2cExpr call = {.kind = F2C_EXPR_CALL, .children = children, .child_count = 2U};
    expect(f2c_call_parameter_count(&call) == 2U, "ordinary actual count is unchanged");
    expect(f2c_call_parameter_actual(&call, 0U) == &value, "keyword wrappers are unwrapped");
    expect(f2c_call_parameter_actual(&call, 1U) == &absent,
           "absent optional positions are retained");
    expect(f2c_call_child_parameter(&call, 1U) == 1U, "ordinary mapping is the identity");
    expect(f2c_call_parameter_actual(&call, 2U) == NULL &&
               f2c_call_child_parameter(&call, 2U) == SIZE_MAX,
           "out-of-range ordinary positions are rejected");
    expect(f2c_call_passed_object(&call) == NULL, "ordinary calls have no implicit owner");
}

static void test_pass_positions(void) {
    F2cExpr owner = {0};
    F2cExpr first = {0};
    F2cExpr second = {0};
    F2cExpr *binding_children[] = {&owner};
    F2cExpr binding = {.kind = F2C_EXPR_COMPONENT, .children = binding_children, .child_count = 1U};
    F2cExpr *children[] = {&binding, &first, &second};
    Symbol procedure = {.type_bound = 1, .external_parameter_count = 3U};
    F2cExpr call = {
        .kind = F2C_EXPR_CALL, .symbol = &procedure, .children = children, .child_count = 3U};
    for (size_t pass = 0U; pass < 3U; ++pass) {
        size_t explicit_index = 1U;
        procedure.type_bound_pass_index = pass;
        expect(f2c_call_parameter_count(&call) == 3U, "PASS has the complete declared dummy count");
        expect(f2c_call_passed_object(&call) == &owner, "the binding child owns the passed object");
        for (size_t parameter = 0U; parameter < 3U; ++parameter) {
            const F2cExpr *expected = parameter == pass ? &owner : children[explicit_index++];
            expect(f2c_call_parameter_actual(&call, parameter) == expected,
                   "PASS inserts the owner at its declared first, middle or last position");
        }
        for (size_t child = 0U; child < 3U; ++child) {
            const size_t parameter = f2c_call_child_parameter(&call, child);
            const F2cExpr *expected = child == 0U ? &owner : children[child];
            expect(parameter < 3U && f2c_call_parameter_actual(&call, parameter) == expected,
                   "child-to-dummy and dummy-to-actual mappings are exact inverses");
        }
    }
    procedure.type_bound_nopass = 1;
    procedure.external_parameter_count = 2U;
    expect(f2c_call_parameter_count(&call) == 2U &&
               f2c_call_parameter_actual(&call, 0U) == &first &&
               f2c_call_parameter_actual(&call, 1U) == &second,
           "NOPASS excludes the binding designator from the dummy arguments");
    expect(f2c_call_child_parameter(&call, 0U) == SIZE_MAX &&
               f2c_call_child_parameter(&call, 1U) == 0U &&
               f2c_call_child_parameter(&call, 2U) == 1U,
           "NOPASS has no implicit dummy but preserves explicit positions");
}

static void test_invalid_calls(void) {
    F2cExpr invalid = {.kind = F2C_EXPR_INVALID};
    Symbol procedure = {
        .type_bound = 1, .external_parameter_count = 1U, .type_bound_pass_index = 2U};
    F2cExpr *children[] = {&invalid};
    F2cExpr call = {
        .kind = F2C_EXPR_CALL, .symbol = &procedure, .children = children, .child_count = 1U};
    expect(f2c_call_parameter_count(NULL) == 0U && f2c_call_parameter_count(&invalid) == 0U,
           "missing and non-call expressions have no arguments");
    expect(f2c_call_parameter_actual(NULL, 0U) == NULL &&
               f2c_call_child_parameter(NULL, 0U) == SIZE_MAX,
           "missing calls are safely rejected");
    expect(f2c_call_passed_object(&call) == NULL && f2c_call_child_parameter(&call, 0U) == SIZE_MAX,
           "malformed binding owners and out-of-range PASS positions are rejected");
    call.children = NULL;
    expect(f2c_call_passed_object(&call) == NULL && f2c_call_parameter_actual(&call, 0U) == NULL,
           "missing child storage is safely rejected");
}

int main(void) {
    test_ordinary_call();
    test_pass_positions();
    test_invalid_calls();
    return failures != 0;
}
