#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
FC=${FC:-gfortran}
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/allocation-model-differential
SOURCES="allocation_model allocation_model_rich allocation_result_values allocation_native_bounds allocation_control_bounds allocation_control_names"

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
native_passed=0
native_unsupported=0
printf 'fixture\tnative_status\n' > "$WORK/native-status.tsv"

for NAME in $SOURCES; do
    SOURCE=$ROOT/test/fixtures/$NAME.f90
    "$F2C" "$SOURCE" -o "$WORK/$NAME.c"
    "$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/$NAME.c" -lm \
        -o "$WORK/$NAME-generated"
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$WORK/$NAME.c" -lm -o "$WORK/$NAME-sanitized"
    "$WORK/$NAME-generated" >"$WORK/$NAME-generated.out"
    "$WORK/$NAME-sanitized" >"$WORK/$NAME-sanitized.out"
    case $NAME in
        allocation_control_bounds|allocation_native_bounds)
            cmp "$ROOT/test/fixtures/$NAME.out" "$WORK/$NAME-generated.out" ;;
    esac
    cmp "$WORK/$NAME-generated.out" "$WORK/$NAME-sanitized.out"
    if ! "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
        -J"$WORK" -I"$WORK" "$SOURCE" -o "$WORK/$NAME-native" \
        > "$WORK/$NAME-native-compile.log" 2>&1; then
        # Keep the complete source and generated/sanitized contract intact.
        # Older GNU compilers reject this valid dynamic nondeferred type-spec;
        # the separate raw native-bounds fixture still covers the other cases.
        version=$("$FC" -dumpversion 2>/dev/null || true)
        major=${version%%.*}
        case $major in
            ''|*[!0-9]*) major=0 ;;
        esac
        if [ "$NAME" = allocation_control_bounds ] &&
            "$FC" --version | head -n 1 | grep -q '^GNU Fortran' &&
            [ "$major" -ge 13 ] && [ "$major" -lt 16 ] &&
            [ "$(grep -c '^Error:' "$WORK/$NAME-native-compile.log")" -eq 1 ] &&
            grep -q '^Error: Allocating fixed at .* with type-spec requires the same character-length parameter as in the declaration$' \
                "$WORK/$NAME-native-compile.log"; then
            cat "$WORK/$NAME-native-compile.log" >&2
            echo "$NAME: GNU $major native capability unsupported; complete generated/sanitized contract passed (not a native differential pass)" >&2
            printf '%s\tunsupported-gnu-dynamic-nondeferred-length\n' "$NAME" \
                >> "$WORK/native-status.tsv"
            native_unsupported=$((native_unsupported + 1))
            continue
        fi
        cat "$WORK/$NAME-native-compile.log" >&2
        exit 1
    fi
    "$WORK/$NAME-native" >"$WORK/$NAME-native.out"

    if ! cmp -s "$WORK/$NAME-generated.out" "$WORK/$NAME-native.out"; then
        echo "generated/native ALLOCATE model behavior mismatch: $NAME" >&2
        diff -u "$WORK/$NAME-native.out" "$WORK/$NAME-generated.out" >&2 || true
        exit 1
    fi
    if ! cmp -s "$WORK/$NAME-generated.out" "$WORK/$NAME-sanitized.out"; then
        echo "optimized/sanitized ALLOCATE model output mismatch: $NAME" >&2
        diff -u "$WORK/$NAME-generated.out" "$WORK/$NAME-sanitized.out" >&2 || true
        exit 1
    fi
    printf '%s\tpassed\n' "$NAME" >> "$WORK/native-status.tsv"
    native_passed=$((native_passed + 1))
done

echo "ALLOCATE contracts passed; native differential passes: $native_passed; explicitly unsupported native capabilities: $native_unsupported"
