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

static void test_array_results_in_control_expressions(void) {
    static const char source[] = "module control_results\n"
                                 "contains\n"
                                 "  function values(offset) result(output)\n"
                                 "    integer, intent(in) :: offset\n"
                                 "    integer, allocatable :: output(:)\n"
                                 "    allocate(output(2))\n"
                                 "    output = [offset, offset + 1]\n"
                                 "  end function values\n"
                                 "  subroutine choose(*, *)\n"
                                 "    return sum(values(0))\n"
                                 "  end subroutine choose\n"
                                 "end module control_results\n"
                                 "program control_user\n"
                                 "  use control_results\n"
                                 "  integer :: value\n"
                                 "  value = 0\n"
                                 "  if (all(values(value) < 2)) then\n"
                                 "    value = value + 1\n"
                                 "  else if (any(values(value) == 3)) then\n"
                                 "    value = value + 2\n"
                                 "  end if\n"
                                 "  if (all(values(value) < 4)) value = value + 1\n"
                                 "  do while (all(values(value) < 5))\n"
                                 "    value = value + 1\n"
                                 "  end do\n"
                                 "  do value = sum(values(0)), sum(values(1)), sum(values(0))\n"
                                 "    if (value > 3) exit\n"
                                 "  end do\n"
                                 "  go to (40, 50) sum(values(0))\n"
                                 "  call choose(*60, *70)\n"
                                 "  select case (sum(values(value)))\n"
                                 "  case (9)\n"
                                 "    value = value + 1\n"
                                 "  case default\n"
                                 "    value = 0\n"
                                 "  end select\n"
                                 "  if (sum(values(value)) - 9) 10, 20, 30\n"
                                 "10 continue\n"
                                 "20 continue\n"
                                 "30 continue\n"
                                 "40 continue\n"
                                 "50 continue\n"
                                 "60 continue\n"
                                 "70 continue\n"
                                 "  if (value < 0) stop sum(values(0))\n"
                                 "end program control_user\n";
    F2cOptions options = {"control-expression.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    const char *first_condition;
    const char *first_cleanup;
    expect(result.error_count == 0U && result.code != NULL,
           "array-valued function results lower in typed control expressions");
    expect_contains(result.code, "bool f2c_condition_",
                    "materialized IF conditions are stored before branch selection");
    expect_contains(result.code, "} else {\n",
                    "materialized ELSE IF preserves conditional short-circuiting");
    expect_contains(result.code, "for (;;) {\n",
                    "materialized DO WHILE conditions are reevaluated inside the loop");
    expect_contains(result.code, "if (!f2c_condition_",
                    "materialized DO WHILE exits through an explicit condition guard");
    expect_contains(result.code, "f2c_array_do_start_function_",
                    "counted DO start expressions use the shared array temporary pipeline");
    expect_contains(result.code, "f2c_array_do_limit_function_",
                    "counted DO limit expressions are evaluated once before loop entry");
    expect_contains(result.code, "f2c_array_do_step_function_",
                    "counted DO step expressions are evaluated once before loop entry");
    expect_contains(result.code, "f2c_array_select_function_",
                    "SELECT CASE selectors use the shared array temporary pipeline");
    expect_contains(result.code, "f2c_array_goto_function_",
                    "computed GOTO selectors use the shared array temporary pipeline");
    expect_contains(result.code, "f2c_array_return_function_",
                    "alternate RETURN selectors use the shared array temporary pipeline");
    expect_contains(result.code, "f2c_array_stop_function_",
                    "STOP codes use the shared array temporary pipeline");
    expect_contains(result.code, "f2c_arithmetic_if_value_",
                    "arithmetic IF stores its materialized scalar selector");
    first_condition = result.code != NULL ? strstr(result.code, "bool f2c_condition_") : NULL;
    first_cleanup =
        first_condition != NULL ? strstr(first_condition, "free(f2c_array_condition_") : NULL;
    expect(first_cleanup != NULL,
           "owned array results are released in the condition evaluation scope");
    expect(first_cleanup != NULL && strstr(first_cleanup, "if (f2c_condition_") != NULL &&
               first_cleanup < strstr(first_cleanup, "if (f2c_condition_"),
           "condition temporaries are released after scalar evaluation and before branching");
    f2c_result_free(&result);
}

int main(void) {
    test_array_results_in_control_expressions();
    if (failures != 0)
        fprintf(stderr, "%d control-expression test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
