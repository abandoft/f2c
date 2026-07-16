#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 /path/to/first/f2c /path/to/second/f2c" >&2
    exit 2
fi

FIRST=$1
SECOND=$2
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/reproducible-toolchains

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK/first" "$WORK/second"

generate_outputs() {
    translator=$1
    output=$2
    "$translator" "$ROOT/test/fixtures/character_abi.f90" \
        -o "$output/character.c" --header "$output/character.h"
    "$translator" "$ROOT/test/fixtures/assigned_goto.f" \
        -o "$output/fixed.c" --header "$output/fixed.h"
    "$translator" \
        "$ROOT/test/fixtures/project_caller.f90" \
        "$ROOT/test/fixtures/project_definition.f90" \
        -o "$output/project.c" --header "$output/project.h"
    "$translator" "$ROOT/test/fixtures/optional_arguments.f90" \
        -o "$output/optional.c" --header "$output/optional.h"
    "$translator" "$ROOT/test/fixtures/explicit_interface.f90" \
        -o "$output/interface.c" --header "$output/interface.h"
    "$translator" "$ROOT/test/fixtures/procedure_interface.f90" \
        -o "$output/procedure.c" --header "$output/procedure.h"
    "$translator" "$ROOT/test/fixtures/deferred_character.f90" \
        -o "$output/deferred.c" --header "$output/deferred.h"
    "$translator" --version > "$output/version.txt"
}

generate_outputs "$FIRST" "$WORK/first"
generate_outputs "$SECOND" "$WORK/second"

for file in character.c character.h fixed.c fixed.h project.c project.h optional.c optional.h \
    interface.c interface.h procedure.c procedure.h deferred.c deferred.h version.txt; do
    if ! cmake -E compare_files "$WORK/first/$file" "$WORK/second/$file"; then
        echo "cross-toolchain generated output differs: $file" >&2
        exit 1
    fi
done

(cd "$WORK/first" && cmake -E sha256sum \
    character.c character.h fixed.c fixed.h project.c project.h optional.c optional.h \
    interface.c interface.h procedure.c procedure.h deferred.c deferred.h version.txt) \
    > "$WORK/SHA256SUMS"
echo "cross-toolchain reproducibility: 15/15 outputs are byte-identical"
