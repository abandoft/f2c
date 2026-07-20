#include "f2c/f2c.h"

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

static void expect_contains(const char *text, const char *needle, const char *message) {
    expect(text != NULL && strstr(text, needle) != NULL, message);
}

static F2cResult transpile(const char *source, const char *name) {
    F2cOptions options = {name, F2C_SOURCE_FREE, 0};
    return f2c_transpile(source, strlen(source), &options);
}

static void test_complete_lowering(void) {
    static const char source[] = "program alternate_call\n"
                                 "  implicit none\n"
                                 "  integer :: value\n"
                                 "  value = 0\n"
                                 "  call route(1, value, *10, *20)\n"
                                 "  error stop 1\n"
                                 "10 value = value + 10\n"
                                 "  goto 30\n"
                                 "20 error stop 2\n"
                                 "30 continue\n"
                                 "end program alternate_call\n"
                                 "subroutine route(mode, value, *, *)\n"
                                 "  implicit none\n"
                                 "  integer, intent(in) :: mode\n"
                                 "  integer, intent(out) :: value\n"
                                 "  value = mode\n"
                                 "  return mode\n"
                                 "end subroutine route\n";
    F2cResult result = transpile(source, "alternate-call.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "alternate-return procedure translates through the complete semantic pipeline");
    expect_contains(result.code, "int32_t route(",
                    "alternate-return subroutine uses an integer selector ABI");
    expect_contains(result.code, "switch (f2c_alternate_return)",
                    "alternate-return CALL dispatches the selector after the call");
    expect_contains(result.code,
                    "case 1:", "the first alternate-return selector is represented explicitly");
    expect_contains(result.code, "goto f2c_label_10;",
                    "the selected alternate return branches to its canonical target label");
    expect_contains(result.code, "return f2c_alternate_return;",
                    "RETURN expression evaluates to the procedure selector");
    f2c_result_free(&result);
}

static void test_return_contract_diagnostics(void) {
    {
        static const char source[] = "subroutine plain()\n"
                                     "  return 1\n"
                                     "end subroutine plain\n";
        F2cResult result = transpile(source, "plain-return.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "RETURN selector outside an alternate-return procedure is rejected");
        expect_contains(result.diagnostics,
                        "alternate RETURN is valid only in a subroutine with alternate-return",
                        "invalid RETURN selector reports its procedure-contract requirement");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine route(*)\n"
                                     "  return 1.5\n"
                                     "end subroutine route\n";
        F2cResult result = transpile(source, "real-return.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "noninteger alternate-return selector is rejected");
        expect_contains(result.diagnostics,
                        "alternate RETURN selector must be a scalar INTEGER expression",
                        "alternate RETURN reports its typed selector contract");
        f2c_result_free(&result);
    }
}

static void test_explicit_interface_binding_diagnostics(void) {
    {
        static const char source[] = "program missing_label\n"
                                     "  call route(1, *10)\n"
                                     "10 continue\n"
                                     "end program missing_label\n"
                                     "subroutine route(value, *, *)\n"
                                     "  integer :: value\n"
                                     "end subroutine route\n";
        F2cResult result = transpile(source, "missing-label.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "a missing alternate actual is rejected");
        expect_contains(result.diagnostics, "has no *label actual specifier",
                        "missing alternate actual identifies the unmatched dummy slot");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "program ordinary_label\n"
                                     "  call route(*10, *20)\n"
                                     "10 continue\n"
                                     "20 continue\n"
                                     "end program ordinary_label\n"
                                     "subroutine route(value, *)\n"
                                     "  integer :: value\n"
                                     "end subroutine route\n";
        F2cResult result = transpile(source, "ordinary-label.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "an alternate specifier cannot bind an ordinary dummy argument");
        expect_contains(result.diagnostics, "cannot be associated with an alternate-return",
                        "ordinary/alternate dummy mismatch is diagnosed structurally");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "program malformed_label\n"
                                     "  call route(*label)\n"
                                     "end program malformed_label\n"
                                     "subroutine route(*)\n"
                                     "end subroutine route\n";
        F2cResult result = transpile(source, "malformed-label.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "a malformed alternate target is rejected");
        expect_contains(result.diagnostics,
                        "alternate return target must be a statement label of one to five digits",
                        "malformed alternate target receives a precise syntax diagnostic");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "program undefined_label\n"
                                     "  call route(*100)\n"
                                     "end program undefined_label\n"
                                     "subroutine route(*)\n"
                                     "end subroutine route\n";
        F2cResult result = transpile(source, "undefined-label.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "an undefined alternate target is rejected by control-flow validation");
        expect_contains(result.diagnostics,
                        "alternate return target label 100 is not defined in this program unit",
                        "alternate target validation is integrated with the branch CFG");
        f2c_result_free(&result);
    }
}

static void test_interface_definition_layout(void) {
    static const char source[] = "program mismatched_interface\n"
                                 "  interface\n"
                                 "    subroutine route(value, *)\n"
                                 "      integer :: value\n"
                                 "    end subroutine route\n"
                                 "  end interface\n"
                                 "  call route(1, *10)\n"
                                 "10 continue\n"
                                 "end program mismatched_interface\n"
                                 "subroutine route(*, value)\n"
                                 "  integer :: value\n"
                                 "end subroutine route\n";
    F2cResult result = transpile(source, "interface-layout.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "explicit interface and project definition must agree on alternate dummy positions");
    expect_contains(result.diagnostics,
                    "incompatible ordinary/alternate-return dummy argument layout",
                    "interface compatibility compares the full dummy layout");
    f2c_result_free(&result);
}

static void test_generic_interface_selection(void) {
    static const char source[] = "program generic_call\n"
                                 "  interface dispatch\n"
                                 "    subroutine route(mode, *, *)\n"
                                 "      integer, intent(in) :: mode\n"
                                 "    end subroutine route\n"
                                 "  end interface dispatch\n"
                                 "  call dispatch(2, *10, *20)\n"
                                 "  error stop 1\n"
                                 "10 error stop 2\n"
                                 "20 continue\n"
                                 "end program generic_call\n"
                                 "subroutine route(mode, *, *)\n"
                                 "  integer, intent(in) :: mode\n"
                                 "  return mode\n"
                                 "end subroutine route\n";
    F2cResult result = transpile(source, "generic-return.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "generic resolution includes alternate-return slots in its operand contract");
    expect_contains(result.code, "route(&(int32_t){2})",
                    "generic alternate-return call resolves to the concrete procedure");
    expect_contains(result.code, "goto f2c_label_20;",
                    "generic alternate-return selector retains the second target");
    f2c_result_free(&result);
}

static void test_procedure_dummy_signature(void) {
    static const char source[] = "program procedure_actual\n"
                                 "  external target\n"
                                 "  call invoke(target)\n"
                                 "end program procedure_actual\n"
                                 "subroutine invoke(callback)\n"
                                 "  abstract interface\n"
                                 "    subroutine callback_contract(*)\n"
                                 "    end subroutine callback_contract\n"
                                 "  end interface\n"
                                 "  procedure(callback_contract) :: callback\n"
                                 "  call callback(*10)\n"
                                 "  error stop 1\n"
                                 "10 continue\n"
                                 "end subroutine invoke\n"
                                 "subroutine target(*)\n"
                                 "  return 1\n"
                                 "end subroutine target\n";
    F2cResult result = transpile(source, "procedure-dummy-return.f90");
    if (result.error_count != 0U && result.diagnostics != NULL)
        fprintf(stderr, "%s", result.diagnostics);
    expect(result.error_count == 0U && result.code != NULL,
           "alternate-return contract propagates through a procedure dummy argument");
    expect_contains(result.code, "int32_t (*callback)(void)",
                    "procedure dummy pointer preserves its integer selector ABI");
    expect_contains(result.code, "const int32_t f2c_alternate_return = (int32_t)callback()",
                    "alternate call through a procedure dummy captures its selector");
    f2c_result_free(&result);
}

static void test_unresolved_external_abi(void) {
    static const char source[] = "program unresolved_call\n"
                                 "  implicit none\n"
                                 "  integer :: value\n"
                                 "  external external_route\n"
                                 "  call external_route(value, *10)\n"
                                 "  goto 20\n"
                                 "10 value = value + 1\n"
                                 "20 continue\n"
                                 "end program unresolved_call\n";
    F2cResult result = transpile(source, "unresolved-call.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "an implicit external alternate-return call records a usable ABI contract");
    expect_contains(result.code, "extern int32_t external_route(",
                    "unresolved alternate-return subroutine receives an integer prototype");
    expect_contains(result.code, "external_route(&value)",
                    "alternate labels are not passed as generated C ABI arguments");
    expect(result.code == NULL || strstr(result.code, "external_route(&value,") == NULL,
           "unresolved external prototype contains only ordinary actual arguments");
    expect_contains(result.code, "goto f2c_label_10;",
                    "unresolved external selector dispatch retains the target branch");
    f2c_result_free(&result);
}

static void test_elemental_restriction(void) {
    static const char source[] = "elemental subroutine route(*)\n"
                                 "end subroutine route\n";
    F2cResult result = transpile(source, "elemental-return.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "ELEMENTAL subroutine cannot expose alternate returns");
    expect_contains(result.diagnostics,
                    "an ELEMENTAL subroutine cannot have alternate-return dummy arguments",
                    "ELEMENTAL restriction is enforced during semantic analysis");
    f2c_result_free(&result);
}

int main(void) {
    test_complete_lowering();
    test_return_contract_diagnostics();
    test_explicit_interface_binding_diagnostics();
    test_interface_definition_layout();
    test_generic_interface_selection();
    test_procedure_dummy_signature();
    test_unresolved_external_abi();
    test_elemental_restriction();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
