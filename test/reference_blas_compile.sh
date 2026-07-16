#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

f2c_input=$1
f2c_dir=$(CDPATH= cd -- "$(dirname -- "$f2c_input")" && pwd)
f2c="$f2c_dir/$(basename -- "$f2c_input")"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/reference-blas-compile
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca

rm -rf "$work"
mkdir -p "$work/generated" "$work/objects"

git clone --quiet --filter=blob:none --no-checkout \
    https://github.com/Reference-LAPACK/lapack.git "$work/lapack"
git -C "$work/lapack" sparse-checkout init --cone
git -C "$work/lapack" sparse-checkout set BLAS/SRC
git -C "$work/lapack" checkout --quiet "$lapack_commit"

cc=${CC:-cc}
find "$work/lapack/BLAS/SRC" -type f \( -iname '*.f' -o -iname '*.f90' \) | sort > \
    "$work/sources.list"
count=$(wc -l < "$work/sources.list" | tr -d ' ')
while IFS= read -r source; do
    filename=$(basename -- "$source")
    name=${filename%.*}
    "$f2c" "$source" -o "$work/generated/$name.c"
    "$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
        -c "$work/generated/$name.c" -o "$work/objects/$name.o"
done < "$work/sources.list"

if [ "$count" -ne 155 ]; then
    echo "expected 155 Reference BLAS sources, compiled $count" >&2
    exit 1
fi
echo "translated and warning-clean compiled all $count Reference BLAS sources"
