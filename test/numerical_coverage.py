#!/usr/bin/env python3
"""Validate versioned BLAS/LAPACK numerical coverage baselines."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


BASELINE_PATH = Path(__file__).with_name("numerical_coverage.json")
INTERNAL_FIELDS = (
    "suites",
    "records",
    "generated_records",
    "native_records",
    "generated_only_records",
    "native_only_records",
)
OBSERVABLE_FIELDS = (
    "suite_count",
    "numerical_records",
    "status_records",
    "observable_records",
)


class CoverageError(Exception):
    """The coverage baseline or a produced manifest is invalid."""


def _nonnegative_integer(mapping: object, field: str, label: str) -> int:
    if not isinstance(mapping, dict):
        raise CoverageError(f"{label} must be an object")
    value = mapping.get(field)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise CoverageError(f"{label}.{field} must be a non-negative integer")
    return value


def load_baseline(path: Path = BASELINE_PATH) -> dict[str, object]:
    try:
        baseline = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(f"cannot load coverage baseline {path}: {error}") from error
    if not isinstance(baseline, dict) or baseline.get("schema_version") != 1:
        raise CoverageError("coverage baseline must use schema version 1")

    observable = baseline.get("observable")
    observable_values = {
        field: _nonnegative_integer(observable, field, "observable")
        for field in OBSERVABLE_FIELDS
    }
    if observable_values["observable_records"] != (
        observable_values["numerical_records"] + observable_values["status_records"]
    ):
        raise CoverageError("observable record totals are inconsistent")

    internal = baseline.get("internal")
    if not isinstance(internal, dict):
        raise CoverageError("internal must be an object")
    for label in ("blas", "lapack"):
        expected = internal.get(label)
        values = {
            field: _nonnegative_integer(expected, field, f"internal.{label}")
            for field in INTERNAL_FIELDS
        }
        if values["records"] != (
            values["generated_records"] + values["native_only_records"]
        ) or values["records"] != (
            values["native_records"] + values["generated_only_records"]
        ):
            raise CoverageError(f"internal.{label} union totals are inconsistent")
    return baseline


def verify_internal_manifest(
    label: str, manifest_path: Path, baseline_path: Path = BASELINE_PATH
) -> None:
    baseline = load_baseline(baseline_path)
    internal = baseline["internal"]
    assert isinstance(internal, dict)
    expected = internal[label]
    assert isinstance(expected, dict)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(
            f"cannot load numerical manifest {manifest_path}: {error}"
        ) from error
    if not isinstance(manifest, dict):
        raise CoverageError(f"numerical manifest {manifest_path} must be an object")
    for field in INTERNAL_FIELDS:
        actual = manifest.get(field)
        if actual != expected[field]:
            raise CoverageError(
                f"{label} {field} differs: actual={actual!r}, expected={expected[field]}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("label", nargs="?", choices=("blas", "lapack"))
    parser.add_argument("manifest", nargs="?", type=Path)
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        baseline = load_baseline(args.baseline)
        if args.self_test:
            print("numerical coverage baseline self-test passed")
            return 0
        if args.label is None or args.manifest is None:
            parser.error("label and manifest are required")
        verify_internal_manifest(args.label, args.manifest, args.baseline)
    except CoverageError as error:
        print(f"numerical coverage validation failed: {error}", file=sys.stderr)
        return 1
    internal = baseline["internal"]
    assert isinstance(internal, dict)
    expected = internal[args.label]
    assert isinstance(expected, dict)
    print(
        f"validated {args.label.upper()} coverage: {expected['suites']} suites, "
        f"{expected['records']} union records"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
