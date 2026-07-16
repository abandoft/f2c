#!/usr/bin/env python3
"""Validate and summarize every official BLAS/LAPACK differential report."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import sys
import tempfile


EXPECTED_SUITES = {
    "blas": 12,
    "install": 4,
    "rfp": 4,
    "lin": 4,
    "eig": 80,
}
BLAS_SUITE = re.compile(r"^[sdcz]blat[123]$")


class ManifestError(Exception):
    pass


def category(suite: str) -> str:
    if BLAS_SUITE.fullmatch(suite):
        return "blas"
    for prefix in ("install", "rfp", "lin", "eig"):
        if suite.startswith(f"{prefix}-"):
            return prefix
    raise ManifestError(f"unknown official suite {suite!r}")


def load_reports(root: Path) -> list[tuple[Path, dict[str, object]]]:
    reports: list[tuple[Path, dict[str, object]]] = []
    for path in sorted(root.rglob("*.json")):
        if path == root / "manifest.json":
            continue
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ManifestError(f"cannot read {path}: {error}") from error
        if not isinstance(report, dict):
            raise ManifestError(f"report {path} is not a JSON object")
        reports.append((path, report))
    return reports


def is_json_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def numerical_value(value: object) -> float | None:
    if is_json_number(value):
        return float(value)
    if value in ("NaN", "Infinity", "-Infinity"):
        return float(str(value))
    return None


def validate_numerical_record(suite: str, index: int, record: dict[str, object]) -> bool:
    generated = record["generated"]
    native = record["native"]
    generated_number = numerical_value(generated)
    native_number = numerical_value(native)
    generated_is_number = generated_number is not None
    native_is_number = native_number is not None
    if generated_is_number != native_is_number:
        raise ManifestError(
            f"report {suite} record {index} compares a number with a non-number"
        )
    if not generated_is_number:
        if record.get("comparison") != "exact" or generated != native:
            raise ManifestError(
                f"report {suite} non-numerical record {index} differs"
            )
        return False

    for field in ("absolute_difference", "relative_difference", "relative_tolerance"):
        if field not in record:
            raise ManifestError(
                f"report {suite} numerical record {index} has no {field}"
            )
    assert generated_number is not None and native_number is not None
    absolute = record["absolute_difference"]
    relative = record["relative_difference"]
    if math.isfinite(generated_number) and math.isfinite(native_number):
        if not is_json_number(absolute) or not is_json_number(relative):
            raise ManifestError(
                f"report {suite} numerical record {index} has non-numerical differences"
            )
        expected_absolute = abs(generated_number - native_number)
        expected_relative = (
            expected_absolute / abs(native_number)
            if native_number != 0.0
            else expected_absolute
        )
        if float(absolute) != expected_absolute or float(relative) != expected_relative:
            raise ManifestError(
                f"report {suite} numerical record {index} has inconsistent differences"
            )
    elif absolute is not None or relative is not None:
        raise ManifestError(
            f"report {suite} non-finite numerical record {index} must use null differences"
        )
    tolerance = record["relative_tolerance"]
    if tolerance is not None and (
        not is_json_number(tolerance)
        or not math.isfinite(float(tolerance))
        or tolerance < 0
    ):
        raise ManifestError(
            f"report {suite} numerical record {index} has an invalid tolerance"
        )

    comparison = record.get("comparison")
    if comparison == "exact":
        if generated_number != native_number:
            raise ManifestError(
                f"report {suite} exact record {index} differs"
            )
    elif comparison in ("relative", "formatted_relative"):
        if tolerance is None:
            raise ManifestError(
                f"report {suite} relative record {index} has no tolerance"
            )
        if math.isfinite(generated_number) and math.isfinite(native_number):
            assert is_json_number(relative)
            if float(relative) > float(tolerance):
                raise ManifestError(
                    f"report {suite} relative record {index} exceeds its tolerance"
                )
        elif generated_number != native_number:
            raise ManifestError(
                f"report {suite} non-finite relative record {index} differs"
            )
    elif comparison == "no_regression":
        if math.isnan(generated_number) or math.isnan(native_number) or generated_number > native_number:
            raise ManifestError(
                f"report {suite} no-regression record {index} regresses"
            )
    elif comparison == "roundoff_bound":
        limit = record.get("absolute_limit")
        if (
            not is_json_number(limit)
            or not math.isfinite(float(limit))
            or float(limit) < 0.0
            or not math.isfinite(generated_number)
            or not math.isfinite(native_number)
            or generated_number < 0.0
            or native_number < 0.0
            or generated_number > float(limit)
            or native_number > float(limit)
        ):
            raise ManifestError(
                f"report {suite} roundoff-bound record {index} exceeds its limit"
            )
    elif comparison == "argmax_of_roundoff_bound":
        linked = record.get("linked_record")
        if (
            not isinstance(linked, str)
            or not linked
            or not generated_number.is_integer()
            or not native_number.is_integer()
            or generated_number < 0.0
            or native_number < 0.0
        ):
            raise ManifestError(
                f"report {suite} roundoff argmax record {index} is invalid"
            )
    else:
        raise ManifestError(
            f"report {suite} numerical record {index} has unknown comparison {comparison!r}"
        )
    return True


def validate_report(path: Path, report: dict[str, object]) -> tuple[str, str, int, int]:
    suite = report.get("suite")
    if not isinstance(suite, str) or not suite:
        raise ManifestError(f"report {path} has no suite identifier")
    if report.get("schema_version") != 1 or report.get("status") != "matched":
        raise ManifestError(f"report {suite} is not a matched schema-v1 report")
    records = report.get("records")
    if not isinstance(records, list) or not records:
        raise ManifestError(f"report {suite} has no comparison records")
    actual_numerical_records = 0
    for index, record in enumerate(records, 1):
        if not isinstance(record, dict) or record.get("status") != "matched":
            raise ManifestError(f"report {suite} record {index} is not matched")
        if not all(name in record for name in ("name", "generated", "native", "comparison")):
            raise ManifestError(f"report {suite} record {index} is incomplete")
        actual_numerical_records += validate_numerical_record(suite, index, record)
    records_by_name = {
        record.get("name"): record for record in records if isinstance(record, dict)
    }
    for index, record in enumerate(records, 1):
        if not isinstance(record, dict) or record.get("comparison") != "argmax_of_roundoff_bound":
            continue
        linked = records_by_name.get(record.get("linked_record"))
        if not isinstance(linked, dict) or linked.get("comparison") != "roundoff_bound":
            raise ManifestError(
                f"report {suite} roundoff argmax record {index} has no bounded result"
            )
    numerical_records = report.get("numerical_records")
    if not isinstance(numerical_records, int) or numerical_records < 0:
        raise ManifestError(f"report {suite} has an invalid numerical record count")
    if numerical_records != actual_numerical_records:
        raise ManifestError(
            f"report {suite} numerical-record count differs: "
            f"declared={numerical_records}, actual={actual_numerical_records}"
        )
    transcripts = report.get("transcripts")
    if not isinstance(transcripts, dict):
        raise ManifestError(f"report {suite} has no transcript provenance")
    for side in ("generated_c", "native_fortran"):
        metadata = transcripts.get(side)
        if not isinstance(metadata, dict) or not isinstance(metadata.get("file"), str):
            raise ManifestError(f"report {suite} has no {side} transcript metadata")
        digest = metadata.get("sha256")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ManifestError(f"report {suite} has an invalid {side} SHA-256")
    return suite, category(suite), numerical_records, len(records)


def build_manifest(root: Path) -> dict[str, object]:
    loaded = load_reports(root)
    suites: set[str] = set()
    categories: Counter[str] = Counter()
    numerical_by_category: Counter[str] = Counter()
    records_by_category: Counter[str] = Counter()
    comparison_policies: Counter[str] = Counter()
    result_groups = 0
    computational_calls = 0
    entries: list[dict[str, object]] = []
    for path, report in loaded:
        suite, suite_category, numerical_records, record_count = validate_report(path, report)
        if suite in suites:
            raise ManifestError(f"duplicate suite report {suite!r}")
        suites.add(suite)
        categories[suite_category] += 1
        numerical_by_category[suite_category] += numerical_records
        records_by_category[suite_category] += record_count
        records = report.get("records")
        assert isinstance(records, list)
        comparison_policies.update(
            str(record["comparison"])
            for record in records
            if isinstance(record, dict)
        )
        groups = report.get("result_groups", 0)
        calls = report.get("computational_calls", 0)
        if not isinstance(groups, int) or not isinstance(calls, int):
            raise ManifestError(f"report {suite} has non-integral coverage counters")
        result_groups += groups
        computational_calls += calls
        entries.append(
            {
                "suite": suite,
                "category": suite_category,
                "report": str(path.relative_to(root)),
                "numerical_records": numerical_records,
                "records": record_count,
            }
        )

    if dict(categories) != EXPECTED_SUITES:
        raise ManifestError(
            f"official suite coverage differs: actual={dict(categories)}, "
            f"expected={EXPECTED_SUITES}"
        )
    if result_groups != 146 or computational_calls != 262388:
        raise ManifestError(
            "official BLAS coverage differs: "
            f"groups={result_groups}, calls={computational_calls}"
        )
    if numerical_by_category != Counter(
        {"blas": 116, "install": 4, "rfp": 32, "lin": 212, "eig": 532}
    ):
        raise ManifestError(
            f"official numerical-record coverage differs: {dict(numerical_by_category)}"
        )
    status_records = sum(records_by_category.values()) - sum(
        numerical_by_category.values()
    )
    if status_records != 46:
        raise ManifestError(
            f"official status-record coverage differs: actual={status_records}, expected=46"
        )
    return {
        "schema_version": 1,
        "status": "matched",
        "suite_count": len(entries),
        "numerical_records": sum(numerical_by_category.values()),
        "status_records": status_records,
        "observable_records": sum(records_by_category.values()),
        "blas_result_groups": result_groups,
        "blas_computational_calls": computational_calls,
        "comparison_policies": dict(sorted(comparison_policies.items())),
        "categories": {
            name: {
                "suites": categories[name],
                "numerical_records": numerical_by_category[name],
                "observable_records": records_by_category[name],
            }
            for name in EXPECTED_SUITES
        },
        "suites": sorted(entries, key=lambda item: str(item["suite"])),
    }


def write_summary(path: Path, manifest: dict[str, object]) -> None:
    categories = manifest["categories"]
    assert isinstance(categories, dict)
    lines = [
        "# Official BLAS/LAPACK numerical differential",
        "",
        "Every numerical record exposed by the official test transcripts is compared with the ",
        "native Fortran build from the same pinned source tree. Internal test ratios that the ",
        "official drivers do not print are covered by their official threshold/pass result, not ",
        "represented as byte-for-byte floating-point equality.",
        "",
        f"- Status: `{manifest['status']}`",
        f"- Suites: {manifest['suite_count']}",
        f"- Numerical records: {manifest['numerical_records']}",
        f"- Level 1 status records: {manifest['status_records']}",
        f"- All observable records (including Level 1 PASS records): {manifest['observable_records']}",
        f"- BLAS result groups: {manifest['blas_result_groups']}",
        f"- BLAS Level 2/3 computational calls: {manifest['blas_computational_calls']}",
        f"- Comparison policies: `{json.dumps(manifest['comparison_policies'], sort_keys=True)}`",
        "",
        "| Category | Suites | Numerical records | Observable records |",
        "| --- | ---: | ---: | ---: |",
    ]
    for name in EXPECTED_SUITES:
        item = categories[name]
        assert isinstance(item, dict)
        lines.append(
            f"| {name.upper()} | {item['suites']} | {item['numerical_records']} | "
            f"{item['observable_records']} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def self_test() -> None:
    def numeric_record(
        generated: float,
        native: float,
        comparison: str,
        tolerance: float | None = None,
        limit: float | None = None,
    ) -> dict[str, object]:
        difference = abs(generated - native)
        return {
            "name": "value",
            "generated": generated,
            "native": native,
            "comparison": comparison,
            "relative_tolerance": tolerance,
            "absolute_limit": limit,
            "absolute_difference": difference,
            "relative_difference": difference / abs(native) if native != 0.0 else difference,
            "status": "matched",
        }

    validate_numerical_record("self-test", 1, numeric_record(2.0, 2.0, "exact"))
    validate_numerical_record(
        "self-test", 1, numeric_record(1.004, 1.0, "relative", 5.0e-3)
    )
    validate_numerical_record(
        "self-test", 1, numeric_record(0.5, 1.0e-4, "roundoff_bound", None, 1.0)
    )
    validate_numerical_record(
        "self-test",
        1,
        {
            "name": "status",
            "generated": "PASS",
            "native": "PASS",
            "comparison": "exact",
            "status": "matched",
        },
    )

    rejected = [
        numeric_record(2.0, 1.0, "exact"),
        numeric_record(1.006, 1.0, "relative", 5.0e-3),
        numeric_record(1.01, 1.0e-4, "roundoff_bound", None, 1.0),
        {
            "name": "status",
            "generated": "PASS",
            "native": "FAIL",
            "comparison": "exact",
            "status": "matched",
        },
    ]
    for index, record in enumerate(rejected, 1):
        try:
            validate_numerical_record("self-test", index, record)
        except ManifestError:
            continue
        raise AssertionError(f"invalid differential record {index} was accepted")

    with tempfile.TemporaryDirectory() as directory:
        destination = Path(directory) / "summary.md"
        write_summary(
            destination,
            {
                "status": "matched",
                "suite_count": 0,
                "numerical_records": 0,
                "status_records": 0,
                "observable_records": 0,
                "blas_result_groups": 0,
                "blas_computational_calls": 0,
                "comparison_policies": {},
                "categories": {
                    name: {"suites": 0, "numerical_records": 0, "observable_records": 0}
                    for name in EXPECTED_SUITES
                },
            },
        )
        assert destination.read_text(encoding="utf-8").startswith(
            "# Official BLAS/LAPACK numerical differential\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report_root")
    args = parser.parse_args()
    if args.report_root == "self-test":
        self_test()
        print("official differential manifest self-test passed")
        return 0
    report_root = Path(args.report_root)
    try:
        manifest = build_manifest(report_root)
    except ManifestError as error:
        print(f"official differential manifest failed: {error}", file=sys.stderr)
        return 1
    report_root.mkdir(parents=True, exist_ok=True)
    (report_root / "manifest.json").write_text(
        json.dumps(manifest, allow_nan=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_summary(report_root / "summary.md", manifest)
    print(
        f"matched {manifest['numerical_records']} official numerical records "
        f"across {manifest['suite_count']} BLAS/LAPACK suites"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
