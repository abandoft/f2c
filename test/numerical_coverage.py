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
    if not isinstance(baseline, dict) or baseline.get("schema_version") != 2:
        raise CoverageError("coverage baseline must use schema version 2")

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
        entry = internal.get(label)
        if not isinstance(entry, dict):
            raise CoverageError(f"internal.{label} must be an object")
        profiles = entry.get("profiles")
        if not isinstance(profiles, list) or not profiles:
            raise CoverageError(f"internal.{label}.profiles must be a non-empty array")
        names: set[str] = set()
        invariant_values: tuple[int, int] | None = None
        for index, profile in enumerate(profiles):
            profile_label = f"internal.{label}.profiles[{index}]"
            if not isinstance(profile, dict):
                raise CoverageError(f"{profile_label} must be an object")
            name = profile.get("name")
            if not isinstance(name, str) or not name or name in names:
                raise CoverageError(f"{profile_label}.name must be a unique non-empty string")
            names.add(name)
            values = {
                field: _nonnegative_integer(profile, field, profile_label)
                for field in INTERNAL_FIELDS
            }
            if values["records"] != (
                values["generated_records"] + values["native_only_records"]
            ) or values["records"] != (
                values["native_records"] + values["generated_only_records"]
            ):
                raise CoverageError(f"{profile_label} union totals are inconsistent")
            invariants = (values["suites"], values["generated_records"])
            if invariant_values is None:
                invariant_values = invariants
            elif invariants != invariant_values:
                raise CoverageError(
                    f"internal.{label} profiles must preserve suite and generated coverage"
                )
    return baseline


def internal_profiles(baseline: dict[str, object], label: str) -> list[dict[str, object]]:
    internal = baseline["internal"]
    assert isinstance(internal, dict)
    entry = internal[label]
    assert isinstance(entry, dict)
    profiles = entry["profiles"]
    assert isinstance(profiles, list)
    return profiles


def match_internal_profile(
    label: str,
    values: object,
    baseline: dict[str, object] | None = None,
) -> dict[str, object]:
    if baseline is None:
        baseline = load_baseline()
    actual = {
        field: _nonnegative_integer(values, field, f"actual.{label}")
        for field in INTERNAL_FIELDS
    }
    profiles = internal_profiles(baseline, label)
    for profile in profiles:
        if all(actual[field] == profile[field] for field in INTERNAL_FIELDS):
            return profile
    names = ", ".join(str(profile["name"]) for profile in profiles)
    details = ", ".join(f"{field}={actual[field]}" for field in INTERNAL_FIELDS)
    raise CoverageError(
        f"{label} coverage does not match a supported native profile "
        f"({names}): {details}"
    )


def verify_internal_manifest(
    label: str, manifest_path: Path, baseline_path: Path = BASELINE_PATH
) -> dict[str, object]:
    baseline = load_baseline(baseline_path)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(
            f"cannot load numerical manifest {manifest_path}: {error}"
        ) from error
    if not isinstance(manifest, dict):
        raise CoverageError(f"numerical manifest {manifest_path} must be an object")
    return match_internal_profile(label, manifest, baseline)


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
            for label in ("blas", "lapack"):
                for profile in internal_profiles(baseline, label):
                    matched = match_internal_profile(label, profile, baseline)
                    assert matched["name"] == profile["name"]
            lapack_profiles = internal_profiles(baseline, "lapack")
            if len(lapack_profiles) > 1:
                hybrid = dict(lapack_profiles[0])
                hybrid["native_records"] = lapack_profiles[1]["native_records"]
                try:
                    match_internal_profile("lapack", hybrid, baseline)
                except CoverageError:
                    pass
                else:
                    raise CoverageError("a mixed native coverage profile was accepted")
            print("numerical coverage baseline self-test passed")
            return 0
        if args.label is None or args.manifest is None:
            parser.error("label and manifest are required")
        matched = verify_internal_manifest(args.label, args.manifest, args.baseline)
    except CoverageError as error:
        print(f"numerical coverage validation failed: {error}", file=sys.stderr)
        return 1
    print(
        f"validated {args.label.upper()} coverage profile {matched['name']}: "
        f"{matched['suites']} suites, {matched['records']} union records"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
