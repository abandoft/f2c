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
WORK=$ROOT/build/elemental-differential
SOURCE=$ROOT/test/fixtures/elemental_procedure.f90

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

# GCC PR 108889: GNU Fortran 13/14 can diagnose the compiler's private
# deferred-character length field during valid reallocating assignment.
# Keep the warning visible and allow only this exact field, not user variables.
FC_ID=$("$FC" --version | sed -n '1p')
FC_VERSION=$("$FC" -dumpfullversion -dumpversion 2>/dev/null || true)
native_warning_options=
case "$FC_ID:$FC_VERSION" in
    *GNU*Fortran*:13.* | *GNU*Fortran*:14.*)
        native_warning_options=-Wno-error=uninitialized
        ;;
esac

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
    -o "$WORK/generated"
# Deliberate splitting: the allowlisted option is a single fixed compiler flag.
# shellcheck disable=SC2086
if ! LC_ALL=C "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
    $native_warning_options -J "$WORK" "$SOURCE" -o "$WORK/native" \
    2> "$WORK/native-compile.log"; then
    cat "$WORK/native-compile.log" >&2
    exit 1
fi
cat "$WORK/native-compile.log" >&2
if sed -n '/\[-Wuninitialized\]/p' "$WORK/native-compile.log" | \
    LC_ALL=C grep -v -x -F \
        "Warning: '.zero_dynamic' is used uninitialized [-Wuninitialized]" \
        > "$WORK/native-unexpected-uninitialized.log"; then
    echo "unexpected native Fortran uninitialized warning" >&2
    cat "$WORK/native-unexpected-uninitialized.log" >&2
    exit 1
fi

"$WORK/generated" > "$WORK/generated.out"
"$WORK/native" > "$WORK/native.out"

if ! cmp -s "$WORK/generated.out" "$WORK/native.out"; then
    echo "generated/native ELEMENTAL procedure output mismatch" >&2
    diff -u "$WORK/native.out" "$WORK/generated.out" >&2 || true
    exit 1
fi

echo "ELEMENTAL procedure differential passed"
