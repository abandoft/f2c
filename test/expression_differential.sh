#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
FC=${FC:-gfortran}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/differential-expressions

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

awk '
function next_random() {
    state = (state * 25173 + 13849) % 65536
    return state
}
function atom() {
    return atoms[1 + next_random() % atom_count]
}
BEGIN {
    state = 23063
    atom_count = 14
    atoms[1] = "x"
    atoms[2] = "y"
    atoms[3] = "(x+y)"
    atoms[4] = "(x-y)"
    atoms[5] = "(x*y)"
    atoms[6] = "(x/(abs(y)+1.0d0))"
    atoms[7] = "sqrt(abs(x)+0.25d0)"
    atoms[8] = "log(abs(y)+1.0d0)"
    atoms[9] = "sin(x)"
    atoms[10] = "cos(y)"
    atoms[11] = "atan(x)"
    atoms[12] = "exp(-abs(x)*0.125d0)"
    atoms[13] = "max(x,y)"
    atoms[14] = "min(x,y)"

    print "subroutine expression_matrix(x, y, output)"
    print "  implicit none"
    print "  double precision, intent(in) :: x, y"
    print "  double precision, intent(out) :: output(256)"
    for (i = 1; i <= 256; ++i) {
        a = atom()
        b = atom()
        c = atom()
        form = next_random() % 10
        if (form == 0)
            expression = "(" a "+" b ")"
        else if (form == 1)
            expression = "(" a "-" b ")"
        else if (form == 2)
            expression = "(" a "*" b ")"
        else if (form == 3)
            expression = "(" a "/(abs(" b ")+0.75d0))"
        else if (form == 4)
            expression = "((" a "+" b ")*(" c "-0.25d0))"
        else if (form == 5)
            expression = "(max(" a "," b ")-min(" b "," c "))"
        else if (form == 6)
            expression = "((" a ")**2+" b ")"
        else if (form == 7)
            expression = "sqrt(abs(" a "*" b ")+0.125d0)"
        else if (form == 8)
            expression = "(sin(" a ")+cos(" b "))"
        else
            expression = "(atan(" a "-" b ")+exp(-abs(" c ")*0.125d0))"
        print "  output(" i ") = " expression
    }
    print "end subroutine expression_matrix"
    print ""
    print "subroutine array_matrix(n, a, b, c)"
    print "  implicit none"
    print "  integer, intent(in) :: n"
    print "  double precision, intent(inout) :: a(n), b(n), c(n)"
    print "  a(2:n) = a(1:n-1)"
    print "  b(1:n:2) = b(n:1:-2)"
    print "  c(2:n-1) = a(1:n-2) + b(3:n)"
    print "  a(1:n:2) = c(n:1:-2) * 0.5d0"
    print "end subroutine array_matrix"
    print ""
    print "subroutine character_matrix(values, source)"
    print "  character(len=4) values(4)"
    print "  character(len=2) source(4)"
    print "  values(2:4) = values(1:3)"
    print "  values(1:4:2) = source(4:1:-2)"
    print "  values(2:4:2) = \"Z\""
    print "end subroutine character_matrix"
    print ""
    print "subroutine character_whole_matrix(copied, broadcast, constructed, source)"
    print "  character(len=4) copied(3), broadcast(3), constructed(3)"
    print "  character(len=2) source(3)"
    print "  copied = source"
    print "  broadcast = \"K\""
    print "  constructed = [ \"A   \", \"BC  \", \"WXYZ\" ]"
    print "end subroutine character_whole_matrix"
    print ""
    print "subroutine character_declaration_matrix(output)"
    print "  character(len=4) output(4)"
    print "  integer, parameter :: base = 3"
    print "  character(len=base+1) :: values(3) = (/ \"A   \", \"BC  \", \"WXYZ\" /)"
    print "  character(len=base+1) :: scalar = \"Q\""
    print "  output(1:3) = values"
    print "  output(4) = scalar"
    print "  values(1) = \"MUT\""
    print "  scalar = \"R\""
    print "end subroutine character_declaration_matrix"
    print ""
    print "subroutine character_length_matrix(value, declared, trimmed, output, automatic_output)"
    print "  character(len=*) value"
    print "  integer declared, trimmed"
    print "  character(len=4) output"
    print "  character(len=9) automatic_output"
    print "  integer, parameter :: base = 3"
    print "  character(len=base+1) local"
    print "  character(len=len(value)+2) automatic"
    print "  declared = len(value)"
    print "  trimmed = len_trim(value)"
    print "  local = \"X\""
    print "  output = local"
    print "  automatic = value // \"XY\""
    print "  automatic_output = automatic"
    print "end subroutine character_length_matrix"
}
' > "$WORK/expressions.f90"

"$F2C" "$WORK/expressions.f90" -o "$WORK/generated.c" --header "$WORK/generated.h"

cat > "$WORK/compare.c" <<'EOF'
#include "generated.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void expression_matrix_(double *x, double *y, double *output);
void array_matrix_(int32_t *n, double *a, double *b, double *c);
void character_matrix_(char *values, char *source, size_t values_length, size_t source_length);
void character_whole_matrix_(char *copied, char *broadcast, char *constructed, char *source,
                             size_t copied_length, size_t broadcast_length,
                             size_t constructed_length, size_t source_length);
void character_declaration_matrix_(char *output, size_t output_length);
void character_length_matrix_(char *value, int32_t *declared, int32_t *trimmed, char *output,
                              char *automatic_output, size_t value_length, size_t output_length,
                              size_t automatic_output_length);

int main(void) {
    double generated[256];
    double reference[256];
    double worst_relative_error = 0.0;
    size_t case_index;
    size_t expression_index;

    for (case_index = 0U; case_index < 32U; ++case_index) {
        double x = ((double)((case_index * 37U) % 29U) - 14.0) / 3.0;
        double y = ((double)((case_index * 53U) % 31U) - 15.0) / 4.0;
        expression_matrix(&x, &y, generated);
        expression_matrix_(&x, &y, reference);
        for (expression_index = 0U; expression_index < 256U; ++expression_index) {
            const double difference = fabs(generated[expression_index] - reference[expression_index]);
            const double scale = 1.0 + fabs(reference[expression_index]);
            const double relative_error = difference / scale;
            if (!isfinite(generated[expression_index]) || !isfinite(reference[expression_index]) ||
                relative_error > 2.0e-12) {
                fprintf(stderr,
                        "expression differential mismatch: case=%zu expression=%zu x=%.17g "
                        "y=%.17g generated=%.17g reference=%.17g relative_error=%.17g\n",
                        case_index, expression_index + 1U, x, y, generated[expression_index],
                        reference[expression_index], relative_error);
                return 1;
            }
            if (relative_error > worst_relative_error)
                worst_relative_error = relative_error;
        }
    }
    for (case_index = 0U; case_index < 32U; ++case_index) {
        int32_t n = 17;
        double generated_a[17];
        double generated_b[17];
        double generated_c[17];
        double reference_a[17];
        double reference_b[17];
        double reference_c[17];
        size_t element_index;
        for (element_index = 0U; element_index < 17U; ++element_index) {
            const double a = ((double)((element_index * 19U + case_index * 7U) % 41U) - 20.0) /
                             7.0;
            const double b = ((double)((element_index * 13U + case_index * 11U) % 37U) - 18.0) /
                             5.0;
            const double c = ((double)((element_index * 23U + case_index * 3U) % 43U) - 21.0) /
                             9.0;
            generated_a[element_index] = reference_a[element_index] = a;
            generated_b[element_index] = reference_b[element_index] = b;
            generated_c[element_index] = reference_c[element_index] = c;
        }
        array_matrix(&n, generated_a, generated_b, generated_c);
        array_matrix_(&n, reference_a, reference_b, reference_c);
        for (element_index = 0U; element_index < 17U; ++element_index) {
            const double generated_values[3] = {generated_a[element_index],
                                                generated_b[element_index],
                                                generated_c[element_index]};
            const double reference_values[3] = {reference_a[element_index],
                                                reference_b[element_index],
                                                reference_c[element_index]};
            size_t array_index;
            for (array_index = 0U; array_index < 3U; ++array_index) {
                const double difference = fabs(generated_values[array_index] -
                                               reference_values[array_index]);
                const double scale = 1.0 + fabs(reference_values[array_index]);
                const double relative_error = difference / scale;
                if (!isfinite(generated_values[array_index]) ||
                    !isfinite(reference_values[array_index]) || relative_error > 2.0e-12) {
                    fprintf(stderr,
                            "array differential mismatch: case=%zu array=%zu element=%zu "
                            "generated=%.17g reference=%.17g relative_error=%.17g\n",
                            case_index, array_index, element_index + 1U,
                            generated_values[array_index], reference_values[array_index],
                            relative_error);
                    return 1;
                }
                if (relative_error > worst_relative_error)
                    worst_relative_error = relative_error;
            }
        }
    }
    for (case_index = 0U; case_index < 32U; ++case_index) {
        char generated_values[16];
        char reference_values[16];
        char generated_source[8];
        char reference_source[8];
        size_t byte_index;
        for (byte_index = 0U; byte_index < 16U; ++byte_index) {
            const char value = (char)('A' + (int)((byte_index + case_index * 3U) % 26U));
            generated_values[byte_index] = reference_values[byte_index] = value;
        }
        for (byte_index = 0U; byte_index < 8U; ++byte_index) {
            const char value = (char)('a' + (int)((byte_index * 5U + case_index) % 26U));
            generated_source[byte_index] = reference_source[byte_index] = value;
        }
        character_matrix(generated_values, generated_source, 4U, 2U);
        character_matrix_(reference_values, reference_source, 4U, 2U);
        if (memcmp(generated_values, reference_values, sizeof(generated_values)) != 0 ||
            memcmp(generated_source, reference_source, sizeof(generated_source)) != 0) {
            fprintf(stderr, "character differential mismatch: case=%zu\n", case_index);
            return 1;
        }
    }
    for (case_index = 0U; case_index < 32U; ++case_index) {
        char generated_copied[12];
        char generated_broadcast[12];
        char generated_constructed[12];
        char reference_copied[12];
        char reference_broadcast[12];
        char reference_constructed[12];
        char generated_source[6];
        char reference_source[6];
        size_t byte_index;
        memset(generated_copied, '?', sizeof(generated_copied));
        memset(generated_broadcast, '?', sizeof(generated_broadcast));
        memset(generated_constructed, '?', sizeof(generated_constructed));
        memset(reference_copied, '?', sizeof(reference_copied));
        memset(reference_broadcast, '?', sizeof(reference_broadcast));
        memset(reference_constructed, '?', sizeof(reference_constructed));
        for (byte_index = 0U; byte_index < sizeof(generated_source); ++byte_index) {
            const char value = (char)('a' + (int)((byte_index * 7U + case_index) % 26U));
            generated_source[byte_index] = reference_source[byte_index] = value;
        }
        character_whole_matrix(generated_copied, generated_broadcast, generated_constructed,
                               generated_source, 4U, 4U, 4U, 2U);
        character_whole_matrix_(reference_copied, reference_broadcast, reference_constructed,
                                reference_source, 4U, 4U, 4U, 2U);
        if (memcmp(generated_copied, reference_copied, sizeof(generated_copied)) != 0 ||
            memcmp(generated_broadcast, reference_broadcast, sizeof(generated_broadcast)) != 0 ||
            memcmp(generated_constructed, reference_constructed,
                   sizeof(generated_constructed)) != 0 ||
            memcmp(generated_source, reference_source, sizeof(generated_source)) != 0) {
            fprintf(stderr, "whole character differential mismatch: case=%zu\n", case_index);
            return 1;
        }
    }
    {
        char generated_first[16];
        char generated_second[16];
        char reference_first[16];
        char reference_second[16];
        character_declaration_matrix(generated_first, 4U);
        character_declaration_matrix(generated_second, 4U);
        character_declaration_matrix_(reference_first, 4U);
        character_declaration_matrix_(reference_second, 4U);
        if (memcmp(generated_first, reference_first, sizeof(generated_first)) != 0 ||
            memcmp(generated_second, reference_second, sizeof(generated_second)) != 0) {
            fprintf(stderr, "character declaration initializer differential mismatch\n");
            return 1;
        }
    }
    for (case_index = 0U; case_index < 32U; ++case_index) {
        char generated_value[7] = {'A', 'B', 'C', ' ', ' ', ' ', ' '};
        char reference_value[7] = {'A', 'B', 'C', ' ', ' ', ' ', ' '};
        char generated_output[4];
        char reference_output[4];
        char generated_automatic_output[9];
        char reference_automatic_output[9];
        int32_t generated_declared = 0;
        int32_t generated_trimmed = 0;
        int32_t reference_declared = 0;
        int32_t reference_trimmed = 0;
        generated_value[case_index % 3U] = (char)('A' + (int)(case_index % 26U));
        reference_value[case_index % 3U] = generated_value[case_index % 3U];
        character_length_matrix(generated_value, &generated_declared, &generated_trimmed,
                                generated_output, generated_automatic_output,
                                sizeof(generated_value), sizeof(generated_output),
                                sizeof(generated_automatic_output));
        character_length_matrix_(reference_value, &reference_declared, &reference_trimmed,
                                 reference_output, reference_automatic_output,
                                 sizeof(reference_value), sizeof(reference_output),
                                 sizeof(reference_automatic_output));
        if (generated_declared != reference_declared || generated_trimmed != reference_trimmed ||
            memcmp(generated_output, reference_output, sizeof(generated_output)) != 0 ||
            memcmp(generated_automatic_output, reference_automatic_output,
                   sizeof(generated_automatic_output)) != 0) {
            fprintf(stderr, "character length differential mismatch: case=%zu\n", case_index);
            return 1;
        }
    }
    printf("differential: 8192 expressions, 1632 numeric array values, and 2112 character "
           "array bytes passed, "
           "worst relative error %.3e\n",
           worst_relative_error);
    return 0;
}
EOF

"$FC" -std=f2018 -O2 -ffp-contract=off -c "$WORK/expressions.f90" -o "$WORK/reference.o"
"$CC" -std=c17 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -I"$WORK" \
    -c "$WORK/generated.c" -o "$WORK/generated.o"
"$CC" -std=c17 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -I"$WORK" \
    -c "$WORK/compare.c" -o "$WORK/compare.o"
"$FC" "$WORK/generated.o" "$WORK/compare.o" "$WORK/reference.o" -lm -o "$WORK/compare"
"$WORK/compare"
