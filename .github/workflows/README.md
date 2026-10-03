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

Performance samples use the same workspace addresses for generated C and native Fortran within
each ABBA/BAAB pair. The two allocations are rotated independently of execution order and receive
equal coverage; inputs are restored before timing, while correctness checks retain independent
outputs. The full gate requires all 71 cases with 24 paired samples and the unchanged 5% limit.
The manual `diagnostics` and `loop-policies` scopes are investigative reports, not parity gates.
Every paired timing is retained at full precision before median selection, with its collection
order, ABBA/BAAB schedule, and workspace number. Reporting happens outside the timed rounds.
The `diagnostics` scope accepts an optional immutable `baseline_sha`; it builds that translator
and captures baseline and candidate assembly, optimization reports, and whole-program LTO
disassembly with the same compiler and pinned numerical sources on one runner. Neither a
diagnostic run nor a baseline comparison substitutes for the full performance gate.
Diagnostic scopes have separate concurrency groups, so requesting a report cannot cancel an
in-progress parity gate for the release candidate.
