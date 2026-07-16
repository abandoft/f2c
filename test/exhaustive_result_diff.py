#!/usr/bin/env python3
"""Compare every numerical ratio emitted by instrumented LAPACK drivers."""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
from dataclasses import dataclass
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import tempfile


NUMBER_TEXT = (
    r"[+-]?(?:inf(?:inity)?|nan|(?:\d+(?:\.\d*)?|\.\d+)(?:[eEdD][+-]?\d+)?|"
    r"(?:\d+(?:\.\d*)?|\.\d+)[+-]\d+)"
)
NUMBER = re.compile(rf"^{NUMBER_TEXT}$", re.IGNORECASE)
NATIVE_RESULT = re.compile(rf"(?:=|\bis)\s*({NUMBER_TEXT})\s*$", re.IGNORECASE)
SUMMARY = re.compile(r"^[SDCZ][A-Z0-9]*\s+\d+\s+\d+$")
LIN_PATH = re.compile(r"^[SDCZ][A-Z0-9]{2}$")
RFP_PATH = re.compile(r"^[SDCZ]PF$")
EIG_PATH = re.compile(r"^[A-Z0-9]{3}$")
EIG_HEADER = re.compile(r"^([A-Z0-9]{3})(?:(\s+--)|(:)|$)")
COLON_RESULT_PATHS = {"GLM", "GQR", "GRQ", "GSV", "CSD", "LSE"}
KEYED_MODES = {"eig", "ec", "balance", "blas", "blas1"}
NATIVE_TEST_TOTAL = re.compile(
    r"(?:out\s+of\s+|\(\s*)(\d+)\s+tests?\s+(?:failed\s+to\s+pass|run\s*\))",
    re.IGNORECASE,
)


class Difference(Exception):
    pass


@dataclass(frozen=True)
class NumericRecord:
    value: float
    token: str
    context: str
    line: int


def parse_number(token: str) -> float:
    normalized = token.strip().replace("d", "e").replace("D", "E")
    if "e" not in normalized.lower():
        match = re.fullmatch(r"([+-]?(?:\d+(?:\.\d*)?|\.\d+))([+-]\d+)", normalized)
        if match:
            normalized = f"{match.group(1)}e{match.group(2)}"
    return float(normalized)


def native_records(path: Path, mode: str | None = None) -> list[NumericRecord]:
    records: list[NumericRecord] = []
    with path.open(encoding="utf-8", errors="replace") as source:
        for line_number, line in enumerate(source, 1):
            match = NATIVE_RESULT.search(line)
            if match is None:
                continue
            context = line[: match.start()].strip()
            lowered = context.lower()
            if "threshold" in lowered:
                continue
            if (
                "test" not in lowered
                and "ratio" not in lowered
                and "result" not in lowered
            ):
                continue
            token = match.group(1)
            value = parse_number(token)
            if mode == "eig" and value < 0.0:
                continue
            records.append(
                NumericRecord(value, token, context, line_number)
            )
    return records


def native_test_total(path: Path) -> int:
    total = 0
    with path.open(encoding="utf-8", errors="replace") as source:
        for line in source:
            match = NATIVE_TEST_TOTAL.search(line)
            if match is not None:
                total += int(match.group(1))
    return total


def generated_records(path: Path, mode: str) -> list[NumericRecord]:
    path_pattern = (
        RFP_PATH
        if mode == "rfp"
        else EIG_PATH
        if mode in KEYED_MODES
        else LIN_PATH
    )
    records: list[NumericRecord] = []
    started = mode in {"ec", "balance", "blas", "blas1"}
    current_path = mode.upper() if started else ""
    with path.open(encoding="utf-8", errors="replace") as source:
        for line_number, line in enumerate(source, 1):
            stripped = line.strip()
            if mode == "eig":
                header = EIG_HEADER.match(stripped)
                if header is not None and (
                    header.group(3) is None
                    or header.group(1) in COLON_RESULT_PATHS
                ) and header.group(1) != "SVD":
                    started = True
                    current_path = header.group(1)
            if not started:
                if path_pattern.fullmatch(stripped):
                    started = True
                    current_path = stripped
                continue
            if path_pattern.fullmatch(stripped):
                current_path = stripped
                continue
            if not stripped or SUMMARY.fullmatch(stripped):
                continue
            tokens = stripped.split()
            if mode in KEYED_MODES:
                marker_index = next(
                    (index for index, token in enumerate(tokens) if "@" in token),
                    None,
                )
                if marker_index is None or marker_index + 1 >= len(tokens):
                    continue
                token = tokens[marker_index + 1]
                if NUMBER.fullmatch(token) is None:
                    continue
                context_tokens = tokens[:marker_index]
                marker_context = tokens[marker_index].replace("@", "")
                if marker_context:
                    context_tokens.append(marker_context)
                context_tokens.extend(tokens[marker_index + 2 :])
                if mode == "eig" and any(
                    len(context_token) > 1
                    and NUMBER.fullmatch(context_token) is None
                    and re.search(r"[A-Za-z]", context_token) is not None
                    for context_token in context_tokens
                ):
                    continue
                normalized_context: list[str] = []
                for context_token in context_tokens:
                    if (
                        normalized_context
                        and normalized_context[-1].isalpha()
                        and context_token.isalpha()
                    ):
                        normalized_context[-1] += context_token
                    else:
                        normalized_context.append(context_token)
                if mode == "eig" and (
                    current_path in {"CBD", "ZBD"}
                    and len(normalized_context) == 8
                    and normalized_context[2] == "16"
                    and normalized_context[-1] in {"6", "7"}
                ):
                    # xCHKBD counts five tests for the synthetic bidiagonal
                    # matrix type.  Results 6 and 7 are computed diagnostics,
                    # but are deliberately excluded from LAPACK's NRUN.
                    continue
                context = " ".join(normalized_context)
            else:
                if len(tokens) < 4 or NUMBER.fullmatch(tokens[-1]) is None:
                    continue
                token = tokens[-1]
                context = " ".join(tokens[:-1])
            value = parse_number(token)
            if mode in {"eig", "ec"} and value < 0.0:
                continue
            if mode in KEYED_MODES:
                context = f"{current_path} {context}"
            records.append(
                NumericRecord(value, token, context, line_number)
            )
    if not records and mode not in KEYED_MODES:
        # Current generated C preserves the official formatted result labels.
        # Older translators emitted a list-directed stream after instrumentation,
        # so keep that parser above and use the native-style parser as a strict
        # fallback when no legacy records are present.
        return native_records(path, mode)
    return records


def aligned_records(
    generated: list[NumericRecord], native: list[NumericRecord], mode: str
) -> list[tuple[NumericRecord | None, NumericRecord | None]]:
    if mode not in KEYED_MODES:
        if len(generated) != len(native):
            raise Difference(
                f"record count differs: generated={len(generated)}, native={len(native)}"
            )
        return list(zip(generated, native))

    by_context: dict[str, deque[tuple[int, NumericRecord]]] = defaultdict(deque)
    for index, record in enumerate(generated):
        by_context[record.context].append((index, record))
    consumed: set[int] = set()
    pairs: list[tuple[NumericRecord | None, NumericRecord | None]] = []
    for native_record in native:
        candidates = by_context[native_record.context]
        if candidates:
            generated_index, generated_record = candidates.popleft()
            consumed.add(generated_index)
            pairs.append((generated_record, native_record))
        else:
            pairs.append((None, native_record))
    pairs.extend(
        (record, None)
        for index, record in enumerate(generated)
        if index not in consumed
    )
    return pairs


def printed_rounding_tolerance(token: str) -> float:
    normalized = token.lower().replace("d", "e")
    if "e" not in normalized:
        exponent_match = re.fullmatch(
            r"([+-]?(?:\d+(?:\.\d*)?|\.\d+))([+-]\d+)", normalized
        )
        if exponent_match:
            normalized = f"{exponent_match.group(1)}e{exponent_match.group(2)}"
    mantissa, _, exponent_text = normalized.partition("e")
    exponent = int(exponent_text) if exponent_text else 0
    fractional_digits = len(mantissa.partition(".")[2])
    return 0.5000001 * (10.0 ** (exponent - fractional_digits))


def json_value(value: float) -> float | str:
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "-inf" if value < 0.0 else "inf"
    return value


def passes_threshold(record: NumericRecord, threshold: float) -> bool:
    """Mirror the official `IF (ratio >= threshold)` reporting branch.

    IEEE comparisons with NaN are false, so LAPACK does not report such a
    record as an above-threshold failure.  Negative infinity likewise stays
    below every finite threshold, while positive infinity fails.  Finite
    transcript values retain half-a-print-unit tolerance because the audit
    consumes formatted output rather than the pre-format binary value.
    """
    if math.isnan(record.value) or record.value == -math.inf:
        return True
    if record.value == math.inf:
        return False
    tolerance = printed_rounding_tolerance(record.token)
    return record.value < threshold + tolerance


def official_threshold_applies(mode: str, record: NumericRecord) -> bool:
    """Return whether the official driver gates this traced ratio.

    xGET37/xGET38 retain RMAX(3) as a useful condition-estimation diagnostic,
    but xCHKEC deliberately gates only RMAX(1:2).  Balance drivers expose no
    per-example threshold at all.  Both kinds of values remain in the audit;
    they must not be counted as official threshold failures.
    """
    if mode in {"balance", "blas1"}:
        return False
    if mode != "ec":
        return True
    fields = record.context.split()
    if len(fields) < 4:
        raise Difference(f"invalid EC identity: {record.context!r}")
    routine = fields[1].upper()
    try:
        site = int(fields[3])
    except ValueError as error:
        raise Difference(f"invalid EC trace site: {record.context!r}") from error
    if routine.endswith("GET37") and site in {3, 4}:
        return False
    if routine.endswith("GET38") and site in {4, 5}:
        return False
    return True


def find_correctness_regressions(
    generated_nan: int,
    native_nan: int,
    generated_threshold_regressions: int,
    expected_generated_nan: int,
    expected_native_nan: int,
) -> list[str]:
    regressions: list[str] = []
    if generated_nan != expected_generated_nan:
        regressions.append(
            f"generated NaN policy changed: expected={expected_generated_nan}, "
            f"actual={generated_nan}"
        )
    if native_nan != expected_native_nan:
        regressions.append(
            f"native NaN baseline changed: expected={expected_native_nan}, "
            f"actual={native_nan}"
        )
    if generated_threshold_regressions:
        regressions.append(
            "generated official threshold failures absent from the native "
            f"baseline={generated_threshold_regressions}"
        )
    return regressions


def compare(
    mode: str,
    generated_path: Path,
    native_path: Path,
    threshold: float,
    expected_generated_nan: int,
    expected_native_nan: int,
    records_path: Path | None,
) -> dict[str, object]:
    generated = generated_records(generated_path, mode)
    native = (
        generated_records(native_path, mode)
        if mode in KEYED_MODES
        else native_records(native_path, mode)
    )
    if mode in KEYED_MODES and (not generated or not native):
        raise Difference(
            f"empty keyed numerical stream: generated={len(generated)}, "
            f"native={len(native)}"
        )
    pairs = aligned_records(generated, native, mode)
    expected_records = (
        native_test_total(native_path) if mode in {"eig", "ec"} else None
    )
    if mode == "eig" and expected_records is not None and len(native) > expected_records:
        raise Difference(
            f"native numerical records exceed official test total: "
            f"records={len(native)}, official total={expected_records}"
        )

    digest = hashlib.sha256()
    maximum_absolute = 0.0
    maximum_relative = 0.0
    nonzero_differences = 0
    substantive_finite_differences = 0
    generated_nan = 0
    native_nan = 0
    both_nan = 0
    generated_failures = 0
    native_failures = 0
    generated_threshold_regressions = 0
    generated_diagnostic_exceedances = 0
    native_diagnostic_exceedances = 0
    generated_only = 0
    native_only = 0
    matched = 0
    record_file = None
    if records_path is not None:
        records_path.parent.mkdir(parents=True, exist_ok=True)
        record_file = gzip.open(records_path, "wt", encoding="utf-8", newline="\n")
    try:
        for index, (generated_record, native_record) in enumerate(pairs, 1):
            if generated_record is None or native_record is None:
                generated_only += int(native_record is None)
                native_only += int(generated_record is None)
                classification = (
                    "missing_in_native" if native_record is None else "missing_in_generated"
                )
                digest.update(
                    (
                        f"{index}\0{generated_record.token if generated_record else '-'}\0"
                        f"{native_record.token if native_record else '-'}\0{classification}\n"
                    ).encode("ascii")
                )
                if record_file is not None:
                    detail = {
                        "index": index,
                        "classification": classification,
                        "generated_c": None
                        if generated_record is None
                        else {
                            "value": json_value(generated_record.value),
                            "token": generated_record.token,
                            "context": generated_record.context,
                            "line": generated_record.line,
                            "passes_threshold": passes_threshold(
                                generated_record, threshold
                            ),
                            "official_threshold_applies":
                            official_threshold_applies(mode, generated_record),
                        },
                        "native_fortran": None
                        if native_record is None
                        else {
                            "value": json_value(native_record.value),
                            "token": native_record.token,
                            "context": native_record.context,
                            "line": native_record.line,
                            "passes_threshold": passes_threshold(native_record, threshold),
                            "official_threshold_applies":
                            official_threshold_applies(mode, native_record),
                        },
                        "absolute_difference": None,
                        "relative_difference": None,
                        "printed_rounding_tolerance": None,
                    }
                    record_file.write(
                        json.dumps(detail, allow_nan=False, sort_keys=True) + "\n"
                    )
                continue
            matched += 1
            generated_value = generated_record.value
            native_value = native_record.value
            generated_is_nan = math.isnan(generated_value)
            native_is_nan = math.isnan(native_value)
            generated_nan += int(generated_is_nan)
            native_nan += int(native_is_nan)
            both_nan += int(generated_is_nan and native_is_nan)
            generated_passes = passes_threshold(generated_record, threshold)
            native_passes = passes_threshold(native_record, threshold)
            generated_applies = official_threshold_applies(mode, generated_record)
            native_applies = official_threshold_applies(mode, native_record)
            generated_failures += int(generated_applies and not generated_passes)
            native_failures += int(native_applies and not native_passes)
            generated_threshold_regressions += int(
                generated_applies
                and not generated_passes
                and native_passes
                and not generated_is_nan
            )
            generated_diagnostic_exceedances += int(
                not generated_applies and not generated_passes
            )
            native_diagnostic_exceedances += int(
                not native_applies and not native_passes
            )
            absolute: float | None = None
            relative: float | None = None
            rounding_tolerance: float | None = None
            classification = "equal"
            if generated_is_nan or native_is_nan:
                if generated_is_nan and native_is_nan:
                    classification = "both_nan"
                elif generated_is_nan:
                    classification = "generated_nan"
                else:
                    classification = "native_nan"
            elif math.isinf(generated_value) or math.isinf(native_value):
                if generated_value != native_value:
                    classification = "different_infinity"
            else:
                absolute = abs(generated_value - native_value)
                scale = abs(native_value)
                relative = absolute / scale if scale != 0.0 else absolute
                rounding_tolerance = printed_rounding_tolerance(
                    generated_record.token
                ) + printed_rounding_tolerance(native_record.token)
                maximum_absolute = max(maximum_absolute, absolute)
                maximum_relative = max(maximum_relative, relative)
                if absolute != 0.0:
                    nonzero_differences += 1
                    if absolute <= rounding_tolerance:
                        classification = "printed_rounding_difference"
                    else:
                        substantive_finite_differences += 1
                        classification = "finite_difference"
            digest.update(
                (
                    f"{index}\0{generated_value.hex()}\0{native_value.hex()}\0"
                    f"{classification}\n"
                ).encode("ascii")
            )
            if record_file is not None:
                detail = {
                    "index": index,
                    "classification": classification,
                    "generated_c": {
                        "value": json_value(generated_value),
                        "token": generated_record.token,
                        "context": generated_record.context,
                        "line": generated_record.line,
                        "passes_threshold": generated_passes,
                        "official_threshold_applies": generated_applies,
                    },
                    "native_fortran": {
                        "value": json_value(native_value),
                        "token": native_record.token,
                        "context": native_record.context,
                        "line": native_record.line,
                        "passes_threshold": native_passes,
                        "official_threshold_applies": native_applies,
                    },
                    "absolute_difference": absolute,
                    "relative_difference": relative,
                    "printed_rounding_tolerance": rounding_tolerance,
                }
                record_file.write(json.dumps(detail, allow_nan=False, sort_keys=True))
                record_file.write("\n")
    finally:
        if record_file is not None:
            record_file.close()

    differences: list[str] = []
    if generated_only or native_only:
        differences.append(
            f"unmatched records: generated-only={generated_only}, "
            f"native-only={native_only}"
        )
    if substantive_finite_differences:
        differences.append(
            "finite differences beyond printed precision="
            f"{substantive_finite_differences}"
        )
    if generated_nan != expected_generated_nan:
        differences.append(
            f"generated NaN count: expected={expected_generated_nan}, "
            f"actual={generated_nan}"
        )
    if native_nan != expected_native_nan:
        differences.append(
            f"native NaN count: expected={expected_native_nan}, actual={native_nan}"
        )
    if generated_failures:
        differences.append(
            f"generated official threshold failures={generated_failures}"
        )
    if native_failures:
        differences.append(
            f"native official threshold failures={native_failures}"
        )
    correctness_regressions = find_correctness_regressions(
        generated_nan,
        native_nan,
        generated_threshold_regressions,
        expected_generated_nan,
        expected_native_nan,
    )

    return {
        "schema_version": 1,
        "status": "different"
        if differences
        else "matched_with_explicit_nan_policy"
        if expected_generated_nan or expected_native_nan
        else "matched",
        "differences": differences,
        "correctness_status":
        "regressed" if correctness_regressions else "passed",
        "correctness_regressions": correctness_regressions,
        "mode": mode,
        "threshold": threshold,
        "records": len(pairs),
        "generated_records": len(generated),
        "native_records": len(native),
        "matched_records": matched,
        "generated_only_records": generated_only,
        "native_only_records": native_only,
        "official_test_total": expected_records,
        "official_tests_without_nonnegative_ratio":
        expected_records - len(native)
        if mode == "eig" and expected_records is not None
        else None,
        "nonzero_finite_differences": nonzero_differences,
        "finite_differences_beyond_printed_precision":
        substantive_finite_differences,
        "maximum_absolute_difference": maximum_absolute,
        "maximum_relative_difference": maximum_relative,
        "generated_nan_records": generated_nan,
        "native_nan_records": native_nan,
        "both_nan_records": both_nan,
        "generated_threshold_failures": generated_failures,
        "native_threshold_failures": native_failures,
        "generated_threshold_regressions": generated_threshold_regressions,
        "generated_diagnostic_threshold_exceedances":
        generated_diagnostic_exceedances,
        "native_diagnostic_threshold_exceedances":
        native_diagnostic_exceedances,
        "record_stream_sha256": digest.hexdigest(),
        "record_report": records_path.name if records_path is not None else None,
        "transcripts": {
            "generated_c": {
                "file": generated_path.name,
                "sha256": hashlib.sha256(generated_path.read_bytes()).hexdigest(),
            },
            "native_fortran": {
                "file": native_path.name,
                "sha256": hashlib.sha256(native_path.read_bytes()).hexdigest(),
            },
        },
    }


def self_test() -> None:
    assert parse_number("0.222507-307") == 0.222507e-307
    assert math.isclose(printed_rounding_tolerance("0.1234E+02"), 0.005000001)
    assert json_value(float("nan")) == "nan"
    assert passes_threshold(NumericRecord(29.0, "29.000", "test", 1), 30.0)
    assert passes_threshold(NumericRecord(float("nan"), "nan", "test", 1), 30.0)
    assert passes_threshold(NumericRecord(-math.inf, "-inf", "test", 1), 30.0)
    assert not passes_threshold(NumericRecord(math.inf, "inf", "test", 1), 30.0)
    assert not official_threshold_applies(
        "ec", NumericRecord(1.0, "1", "EC SGET37 31 3", 1)
    )
    assert official_threshold_applies(
        "ec", NumericRecord(1.0, "1", "EC SGET37 31 2", 1)
    )
    assert not official_threshold_applies(
        "ec", NumericRecord(1.0, "1", "EC ZGET38 55 5", 1)
    )
    assert not official_threshold_applies(
        "balance", NumericRecord(1.0, "1", "BALANCE SCHKBL 1 1", 1)
    )
    assert not find_correctness_regressions(234, 0, 0, 234, 0)
    assert not find_correctness_regressions(0, 0, 0, 0, 0)
    assert find_correctness_regressions(0, 0, 1, 0, 0)
    assert find_correctness_regressions(0, 1, 0, 0, 0)
    assert NATIVE_TEST_TOTAL.search("SHS: 3 out of  2016 tests failed to pass")
    assert NATIVE_TEST_TOTAL.search("passed the threshold (  48 tests run)")
    assert EIG_HEADER.match("SST -- Real Symmetric eigenvalue problem")
    assert EIG_HEADER.match("GLM: GLM Problem")
    with tempfile.TemporaryDirectory() as directory:
        formatted = Path(directory) / "formatted.out"
        formatted.write_text(
            "SGE: General dense matrices\n"
            " M = 1, N = 1, type 1, test( 1) = 0.125\n",
            encoding="utf-8",
        )
        formatted_records = generated_records(formatted, "lin")
        assert len(formatted_records) == 1
        assert formatted_records[0].value == 0.125
    generated = [
        NumericRecord(1.0, "1", "DEV 1 1", 1),
        NumericRecord(2.0, "2", "DEV 1 2", 2),
    ]
    native = [NumericRecord(2.0, "2", "DEV 1 2", 1)]
    assert aligned_records(generated, native, "eig") == [
        (generated[1], native[0]),
        (generated[0], None),
    ]
    assert aligned_records(generated, native, "ec") == [
        (generated[1], native[0]),
        (generated[0], None),
    ]
    assert aligned_records(generated, native, "balance") == [
        (generated[1], native[0]),
        (generated[0], None),
    ]


def build_manifest(report_directory: Path) -> dict[str, object]:
    reports: list[dict[str, object]] = []
    for path in sorted(report_directory.glob("*.json")):
        if path.name == "manifest.json":
            continue
        report = json.loads(path.read_text(encoding="utf-8"))
        if report.get("schema_version") != 1 or report.get("status") not in {
            "matched",
            "matched_with_explicit_nan_policy",
            "different",
        }:
            raise Difference(f"invalid exhaustive report: {path}")
        if report.get("correctness_status") not in {"passed", "regressed"}:
            raise Difference(f"missing correctness status in exhaustive report: {path}")
        record_report = report.get("record_report")
        if record_report is not None and not (report_directory / record_report).is_file():
            raise Difference(f"missing per-record artifact for {path.name}: {record_report}")
        reports.append(report)
    if not reports:
        raise Difference(f"no exhaustive reports found in {report_directory}")
    return {
        "schema_version": 1,
        "status": "different"
        if any(report["status"] == "different" for report in reports)
        else "matched_with_explicit_nan_policy"
        if any(report["status"] == "matched_with_explicit_nan_policy" for report in reports)
        else "matched",
        "correctness_status": "regressed"
        if any(report["correctness_status"] == "regressed" for report in reports)
        else "passed",
        "suites": len(reports),
        "records": sum(int(report["records"]) for report in reports),
        "generated_records": sum(
            int(report.get("generated_records", report["records"]))
            for report in reports
        ),
        "native_records": sum(
            int(report.get("native_records", report["records"]))
            for report in reports
        ),
        "generated_only_records": sum(
            int(report.get("generated_only_records", 0)) for report in reports
        ),
        "native_only_records": sum(
            int(report.get("native_only_records", 0)) for report in reports
        ),
        "nonzero_finite_differences": sum(
            int(report.get("nonzero_finite_differences", 0)) for report in reports
        ),
        "finite_differences_beyond_printed_precision": sum(
            int(report.get("finite_differences_beyond_printed_precision", 0))
            for report in reports
        ),
        "generated_threshold_failures": sum(
            int(report.get("generated_threshold_failures", 0))
            for report in reports
        ),
        "native_threshold_failures": sum(
            int(report.get("native_threshold_failures", 0)) for report in reports
        ),
        "generated_threshold_regressions": sum(
            int(report.get("generated_threshold_regressions", 0))
            for report in reports
        ),
        "generated_diagnostic_threshold_exceedances": sum(
            int(report.get("generated_diagnostic_threshold_exceedances", 0))
            for report in reports
        ),
        "native_diagnostic_threshold_exceedances": sum(
            int(report.get("native_diagnostic_threshold_exceedances", 0))
            for report in reports
        ),
        "different_suites": sum(
            int(report["status"] == "different") for report in reports
        ),
        "regressed_suites": sum(
            int(report["correctness_status"] == "regressed") for report in reports
        ),
        "generated_nan_records": sum(
            int(report["generated_nan_records"]) for report in reports
        ),
        "native_nan_records": sum(
            int(report["native_nan_records"]) for report in reports
        ),
        "modes": {
            mode: sum(1 for report in reports if report["mode"] == mode)
            for mode in (
                "lin", "rfp", "eig", "ec", "balance", "blas", "blas1"
            )
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode",
        choices=(
            "lin", "rfp", "eig", "ec", "balance", "blas", "blas1",
            "manifest", "self-test"
        ),
    )
    parser.add_argument("generated", nargs="?", type=Path)
    parser.add_argument("native", nargs="?", type=Path)
    parser.add_argument("--threshold", type=float, default=30.0)
    parser.add_argument("--expected-generated-nan", type=int, default=0)
    parser.add_argument("--expected-native-nan", type=int, default=0)
    parser.add_argument("--records", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--fail-on", choices=("difference", "regression"), default="difference"
    )
    args = parser.parse_args()
    if args.mode == "self-test":
        self_test()
        print("exhaustive LAPACK result differential self-test passed")
        return 0
    if args.mode == "manifest":
        if args.generated is None:
            parser.error("the exhaustive report directory is required")
        try:
            report = build_manifest(args.generated)
        except Difference as error:
            print(f"exhaustive LAPACK manifest failed: {error}", file=sys.stderr)
            return 1
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
        print(
            f"validated {report['suites']} exhaustive suites and "
            f"{report['records']} per-result records"
        )
        return int(
            report["status"] == "different"
            if args.fail_on == "difference"
            else report["correctness_status"] == "regressed"
        )
    if args.generated is None or args.native is None:
        parser.error("generated and native transcripts are required")
    if args.threshold <= 0.0 or not math.isfinite(args.threshold):
        parser.error("--threshold must be finite and positive")
    if args.expected_generated_nan < 0 or args.expected_native_nan < 0:
        parser.error("expected NaN counts cannot be negative")
    try:
        report = compare(
            args.mode,
            args.generated,
            args.native,
            args.threshold,
            args.expected_generated_nan,
            args.expected_native_nan,
            args.records,
        )
    except Difference as error:
        print(f"exhaustive LAPACK result differential failed: {error}", file=sys.stderr)
        return 1
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, allow_nan=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(
        f"differentially checked all {report['records']} numerical records; "
        f"generated/native NaN records={report['generated_nan_records']}/"
        f"{report['native_nan_records']}"
    )
    if report["status"] == "different":
        print(
            "exhaustive LAPACK result differential found: "
            + "; ".join(report["differences"]),
            file=sys.stderr,
        )
    return int(
        report["status"] == "different"
        if args.fail_on == "difference"
        else report["correctness_status"] == "regressed"
    )


if __name__ == "__main__":
    raise SystemExit(main())
