#include "f2c/f2c.h"

#include <stdio.h>
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

static size_t occurrence_count(const char *text, const char *needle) {
    size_t count = 0U;
    size_t length;
    if (text == NULL || needle == NULL || needle[0] == '\0')
        return 0U;
    length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL) {
        ++count;
        text += length;
    }
    return count;
}

static F2cResult transpile(const char *source, const char *name) {
    F2cOptions options = {name, F2C_SOURCE_FREE, 0};
    return f2c_transpile(source, strlen(source), &options);
}

static void test_branch_merge_and_duplicate_targets(void) {
    static const char source[] = "subroutine route(flag, value)\n"
                                 "  implicit none\n"
                                 "  logical, intent(in) :: flag\n"
                                 "  integer, intent(out) :: value\n"
                                 "  integer :: target\n"
                                 "  if (flag) then\n"
                                 "    assign 100 to target\n"
                                 "  else\n"
                                 "    assign 200 to target\n"
                                 "  end if\n"
                                 "  goto target, (100, 100, 200)\n"
                                 "100 value = 1\n"
                                 "  goto 300\n"
                                 "200 value = 2\n"
                                 "300 continue\n"
                                 "end subroutine route\n";
    F2cResult result = transpile(source, "assigned-branch-merge.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "assigned-label definitions from both IF branches merge at the GOTO");
    expect(occurrence_count(result.code, "case 100:") == 1U,
           "duplicate allowed labels produce one canonical C switch case");
    expect(occurrence_count(result.code, "case 200:") == 1U,
           "the second reachable branch label is retained");
    f2c_result_free(&result);
}

static void test_loop_back_edge_resolution(void) {
    static const char source[] = "subroutine loop_route(flag, value)\n"
                                 "  implicit none\n"
                                 "  logical, intent(in) :: flag\n"
                                 "  integer, intent(out) :: value\n"
                                 "  integer :: target, iteration\n"
                                 "  assign 100 to target\n"
                                 "  do iteration = 1, 2\n"
                                 "    if (flag) assign 200 to target\n"
                                 "  end do\n"
                                 "  goto target\n"
                                 "100 value = 1\n"
                                 "  goto 300\n"
                                 "200 value = 2\n"
                                 "300 continue\n"
                                 "end subroutine loop_route\n";
    F2cResult result = transpile(source, "assigned-loop-merge.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "assigned-label dataflow converges across loop back edges");
    expect_contains(result.code, "case 100:", "the pre-loop label reaches the bare assigned GOTO");
    expect_contains(result.code, "case 200:",
                    "the conditionally assigned loop label reaches the bare assigned GOTO");
    f2c_result_free(&result);
}

static void test_unreachable_definition_exclusion(void) {
    static const char source[] = "program unreachable_definition\n"
                                 "  implicit none\n"
                                 "  integer :: target, value\n"
                                 "  assign 100 to target\n"
                                 "  goto target\n"
                                 "  assign 200 to target\n"
                                 "200 value = -1\n"
                                 "  goto 300\n"
                                 "100 value = 1\n"
                                 "300 if (value /= 1) error stop 1\n"
                                 "end program unreachable_definition\n";
    F2cResult result = transpile(source, "assigned-unreachable.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "a definition after an unconditional assigned GOTO does not contaminate its input");
    expect_contains(result.code,
                    "case 100:", "the definition reaching the assigned GOTO is emitted");
    expect(occurrence_count(result.code, "case 200:") == 0U,
           "an unreachable later ASSIGN is excluded from the resolved target set");
    f2c_result_free(&result);
}

static void test_cross_variable_control_flow_refinement(void) {
    static const char source[] = "subroutine cross_variable_refinement()\n"
                                 "  implicit none\n"
                                 "  integer :: first, second\n"
                                 "  goto 10\n"
                                 "  assign 200 to first\n"
                                 "10 assign 100 to first\n"
                                 "  goto first\n"
                                 "200 assign 300 to second\n"
                                 "100 goto second\n"
                                 "300 continue\n"
                                 "end subroutine cross_variable_refinement\n";
    F2cResult result = transpile(source, "assigned-cross-variable-refinement.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "resolved assigned-GOTO edges refine reachability for every label variable");
    expect_contains(result.diagnostics,
                    "assigned GOTO variable 'second' is not defined with a statement label",
                    "an ASSIGN reachable only through a provisional edge cannot define another "
                    "label variable");
    f2c_result_free(&result);
}

static void test_definition_path_diagnostics(void) {
    {
        static const char source[] = "subroutine maybe_unassigned(flag)\n"
                                     "  implicit none\n"
                                     "  logical, intent(in) :: flag\n"
                                     "  integer :: target\n"
                                     "  if (flag) assign 100 to target\n"
                                     "  goto target\n"
                                     "100 continue\n"
                                     "end subroutine maybe_unassigned\n";
        F2cResult result = transpile(source, "assigned-maybe-unassigned.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "a conditionally initialized assigned GOTO variable is rejected");
        expect_contains(result.diagnostics, "not defined with a statement label on every reachable",
                        "the diagnostic identifies the missing reaching definition");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine integer_redefinition()\n"
                                     "  implicit none\n"
                                     "  integer :: target\n"
                                     "  assign 100 to target\n"
                                     "  target = 7\n"
                                     "  goto target\n"
                                     "100 continue\n"
                                     "end subroutine integer_redefinition\n";
        F2cResult result = transpile(source, "assigned-integer-redefinition.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "ordinary INTEGER redefinition kills the statement-label value");
        expect_contains(result.diagnostics, "not defined with a statement label on every reachable",
                        "a killed assigned-label value is diagnosed at its GOTO use");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine disallowed_target(flag)\n"
                                     "  implicit none\n"
                                     "  logical, intent(in) :: flag\n"
                                     "  integer :: target\n"
                                     "  if (flag) then\n"
                                     "    assign 100 to target\n"
                                     "  else\n"
                                     "    assign 200 to target\n"
                                     "  end if\n"
                                     "  goto target, (100)\n"
                                     "100 continue\n"
                                     "200 continue\n"
                                     "end subroutine disallowed_target\n";
        F2cResult result = transpile(source, "assigned-disallowed-target.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "a reaching label outside the assigned GOTO allowed list is rejected");
        expect_contains(result.diagnostics, "may hold label 200 outside its allowed label list",
                        "the disallowed reaching label is named precisely");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine allocation_status_redefinition()\n"
                                     "  implicit none\n"
                                     "  integer :: target\n"
                                     "  integer, allocatable :: values(:)\n"
                                     "  assign 100 to target\n"
                                     "  allocate(values(1), stat=target)\n"
                                     "  goto target\n"
                                     "100 continue\n"
                                     "end subroutine allocation_status_redefinition\n";
        F2cResult result = transpile(source, "assigned-allocation-status.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "ALLOCATE STAT= kills a previously assigned statement-label value");
        expect_contains(result.diagnostics, "not defined with a statement label on every reachable",
                        "definable allocation status outputs participate in label dataflow");
        f2c_result_free(&result);
    }
}

static void test_assigned_label_reference_restriction(void) {
    static const char source[] = "subroutine invalid_label_reference(value)\n"
                                 "  implicit none\n"
                                 "  integer, intent(out) :: value\n"
                                 "  integer :: target\n"
                                 "  assign 100 to target\n"
                                 "  value = target\n"
                                 "100 continue\n"
                                 "end subroutine invalid_label_reference\n";
    F2cResult result = transpile(source, "assigned-illegal-reference.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "a statement-label value cannot be read as an ordinary INTEGER");
    expect_contains(result.diagnostics,
                    "may only be referenced by assigned GOTO or as an assigned FORMAT",
                    "illegal assigned-label use reports the permitted contexts");
    f2c_result_free(&result);
}

static void test_assigned_format_dataflow(void) {
    {
        static const char source[] = "subroutine print_route(flag, value)\n"
                                     "  implicit none\n"
                                     "  logical, intent(in) :: flag\n"
                                     "  integer, intent(in) :: value\n"
                                     "  integer :: fmt\n"
                                     "  if (flag) then\n"
                                     "    assign 100 to fmt\n"
                                     "  else\n"
                                     "    assign 200 to fmt\n"
                                     "  end if\n"
                                     "  print fmt, value\n"
                                     "100 format('A', I2)\n"
                                     "200 format('B', I2)\n"
                                     "end subroutine print_route\n";
        F2cResult result = transpile(source, "assigned-format-flow.f90");
        expect(result.error_count == 0U && result.code != NULL,
               "assigned FORMAT definitions merge through the same CFG dataflow");
        expect_contains(result.code, "case 100: f2c_io_format_program",
                        "the first reachable FORMAT program is selected explicitly");
        expect_contains(result.code, "case 200: f2c_io_format_program",
                        "the second reachable FORMAT program is selected explicitly");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine executable_format(value)\n"
                                     "  implicit none\n"
                                     "  integer, intent(in) :: value\n"
                                     "  integer :: fmt\n"
                                     "  assign 100 to fmt\n"
                                     "  print fmt, value\n"
                                     "100 continue\n"
                                     "end subroutine executable_format\n";
        F2cResult result = transpile(source, "assigned-format-executable-label.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "an executable statement label cannot be consumed as an assigned FORMAT");
        expect_contains(result.diagnostics, "may hold executable label 100",
                        "assigned FORMAT diagnostics identify an executable reaching label");
        f2c_result_free(&result);
    }
}

static void test_default_integer_and_assign_target_constraints(void) {
    {
        static const char source[] = "subroutine wide_assigned_goto()\n"
                                     "  implicit none\n"
                                     "  integer(kind=8) :: target\n"
                                     "  assign 100 to target\n"
                                     "  goto target\n"
                                     "100 continue\n"
                                     "end subroutine wide_assigned_goto\n";
        F2cResult result = transpile(source, "assigned-wide-kind.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "ASSIGN and assigned GOTO reject nondefault INTEGER kind");
        expect_contains(result.diagnostics, "target must have default INTEGER kind",
                        "the legacy assigned-label ABI constraint is explicit");
        f2c_result_free(&result);
    }
    {
        static const char source[] = "subroutine invalid_assign_target()\n"
                                     "  implicit none\n"
                                     "  integer :: target, value\n"
                                     "  assign 100 to target\n"
                                     "100 data value /1/\n"
                                     "end subroutine invalid_assign_target\n";
        F2cResult result = transpile(source, "assigned-data-label.f90");
        expect(result.code == NULL && result.error_count != 0U,
               "ASSIGN rejects a label that is neither executable nor FORMAT");
        expect_contains(result.diagnostics,
                        "does not identify an executable branch target or FORMAT statement",
                        "invalid ASSIGN target classification is diagnosed before code generation");
        f2c_result_free(&result);
    }
}

static void test_resolved_branch_construct_entry(void) {
    static const char source[] = "subroutine invalid_construct_entry(flag)\n"
                                 "  implicit none\n"
                                 "  logical, intent(in) :: flag\n"
                                 "  integer :: target\n"
                                 "  assign 100 to target\n"
                                 "  goto target\n"
                                 "  if (flag) then\n"
                                 "100 continue\n"
                                 "  end if\n"
                                 "end subroutine invalid_construct_entry\n";
    F2cResult result = transpile(source, "assigned-construct-entry.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "a bare assigned GOTO cannot enter a structured construct");
    expect_contains(result.diagnostics, "assigned GOTO target label 100 illegally enters",
                    "resolved bare targets receive the same construct-entry validation as "
                    "explicit branches");
    f2c_result_free(&result);
}

int main(void) {
    test_branch_merge_and_duplicate_targets();
    test_loop_back_edge_resolution();
    test_unreachable_definition_exclusion();
    test_cross_variable_control_flow_refinement();
    test_definition_path_diagnostics();
    test_assigned_label_reference_restriction();
    test_assigned_format_dataflow();
    test_default_integer_and_assign_target_constraints();
    test_resolved_branch_construct_entry();
    if (failures != 0)
        fprintf(stderr, "%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
