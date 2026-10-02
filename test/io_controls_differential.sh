#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

f2c=$1
cc=${CC:-cc}
fc=${FC:-gfortran}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/io-controls-differential

for compiler in "$cc" "$fc"; do
    if ! command -v "$compiler" >/dev/null 2>&1; then
        echo "compiler not found: $compiler" >&2
        exit 2
    fi
done

cmake -E remove_directory "$work"
cmake -E make_directory "$work"

run_case() {
    name=$1
    source=$root/test/fixtures/$name.f90
    "$f2c" "$source" -o "$work/generated_$name.c"
    "$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/generated_$name.c" \
        -lm -o "$work/$name"
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$work/generated_$name.c" -lm \
        -o "$work/$name-sanitized"
    "$work/$name" > "$work/$name.out"
    "$work/$name-sanitized" > "$work/$name-sanitized.out"
    cmp "$work/$name.out" "$work/$name-sanitized.out"
    if [ "$name" != dtio_controls ]; then
        "$fc" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
            -J "$work" "$source" -o "$work/$name-native"
        "$work/$name-native" > "$work/$name-native.out"
        if ! cmp -s "$work/$name.out" "$work/$name-native.out"; then
            echo "generated/native I/O controls mismatch: $name" >&2
            diff -u "$work/$name-native.out" "$work/$name.out" >&2 || true
            exit 1
        fi
    fi
}

run_case list_controls
run_case dtio_connection_controls
run_case dtio_controls
cmp "$root/test/generated/dtio_controls.txt" "$work/dtio_controls.out"

"$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -I "$work" \
    "$root/test/generated/io_number_test.c" -lm -o "$work/io-number-test"
"$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I "$work" "$root/test/generated/io_number_test.c" \
    -lm -o "$work/io-number-test-sanitized"
"$work/io-number-test"
"$work/io-number-test-sanitized"

echo "I/O controls, exact rounding, DT scope, and sanitizer validation passed"
