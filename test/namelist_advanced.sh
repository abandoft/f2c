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
WORK=$ROOT/build/namelist-advanced

for compiler in "$CC" "$FC"; do
    if ! command -v "$compiler" >/dev/null 2>&1; then
        echo "compiler not found: $compiler" >&2
        exit 2
    fi
done

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

for fixture in namelist_section_bounds namelist_multidimensional_auto_allocate \
    namelist_dynamic_shape namelist_derived_array namelist_derived_sections \
    namelist_invalid_designators namelist_zero_length_character \
    namelist_polymorphic_component namelist_dtio_unit; do
    source_file=$ROOT/test/fixtures/$fixture.f90
    generated_c=$WORK/$fixture.c
    "$F2C" "$source_file" -o "$generated_c"
    "$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$generated_c" -lm \
        -o "$WORK/$fixture"
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -fno-sanitize-recover=all "$generated_c" -lm \
        -o "$WORK/$fixture-sanitized"
    "$WORK/$fixture" >"$WORK/$fixture.out"

    asan_leaks=1
    if [ "$(uname -s)" = Darwin ]; then
        asan_leaks=0
    fi
    ASAN_OPTIONS=detect_leaks=$asan_leaks:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$WORK/$fixture-sanitized" >"$WORK/$fixture-sanitized.out"
    cmake -E compare_files "$WORK/$fixture.out" "$WORK/$fixture-sanitized.out"
done

"$FC" -std=f2018 -O2 -Wall -Wextra -Werror -J "$WORK" \
    "$ROOT/test/fixtures/namelist_dtio_unit.f90" -o "$WORK/namelist_dtio_unit-native"
"$WORK/namelist_dtio_unit-native" >"$WORK/namelist_dtio_unit-native.out"
cmake -E compare_files "$WORK/namelist_dtio_unit-native.out" \
    "$WORK/namelist_dtio_unit.out"

"$FC" -std=f2018 -O2 -Wall -Wextra -Werror -J "$WORK" \
    "$ROOT/test/fixtures/namelist_derived_array.f90" -o "$WORK/namelist_derived_array-native"
"$WORK/namelist_derived_array-native" >"$WORK/namelist_derived_array-native.out"
cmake -E compare_files "$WORK/namelist_derived_array-native.out" \
    "$WORK/namelist_derived_array.out"

"$FC" -std=f2018 -O2 -Wall -Wextra -Werror -J "$WORK" \
    "$ROOT/test/fixtures/namelist_invalid_designators.f90" \
    -o "$WORK/namelist_invalid_designators-native"
"$WORK/namelist_invalid_designators-native" \
    >"$WORK/namelist_invalid_designators-native.out"
cmake -E compare_files "$WORK/namelist_invalid_designators-native.out" \
    "$WORK/namelist_invalid_designators.out"

echo "advanced NAMELIST validation passed"
