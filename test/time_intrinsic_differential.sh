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
WORK=$ROOT/build/time-intrinsic-differential
SOURCE=$ROOT/test/fixtures/time_intrinsics.f90

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi
if ! command -v "$FC" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $FC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
    -o "$WORK/generated"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$WORK/generated.c" -lm -o "$WORK/generated-sanitized"
"$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
    "$SOURCE" -o "$WORK/native"

"$WORK/generated" >"$WORK/generated.out"
"$WORK/generated-sanitized" >"$WORK/generated-sanitized.out"
"$WORK/native" >"$WORK/native.out"

if ! cmp -s "$WORK/generated.out" "$WORK/native.out"; then
    echo "generated/native time intrinsic property mismatch" >&2
    diff -u "$WORK/native.out" "$WORK/generated.out" >&2 || true
    exit 1
fi
if ! cmp -s "$WORK/generated.out" "$WORK/generated-sanitized.out"; then
    echo "optimized/sanitized time intrinsic output mismatch" >&2
    diff -u "$WORK/generated.out" "$WORK/generated-sanitized.out" >&2 || true
    exit 1
fi

echo "time intrinsic differential and sanitizer validation passed"

"$F2C" "$ROOT/test/fixtures/etime.f90" -o "$WORK/etime.c"
for profile in optimized sanitized unavailable; do
    case $profile in
        optimized) flags='-O2' ;;
        sanitized) flags='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all' ;;
        unavailable) flags='-O2 -DF2C_DISABLE_PROCESS_CPU_TIME=1' ;;
    esac
    # Intentional splitting of controlled, literal compiler flags.
    # shellcheck disable=SC2086
    "$CC" -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes \
        -Wmissing-prototypes -Werror $flags "$WORK/etime.c" -lm -o "$WORK/etime-$profile"
    "$WORK/etime-$profile" > "$WORK/etime-$profile.out"
done
"$FC" -std=gnu -O2 -Wall -Wextra -Werror "$ROOT/test/fixtures/etime.f90" -o "$WORK/etime-native"
"$WORK/etime-native" > "$WORK/etime-native.out"
for profile in optimized sanitized unavailable; do
    cmp "$WORK/etime-native.out" "$WORK/etime-$profile.out"
done
echo "ETIME native properties, sanitizer checks, and unavailable-platform contract passed"
