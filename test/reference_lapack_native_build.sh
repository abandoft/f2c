#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/reference-lapack-work" >&2
    exit 2
fi

work=$1
source=$work/lapack
build=$work/native
build_log=$work/native-build.log
fc=${FC:-gfortran}

if ! command -v "$fc" >/dev/null 2>&1; then
    echo "a native Fortran compiler is required for LAPACK differential testing" >&2
    exit 2
fi
if [ ! -f "$source/CMakeLists.txt" ]; then
    echo "Reference LAPACK source tree is missing: $source" >&2
    exit 2
fi

: >"$build_log"
if ! cmake -S "$source" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_Fortran_COMPILER="$(command -v "$fc")" \
    -DBUILD_TESTING=ON \
    -DBUILD_INDEX64_EXT_API=OFF \
    -DTEST_FORTRAN_COMPILER=ON \
    -DLAPACKE=OFF \
    -DCBLAS=OFF \
    -DLAPACK_TESTING_USE_PYTHON=OFF >>"$build_log" 2>&1; then
    tail -n 100 "$build_log" >&2
    exit 1
fi

if ! cmake --build "$build" --parallel "${F2C_JOBS:-2}" --target \
    xblat1s xblat2s xblat3s xblat1d xblat2d xblat3d \
    xblat1c xblat2c xblat3c xblat1z xblat2z xblat3z \
    xlintsts xlintstd xlintstc xlintstz \
    xlintstrfs xlintstrfd xlintstrfc xlintstrfz \
    xeigtsts xeigtstd xeigtstc xeigtstz \
    test_zcomplexabs test_zcomplexdiv test_zcomplexmult test_zminMax \
    >>"$build_log" 2>&1; then
    tail -n 100 "$build_log" >&2
    exit 1
fi

mkdir -p "$work/native-results"
echo "built native Fortran Reference LAPACK differential baseline"
