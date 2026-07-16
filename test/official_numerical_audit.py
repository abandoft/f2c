#!/usr/bin/env python3
"""Audit every official BLAS/LAPACK numerical differential record.

The official drivers expose two complementary result surfaces:

* their ordinary transcripts, validated by ``differential_manifest.py``; and
* identity-keyed internal values emitted by computation-preserving trace
  builds, validated here record by record against the native Fortran build.

This program deliberately streams the compressed JSONL artifacts.  The full
Reference LAPACK gate contains millions of records and must not be loaded into
memory merely to prove that every reported difference is internally sound.
"""

from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import TextIO

# Keep every generated/intermediate artifact under build/, including Python's
# cache for the two local validator modules imported below.
sys.dont_write_bytecode = True

from differential_manifest import build_manifest as build_observable_manifest
from exhaustive_result_diff import (
    NumericRecord,
    official_threshold_applies,
    passes_threshold,
    printed_rounding_tolerance,
)
from numerical_coverage import load_baseline


COVERAGE_BASELINE = load_baseline()
EXPECTED_OBSERVABLE = COVERAGE_BASELINE["observable"]
EXPECTED_INTERNAL = COVERAGE_BASELINE["internal"]
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class AuditError(Exception):
    """A differential artifact is incomplete, corrupt, or inconsistent."""


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def numeric_value(value: object, label: str) -> float:
    if isinstance(value, bool):
        raise AuditError(f"{label} is a boolean, not a numerical value")
    if isinstance(value, (int, float)):
        return float(value)
    if value == "nan":
        return math.nan
    if value == "inf":
        return math.inf
    if value == "-inf":
        return -math.inf
    raise AuditError(f"{label} has invalid numerical value {value!r}")


def exact_optional_number(value: object, expected: float | None, label: str) -> None:
    if expected is None:
        if value is not None:
            raise AuditError(f"{label} must be null")
        return
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise AuditError(f"{label} must be numerical")
    if float(value) != expected:
        raise AuditError(f"{label} differs: recorded={value!r}, expected={expected!r}")


def read_side(side: object, label: str) -> NumericRecord | None:
    if side is None:
        return None
    if not isinstance(side, dict):
        raise AuditError(f"{label} is not an object")
    token = side.get("token")
    context = side.get("context")
    line = side.get("line")
    if not isinstance(token, str) or not token:
        raise AuditError(f"{label} has no numerical token")
    if not isinstance(context, str) or not context:
        raise AuditError(f"{label} has no test identity")
    if not isinstance(line, int) or isinstance(line, bool) or line <= 0:
        raise AuditError(f"{label} has an invalid transcript line")
    return NumericRecord(numeric_value(side.get("value"), label), token, context, line)


def expected_classification(
    generated: NumericRecord | None, native: NumericRecord | None
) -> tuple[str, float | None, float | None, float | None]:
    if generated is None:
        if native is None:
            raise AuditError("a record cannot be absent from both builds")
        return "missing_in_generated", None, None, None
    if native is None:
        return "missing_in_native", None, None, None

    generated_value = generated.value
    native_value = native.value
    if math.isnan(generated_value) or math.isnan(native_value):
        if math.isnan(generated_value) and math.isnan(native_value):
            return "both_nan", None, None, None
        if math.isnan(generated_value):
            return "generated_nan", None, None, None
        return "native_nan", None, None, None
    if math.isinf(generated_value) or math.isinf(native_value):
        return (
            "equal" if generated_value == native_value else "different_infinity",
            None,
            None,
            None,
        )

    absolute = abs(generated_value - native_value)
    relative = absolute / abs(native_value) if native_value != 0.0 else absolute
    rounding = printed_rounding_tolerance(
        generated.token
    ) + printed_rounding_tolerance(native.token)
    if absolute == 0.0:
        classification = "equal"
    elif absolute <= rounding:
        classification = "printed_rounding_difference"
    else:
        classification = "finite_difference"
    return classification, absolute, relative, rounding


def validate_side_policy(
    side: object, record: NumericRecord | None, mode: str, threshold: float, label: str
) -> None:
    if record is None:
        return
    assert isinstance(side, dict)
    expected_pass = passes_threshold(record, threshold)
    expected_applies = official_threshold_applies(mode, record)
    if side.get("passes_threshold") is not expected_pass:
        raise AuditError(f"{label} has an inconsistent threshold result")
    if side.get("official_threshold_applies") is not expected_applies:
        raise AuditError(f"{label} has an inconsistent official-threshold policy")


def validate_record(
    detail: object,
    expected_index: int,
    mode: str,
    threshold: float,
) -> tuple[str, NumericRecord | None, NumericRecord | None]:
    if not isinstance(detail, dict):
        raise AuditError(f"record {expected_index} is not a JSON object")
    if detail.get("index") != expected_index:
        raise AuditError(
            f"record index differs: expected={expected_index}, actual={detail.get('index')!r}"
        )
    generated_side = detail.get("generated_c")
    native_side = detail.get("native_fortran")
    generated = read_side(generated_side, f"record {expected_index} generated C")
    native = read_side(native_side, f"record {expected_index} native Fortran")
    classification, absolute, relative, rounding = expected_classification(
        generated, native
    )
    if detail.get("classification") != classification:
        raise AuditError(
            f"record {expected_index} classification differs: "
            f"recorded={detail.get('classification')!r}, expected={classification!r}"
        )
    exact_optional_number(
        detail.get("absolute_difference"), absolute, f"record {expected_index} absolute difference"
    )
    exact_optional_number(
        detail.get("relative_difference"), relative, f"record {expected_index} relative difference"
    )
    exact_optional_number(
        detail.get("printed_rounding_tolerance"),
        rounding,
        f"record {expected_index} printed-rounding tolerance",
    )
    validate_side_policy(
        generated_side,
        generated,
        mode,
        threshold,
        f"record {expected_index} generated C",
    )
    validate_side_policy(
        native_side,
        native,
        mode,
        threshold,
        f"record {expected_index} native Fortran",
    )
    return classification, generated, native


def open_jsonl(path: Path) -> TextIO:
    try:
        return gzip.open(path, "rt", encoding="utf-8")
    except OSError as error:
        raise AuditError(f"cannot open {path}: {error}") from error


def validate_transcript(
    transcript_root: Path, metadata: object, label: str
) -> dict[str, object]:
    if not isinstance(metadata, dict):
        raise AuditError(f"{label} transcript metadata is absent")
    filename = metadata.get("file")
    expected_digest = metadata.get("sha256")
    if not isinstance(filename, str) or not filename:
        raise AuditError(f"{label} transcript filename is invalid")
    if not isinstance(expected_digest, str) or SHA256.fullmatch(expected_digest) is None:
        raise AuditError(f"{label} transcript SHA-256 is invalid")
    path = transcript_root / filename
    if not path.is_file():
        raise AuditError(f"{label} transcript is missing: {path}")
    actual_digest = file_sha256(path)
    if actual_digest != expected_digest:
        raise AuditError(
            f"{label} transcript SHA-256 differs: expected={expected_digest}, "
            f"actual={actual_digest}"
        )
    return {"file": filename, "sha256": actual_digest, "bytes": path.stat().st_size}


def validate_report(report_path: Path, report_root: Path) -> dict[str, object]:
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AuditError(f"cannot read {report_path}: {error}") from error
    if not isinstance(report, dict) or report.get("schema_version") != 1:
        raise AuditError(f"{report_path} is not a schema-v1 report")
    mode = report.get("mode")
    if mode not in {"lin", "rfp", "eig", "ec", "balance", "blas", "blas1"}:
        raise AuditError(f"{report_path} has invalid mode {mode!r}")
    threshold_value = report.get("threshold")
    if isinstance(threshold_value, bool) or not isinstance(threshold_value, (int, float)):
        raise AuditError(f"{report_path} has invalid threshold")
    threshold = float(threshold_value)
    if threshold <= 0.0 or not math.isfinite(threshold):
        raise AuditError(f"{report_path} has non-positive or non-finite threshold")
    record_name = report.get("record_report")
    if not isinstance(record_name, str) or not record_name:
        raise AuditError(f"{report_path} has no per-result record artifact")
    record_path = report_root / record_name
    if not record_path.is_file():
        raise AuditError(f"per-result record artifact is missing: {record_path}")

    classifications: Counter[str] = Counter()
    semantic_digest = hashlib.sha256()
    generated_records = 0
    native_records = 0
    matched_records = 0
    generated_only = 0
    native_only = 0
    nonzero_finite = 0
    substantive_finite = 0
    generated_nan = 0
    native_nan = 0
    generated_failures = 0
    native_failures = 0
    generated_threshold_regressions = 0
    generated_diagnostic_exceedances = 0
    native_diagnostic_exceedances = 0
    try:
        with open_jsonl(record_path) as source:
            for index, line in enumerate(source, 1):
                try:
                    detail = json.loads(line)
                except json.JSONDecodeError as error:
                    raise AuditError(
                        f"{record_path} line {index} is invalid JSON: {error}"
                    ) from error
                classification, generated, native = validate_record(
                    detail, index, str(mode), threshold
                )
                classifications[classification] += 1
                generated_records += int(generated is not None)
                native_records += int(native is not None)
                generated_only += int(generated is not None and native is None)
                native_only += int(generated is None and native is not None)
                if generated is None or native is None:
                    semantic_digest.update(
                        (
                            f"{index}\0{generated.token if generated else '-'}\0"
                            f"{native.token if native else '-'}\0{classification}\n"
                        ).encode("ascii")
                    )
                    continue
                matched_records += 1
                semantic_digest.update(
                    (
                        f"{index}\0{generated.value.hex()}\0{native.value.hex()}\0"
                        f"{classification}\n"
                    ).encode("ascii")
                )
                generated_is_nan = math.isnan(generated.value)
                native_is_nan = math.isnan(native.value)
                generated_nan += int(generated_is_nan)
                native_nan += int(native_is_nan)
                generated_pass = passes_threshold(generated, threshold)
                native_pass = passes_threshold(native, threshold)
                generated_applies = official_threshold_applies(str(mode), generated)
                native_applies = official_threshold_applies(str(mode), native)
                generated_failures += int(generated_applies and not generated_pass)
                native_failures += int(native_applies and not native_pass)
                generated_threshold_regressions += int(
                    generated_applies
                    and not generated_pass
                    and native_pass
                    and not generated_is_nan
                )
                generated_diagnostic_exceedances += int(
                    not generated_applies and not generated_pass
                )
                native_diagnostic_exceedances += int(
                    not native_applies and not native_pass
                )
                if classification in {
                    "printed_rounding_difference",
                    "finite_difference",
                }:
                    nonzero_finite += 1
                substantive_finite += int(classification == "finite_difference")
    except OSError as error:
        raise AuditError(f"cannot read {record_path}: {error}") from error

    records = sum(classifications.values())
    expected_fields = {
        "records": records,
        "generated_records": generated_records,
        "native_records": native_records,
        "matched_records": matched_records,
        "generated_only_records": generated_only,
        "native_only_records": native_only,
        "nonzero_finite_differences": nonzero_finite,
        "finite_differences_beyond_printed_precision": substantive_finite,
        "generated_nan_records": generated_nan,
        "native_nan_records": native_nan,
        "generated_threshold_failures": generated_failures,
        "native_threshold_failures": native_failures,
        "generated_threshold_regressions": generated_threshold_regressions,
        "generated_diagnostic_threshold_exceedances": generated_diagnostic_exceedances,
        "native_diagnostic_threshold_exceedances": native_diagnostic_exceedances,
    }
    for field, expected in expected_fields.items():
        if report.get(field) != expected:
            raise AuditError(
                f"{report_path} field {field} differs: "
                f"recorded={report.get(field)!r}, recomputed={expected}"
            )
    if report.get("record_stream_sha256") != semantic_digest.hexdigest():
        raise AuditError(f"{report_path} semantic record-stream SHA-256 differs")

    transcripts = report.get("transcripts")
    if not isinstance(transcripts, dict):
        raise AuditError(f"{report_path} has no transcript provenance")
    transcript_root = report_root.parent / "transcript"
    generated_transcript = validate_transcript(
        transcript_root, transcripts.get("generated_c"), "generated C"
    )
    native_transcript = validate_transcript(
        transcript_root, transcripts.get("native_fortran"), "native Fortran"
    )
    return {
        "suite": report_path.stem,
        "mode": mode,
        "report": report_path.name,
        "report_sha256": file_sha256(report_path),
        "record_report": record_name,
        "record_report_sha256": file_sha256(record_path),
        "record_report_bytes": record_path.stat().st_size,
        "record_stream_sha256": semantic_digest.hexdigest(),
        "records": records,
        "generated_records": generated_records,
        "native_records": native_records,
        "matched_records": matched_records,
        "generated_only_records": generated_only,
        "native_only_records": native_only,
        "classifications": dict(sorted(classifications.items())),
        "finite_differences_beyond_printed_precision": substantive_finite,
        "generated_threshold_regressions": generated_threshold_regressions,
        "correctness_status": report.get("correctness_status"),
        "transcripts": {
            "generated_c": generated_transcript,
            "native_fortran": native_transcript,
        },
    }


def validate_internal_root(
    label: str, report_root: Path
) -> tuple[dict[str, object], list[dict[str, object]]]:
    expected = EXPECTED_INTERNAL[label]
    paths = sorted(
        path for path in report_root.glob("*.json") if path.name != "manifest.json"
    )
    if len(paths) != expected["suites"]:
        raise AuditError(
            f"{label} suite coverage differs: actual={len(paths)}, "
            f"expected={expected['suites']}"
        )
    entries = [validate_report(path, report_root) for path in paths]
    totals = {
        "suites": len(entries),
        "records": sum(int(entry["records"]) for entry in entries),
        "generated_records": sum(
            int(entry["generated_records"]) for entry in entries
        ),
        "native_records": sum(int(entry["native_records"]) for entry in entries),
        "matched_records": sum(int(entry["matched_records"]) for entry in entries),
        "generated_only_records": sum(
            int(entry["generated_only_records"]) for entry in entries
        ),
        "native_only_records": sum(
            int(entry["native_only_records"]) for entry in entries
        ),
        "finite_differences_beyond_printed_precision": sum(
            int(entry["finite_differences_beyond_printed_precision"])
            for entry in entries
        ),
        "generated_threshold_regressions": sum(
            int(entry["generated_threshold_regressions"]) for entry in entries
        ),
        "regressed_suites": sum(
            int(entry["correctness_status"] == "regressed") for entry in entries
        ),
    }
    for field, expected_value in expected.items():
        if totals[field] != expected_value:
            raise AuditError(
                f"{label} {field} differs: actual={totals[field]}, "
                f"expected={expected_value}"
            )

    stored_path = report_root / "manifest.json"
    if not stored_path.is_file():
        raise AuditError(f"{label} exhaustive manifest is missing: {stored_path}")
    stored = json.loads(stored_path.read_text(encoding="utf-8"))
    for field in (
        "suites",
        "records",
        "generated_records",
        "native_records",
        "generated_only_records",
        "native_only_records",
        "generated_threshold_regressions",
    ):
        if stored.get(field) != totals[field]:
            raise AuditError(
                f"{label} stored manifest field {field} differs: "
                f"stored={stored.get(field)!r}, recomputed={totals[field]}"
            )
    totals["manifest"] = stored_path.name
    totals["manifest_sha256"] = file_sha256(stored_path)
    return totals, entries


def build_audit(
    observable_root: Path, blas_root: Path, lapack_root: Path
) -> dict[str, object]:
    observable = build_observable_manifest(observable_root)
    for field, expected in EXPECTED_OBSERVABLE.items():
        if observable.get(field) != expected:
            raise AuditError(
                f"official observable {field} differs: "
                f"actual={observable.get(field)!r}, expected={expected}"
            )
    stored_observable = observable_root / "manifest.json"
    if not stored_observable.is_file():
        raise AuditError(f"official observable manifest is missing: {stored_observable}")
    stored = json.loads(stored_observable.read_text(encoding="utf-8"))
    if stored != observable:
        raise AuditError("stored official observable manifest differs from recomputed data")

    blas_totals, blas_entries = validate_internal_root("blas", blas_root)
    lapack_totals, lapack_entries = validate_internal_root("lapack", lapack_root)
    internal_records = int(blas_totals["records"]) + int(lapack_totals["records"])
    matched_records = int(blas_totals["matched_records"]) + int(
        lapack_totals["matched_records"]
    )
    finite_differences = int(
        blas_totals["finite_differences_beyond_printed_precision"]
    ) + int(lapack_totals["finite_differences_beyond_printed_precision"])
    threshold_regressions = int(
        blas_totals["generated_threshold_regressions"]
    ) + int(lapack_totals["generated_threshold_regressions"])
    regressed_suites = int(blas_totals["regressed_suites"]) + int(
        lapack_totals["regressed_suites"]
    )
    return {
        "schema_version": 1,
        "status": "passed"
        if threshold_regressions == 0 and regressed_suites == 0
        else "regressed",
        "oracle": "native Fortran build from the pinned Reference LAPACK source tree",
        "observable": {
            **{field: observable[field] for field in EXPECTED_OBSERVABLE},
            "manifest": stored_observable.name,
            "manifest_sha256": file_sha256(stored_observable),
        },
        "internal": {
            "suite_count": int(blas_totals["suites"])
            + int(lapack_totals["suites"]),
            "records": internal_records,
            "matched_records": matched_records,
            "generated_only_records": int(blas_totals["generated_only_records"])
            + int(lapack_totals["generated_only_records"]),
            "native_only_records": int(blas_totals["native_only_records"])
            + int(lapack_totals["native_only_records"]),
            "finite_differences_beyond_printed_precision": finite_differences,
            "generated_threshold_regressions": threshold_regressions,
            "regressed_suites": regressed_suites,
            "blas": blas_totals,
            "lapack": lapack_totals,
        },
        "record_streams": blas_entries + lapack_entries,
    }


def write_summary(path: Path, audit: dict[str, object]) -> None:
    observable = audit["observable"]
    internal = audit["internal"]
    assert isinstance(observable, dict) and isinstance(internal, dict)
    lines = [
        "# Official BLAS/LAPACK per-result numerical differential",
        "",
        "Every ordinary official numerical record and every identity-keyed internal result is",
        "compared with a native Fortran build from the same pinned source tree. Raw floating-point",
        "differences remain visible; the correctness status separately reports official-threshold",
        "regressions.",
        "",
        f"- Status: `{audit['status']}`",
        f"- Official suites: {observable['suite_count']}",
        f"- Official observable numerical records: {observable['numerical_records']}",
        f"- Official observable status records: {observable['status_records']}",
        f"- Internal traced suites: {internal['suite_count']}",
        f"- Internal per-result records: {internal['records']}",
        f"- Paired generated-C/native-Fortran records: {internal['matched_records']}",
        f"- Generated-only/native-only records: {internal['generated_only_records']}/"
        f"{internal['native_only_records']}",
        "- Finite differences beyond transcript printing precision: "
        f"{internal['finite_differences_beyond_printed_precision']}",
        f"- Generated official-threshold regressions: {internal['generated_threshold_regressions']}",
        "",
        "Each compressed JSONL record retains the test identity, both values and source lines, "
        "absolute/relative difference, printing tolerance, classification, and threshold policy.",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def self_test() -> None:
    generated = {
        "value": 1.25,
        "token": "1.2500",
        "context": "DEV 3 7",
        "line": 11,
        "passes_threshold": True,
        "official_threshold_applies": True,
    }
    native = dict(generated)
    detail = {
        "index": 1,
        "classification": "equal",
        "generated_c": generated,
        "native_fortran": native,
        "absolute_difference": 0.0,
        "relative_difference": 0.0,
        "printed_rounding_tolerance": 2.0 * printed_rounding_tolerance("1.2500"),
    }
    classification, _, _ = validate_record(detail, 1, "eig", 20.0)
    assert classification == "equal"
    missing = {
        "index": 2,
        "classification": "missing_in_generated",
        "generated_c": None,
        "native_fortran": native,
        "absolute_difference": None,
        "relative_difference": None,
        "printed_rounding_tolerance": None,
    }
    classification, _, _ = validate_record(missing, 2, "eig", 20.0)
    assert classification == "missing_in_generated"
    forged = dict(detail)
    forged["absolute_difference"] = 1.0
    try:
        validate_record(forged, 1, "eig", 20.0)
    except AuditError:
        pass
    else:
        raise AssertionError("forged per-result difference was accepted")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("observable", nargs="?", type=Path)
    parser.add_argument("blas", nargs="?", type=Path)
    parser.add_argument("lapack", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("official numerical per-result audit self-test passed")
        return 0
    if args.observable is None or args.blas is None or args.lapack is None:
        parser.error("observable, BLAS and LAPACK report directories are required")
    try:
        audit = build_audit(args.observable, args.blas, args.lapack)
    except (AuditError, OSError, json.JSONDecodeError) as error:
        print(f"official numerical per-result audit failed: {error}", file=sys.stderr)
        return 1
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(audit, allow_nan=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    if args.summary is not None:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        write_summary(args.summary, audit)
    internal = audit["internal"]
    assert isinstance(internal, dict)
    print(
        f"audited all {internal['records']} internal numerical records across "
        f"{internal['suite_count']} suites; paired={internal['matched_records']}, "
        f"generated-only={internal['generated_only_records']}, "
        f"native-only={internal['native_only_records']}"
    )
    return int(audit["status"] != "passed")


if __name__ == "__main__":
    raise SystemExit(main())
