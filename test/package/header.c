#include "f2c/f2c.h"

_Static_assert(F2C_VERSION_MAJOR >= 1, "packaged version metadata is available");

F2cOptions f2c_package_header_check(void) {
    const F2cOptions options = {"package-check.f90", F2C_SOURCE_AUTO, 0};
    return options;
}
