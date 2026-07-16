#include "generated_project.h"
#include <math.h>

int main(void) {
    double value = 3.0;
    project_apply(&value);
    return fabs(value - 1086.0) > 1.0e-12;
}
