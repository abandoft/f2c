#include "f2c/f2c.h"

#include <stdio.h>
#include <string.h>

static int failures;

static const char declarations[] = "module bound_calls\n"
                                   " type :: box\n"
                                   "  integer :: width = 2\n"
                                   " contains\n"
                                   "  procedure, pass(self) :: update => update_box\n"
                                   "  procedure, pass(self) :: make => make_text\n"
                                   " end type\n"
                                   "contains\n"
                                   " subroutine update_box(n,self,extra)\n"
                                   "  integer, intent(inout) :: n\n"
                                   "  class(box), intent(inout) :: self\n"
                                   "  integer, optional, intent(in) :: extra\n"
                                   "  n = n+1\n"
                                   "  if (present(extra)) self%width = extra\n"
                                   " end subroutine\n"
                                   " function make_text(n,self,extra) result(text)\n"
                                   "  integer, intent(inout) :: n\n"
                                   "  class(box), intent(inout) :: self\n"
                                   "  integer, optional, intent(in) :: extra\n"
                                   "  character(n) :: text\n"
                                   "  text = 'x'\n"
                                   "  n = n+1\n"
                                   "  if (present(extra)) self%width = extra\n"
                                   " end function\n"
                                   "end module\n"
                                   "program test_bound_call\n"
                                   " use bound_calls\n"
                                   " implicit none\n"
                                   " type(box) :: object\n"
                                   " integer :: n, extra, values(2)\n"
                                   " integer(kind=8) :: wide\n"
                                   " character(:), allocatable :: text\n";

static void check(const char *action, const char *diagnostic) {
    char source[4096];
    const int written =
        snprintf(source, sizeof(source), "%s %s\nend program\n", declarations, action);
    F2cOptions options = {"bound-call.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    if (written < 0 || (size_t)written >= sizeof(source)) {
        ++failures;
        return;
    }
    result = f2c_transpile(source, (size_t)written, &options);
    if (diagnostic == NULL) {
        if (result.error_count != 0U || result.code == NULL) {
            fprintf(stderr, "FAIL: valid bound call: %s\n%s\n", action,
                    result.diagnostics != NULL ? result.diagnostics : "");
            ++failures;
        }
    } else if (result.code != NULL || result.error_count == 0U || result.diagnostics == NULL ||
               strstr(result.diagnostics, diagnostic) == NULL) {
        fprintf(stderr, "FAIL: bound call must diagnose '%s': %s\n%s\n", diagnostic, action,
                result.diagnostics != NULL ? result.diagnostics : "");
        ++failures;
    }
    f2c_result_free(&result);
}

int main(void) {
    check("text = object%make(extra=extra, n=n)", NULL);
    check("text = object%make(n=n)", NULL);
    check("call object%update(extra=extra, n=n)", NULL);
    check("call object%update(n)", NULL);
    check("text = object%make(missing=n)", "no dummy argument named 'missing'");
    check("call object%update(missing=n)", "no dummy argument named 'missing'");
    check("text = object%make(n=n, n=extra)", "more than once");
    check("text = object%make(n=n, self=object)", "more than once");
    check("text = object%make(n=n, extra)", "positional argument follows a keyword");
    check("text = object%make()", "required dummy argument 'n'");
    check("call object%update()", "required dummy argument 'n'");
    check("text = object%make(n=1)", "is not definable");
    check("text = object%make(n=wide)", "has kind 8");
    check("text = object%make(n=values)", "has rank 1");
    check("text = object%make(n=1.5)", "has type REAL");
    check("call object%make(n)", "cannot be invoked as a SUBROUTINE");
    check("text = object%update(n)", "cannot be invoked as a FUNCTION");
    return failures != 0;
}
