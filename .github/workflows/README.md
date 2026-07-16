# GitHub Actions workflows

Each workflow owns one operational responsibility and can be rerun independently. GitHub only
discovers workflow definitions stored directly in `.github/workflows`, so the directory is kept
flat and descriptive rather than grouped into nested folders.

| Workflow | Pull requests | `main` | Scheduled/manual | Responsibility |
|---|---:|---:|---:|---|
| `build-and-test.yml` | Yes | Yes | Manual | Static/shared Debug/Release builds and tests on Linux, macOS, and Windows |
| `sanitizers.yml` | Yes | Yes | Manual | Clang AddressSanitizer and UndefinedBehaviorSanitizer test suite |
| `reproducibility.yml` | Yes | Yes | Manual | Byte-for-byte GCC/Clang generated-output comparison |
| `webassembly.yml` | Yes | Yes | Manual | Emscripten portability build |
| `numerical-validation.yml` | Yes | Yes | Manual | Complete BLAS/LAPACK compilation, native differential, and numerical audit |
| `performance.yml` | No | Yes | Weekly/manual | Generated C versus native Fortran performance parity |
| `fuzz.yml` | No | No | Weekly/manual | Coverage-guided libFuzzer campaign with ASan/UBSan |
| `release.yml` | No | Tagged release | Plain semantic-version tag such as `1.0.0` | Tested packages, checksums, provenance, and GitHub release publication |

The first five workflows are the stable correctness and portability checks intended for branch
protection. Performance is isolated because timing on shared runners is operationally different
from deterministic correctness. Every workflow uses least-privilege permissions, explicit job
timeouts, concurrency control, and writes generated data only below `build/`.
