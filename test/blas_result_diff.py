#!/usr/bin/env python3
"""Compare every official Reference BLAS summary record with native Fortran."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


class Difference(Exception):
    pass


@dataclass(frozen=True)
class RoutineResult:
    routine: str
    generated: int | str
    native: int | str


LEVEL1_HEADER = re.compile(r"^\s*(\d+)\s+([A-Z][A-Z0-9]*)\s*$")
NATIVE_LEVEL1_HEADER = re.compile(
    r"Test of subprogram number\s+(\d+)\s+([A-Z][A-Z0-9]*)", re.IGNORECASE
)
GENERATED_SUMMARY = re.compile(r"^\s*([A-Z][A-Z0-9]*)\s+(\d+)\s*$")
NATIVE_SUMMARY = re.compile(
    r"^\s*([A-Z][A-Z0-9]*)\s+PASSED THE COMPUTATIONAL TESTS\s*"
    r"\(\s*(\d+)\s+CALLS\)",
    re.IGNORECASE,
)
NATIVE_ERROR_EXIT = re.compile(
    r"^\s*([A-Z][A-Z0-9]*)\s+PASSED THE TESTS OF ERROR-EXITS", re.IGNORECASE
)
NUMBER = re.compile(
    r"^[+-]?(?:inf(?:inity)?|nan|(?:\d+(?:\.\d*)?|\.\d+)(?:[eEdD][+-]?\d+)?)$",
    re.IGNORECASE,
)


def canonical_name(name: str) -> str:
    # The official Level 3 FORMAT fields are A6 although *GEMMTR has seven
    # characters.  Compare the semantic routine identifier exposed by the
    # native transcript instead of treating that formatting truncation as a
    # generated-code difference.
    return name.strip().upper()[:6]


def reject_failure_markers(label: str, text: str) -> None:
    lowered = text.lower()
    markers = (
        " failed ",
        "failures",
        "fatal error",
        "completed the computational tests",
        "was not tested",
        "error-exit tests were not",
    )
    marker = next((item.strip() for item in markers if item in lowered), None)
    if marker is not None:
        raise Difference(f"{label} reports {marker!r}")


def parse_generated_level1(text: str) -> list[tuple[int, str]]:
    records: list[tuple[int, str]] = []
    for line in text.splitlines():
        if not line.strip():
            continue
        match = LEVEL1_HEADER.fullmatch(line)
        if match is None:
            raise Difference(f"generated Level 1 output contains a failure/detail line: {line!r}")
        records.append((int(match.group(1)), canonical_name(match.group(2))))
    if not records:
        raise Difference("generated Level 1 output has no routine records")
    return records


def parse_native_level1(text: str) -> list[tuple[int, str]]:
    matches = list(NATIVE_LEVEL1_HEADER.finditer(text))
    if not matches:
        raise Difference("native Level 1 output has no routine records")
    records: list[tuple[int, str]] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        result = text[match.end() : end].upper()
        if "FAIL" in result or "PASS" not in result:
            raise Difference(
                f"native Level 1 routine {match.group(2).upper()} did not report PASS"
            )
        records.append((int(match.group(1)), canonical_name(match.group(2))))
    return records


def compare_level1(generated: str, native: str) -> tuple[list[RoutineResult], dict[str, float]]:
    generated_records = parse_generated_level1(generated)
    native_records = parse_native_level1(native)
    if generated_records != native_records:
        raise Difference(
            f"Level 1 routine sequence differs: generated={generated_records}, "
            f"native={native_records}"
        )
    return (
        [RoutineResult(name, "PASS", "PASS") for _, name in generated_records],
        {},
    )


def native_scalar(text: str, pattern: str, label: str) -> float:
    match = re.search(pattern, text, re.IGNORECASE)
    if match is None:
        raise Difference(f"native output is missing {label}")
    return float(match.group(1).replace("d", "e").replace("D", "E"))


def generated_scalars(text: str) -> list[float]:
    values: list[float] = []
    for line in text.splitlines():
        token = line.strip()
        if NUMBER.fullmatch(token):
            values.append(float(token.replace("d", "e").replace("D", "E")))
    return values


def compare_level23(
    generated: str, native: str
) -> tuple[list[RoutineResult], dict[str, float]]:
    reject_failure_markers("generated C", generated)
    reject_failure_markers("native Fortran", native)

    generated_records = [
        (canonical_name(match.group(1)), int(match.group(2)))
        for line in generated.splitlines()
        if (match := GENERATED_SUMMARY.fullmatch(line)) is not None
    ]
    native_records = [
        (canonical_name(match.group(1)), int(match.group(2)))
        for line in native.splitlines()
        if (match := NATIVE_SUMMARY.match(line)) is not None
    ]
    if not generated_records or not native_records:
        raise Difference("no Level 2/3 computational summary records were found")
    if generated_records != native_records:
        limit = min(len(generated_records), len(native_records))
        index = next(
            (i for i in range(limit) if generated_records[i] != native_records[i]), limit
        )
        generated_item = generated_records[index] if index < len(generated_records) else "<missing>"
        native_item = native_records[index] if index < len(native_records) else "<missing>"
        raise Difference(
            f"computational record {index + 1} differs: "
            f"generated={generated_item}, native={native_item}"
        )

    generated_headers = [
        canonical_name(line.strip())
        for line in generated.splitlines()
        if re.fullmatch(r"\s*[A-Z][A-Z0-9]*\s*", line)
    ]
    expected_names = [name for name, _ in generated_records]
    if generated_headers != expected_names:
        raise Difference(
            "generated error-exit routine sequence differs from its computational summaries"
        )
    native_error_exits = [
        canonical_name(match.group(1))
        for line in native.splitlines()
        if (match := NATIVE_ERROR_EXIT.match(line)) is not None
    ]
    if native_error_exits != expected_names:
        raise Difference(
            f"native error-exit routine sequence differs: expected={expected_names}, "
            f"native={native_error_exits}"
        )

    scalars = generated_scalars(generated)
    if len(scalars) != 2:
        raise Difference(
            f"generated output has {len(scalars)} standalone threshold/precision values; expected 2"
        )
    generated_threshold, generated_precision = scalars
    native_threshold = native_scalar(
        native, r"TEST RATIO IS LESS THAN\s+([0-9.EeDd+-]+)", "test threshold"
    )
    native_precision = native_scalar(
        native, r"MACHINE PRECISION IS TAKEN TO BE\s+([0-9.EeDd+-]+)", "machine precision"
    )
    if generated_threshold != native_threshold:
        raise Difference(
            f"test threshold differs: generated={generated_threshold:g}, native={native_threshold:g}"
        )
    # The native official E9.1 transcript retains only two significant digits.
    if not math.isclose(generated_precision, native_precision, rel_tol=5.0e-2, abs_tol=0.0):
        raise Difference(
            "machine precision differs beyond the native transcript's E9.1 resolution: "
            f"generated={generated_precision:.17g}, native={native_precision:.17g}"
        )

    results = [RoutineResult(name, count, count) for name, count in generated_records]
    return results, {
        "generated_threshold": generated_threshold,
        "native_threshold": native_threshold,
        "generated_machine_precision": generated_precision,
        "native_machine_precision": native_precision,
    }


def write_report(
    path: Path,
    suite: str,
    mode: str,
    results: list[RoutineResult],
    scalars: dict[str, float],
    generated_path: Path,
    native_path: Path,
) -> None:
    records: list[dict[str, object]] = []
    for result in results:
        record: dict[str, object] = {
            "kind": "computational_call_count"
            if isinstance(result.generated, int)
            else "routine_status",
            "name": result.routine,
            "generated": result.generated,
            "native": result.native,
            "comparison": "exact",
            "status": "matched",
        }
        if isinstance(result.generated, (int, float)) and isinstance(
            result.native, (int, float)
        ):
            difference = abs(float(result.generated) - float(result.native))
            record.update(
                {
                    "absolute_difference": difference,
                    "relative_difference": difference / abs(float(result.native))
                    if result.native != 0
                    else difference,
                    "relative_tolerance": None,
                }
            )
        records.append(record)
    for name in ("threshold", "machine_precision"):
        generated_name = f"generated_{name}"
        native_name = f"native_{name}"
        if generated_name in scalars and native_name in scalars:
            generated_value = scalars[generated_name]
            native_value = scalars[native_name]
            difference = abs(generated_value - native_value)
            records.append(
                {
                    "kind": "test_parameter" if name == "threshold" else "machine_constant",
                    "name": name,
                    "generated": generated_value,
                    "native": native_value,
                    "absolute_difference": difference,
                    "relative_difference": difference / abs(native_value)
                    if native_value != 0.0
                    else difference,
                    "comparison": "exact" if name == "threshold" else "formatted_relative",
                    "relative_tolerance": None if name == "threshold" else 5.0e-2,
                    "status": "matched",
                }
            )
    report = {
        "schema_version": 1,
        "suite": suite,
        "mode": mode,
        "status": "matched",
        "result_groups": len(results),
        "numerical_records": sum(
            1 for record in records if isinstance(record["generated"], (int, float))
        ),
        "computational_calls": sum(
            result.generated for result in results if isinstance(result.generated, int)
        ),
        "scalars": scalars,
        "routines": [asdict(result) for result in results],
        "records": records,
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
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def self_test() -> None:
    generated1 = "\n 1 SDOT\n\n 2 SAXPY\n"
    native1 = (
        "Test of subprogram number 1 SDOT\n----- PASS -----\n"
        "Test of subprogram number 2 SAXPY\n----- PASS -----\n"
    )
    results, _ = compare_level1(generated1, native1)
    assert [item.routine for item in results] == ["SDOT", "SAXPY"]

    generated23 = "0 1 2\n\n16\n\n1.1920929e-07\n\nSGEMV\n\nSGEMV 3461\n"
    native23 = (
        "ROUTINES PASS COMPUTATIONAL TESTS IF TEST RATIO IS LESS THAN 16.00\n"
        "RELATIVE MACHINE PRECISION IS TAKEN TO BE 1.2E-07\n"
        "SGEMV PASSED THE TESTS OF ERROR-EXITS\n"
        "SGEMV PASSED THE COMPUTATIONAL TESTS ( 3461 CALLS)\n"
    )
    results, scalars = compare_level23(generated23, native23)
    assert results == [RoutineResult("SGEMV", 3461, 3461)]
    assert scalars["generated_threshold"] == 16.0

    report = Path("blas-result-diff-self-test.json")
    try:
        write_report(
            report,
            "sblat2",
            "level23",
            results,
            scalars,
            Path(__file__),
            Path(__file__),
        )
        parsed = json.loads(report.read_text(encoding="utf-8"))
        count_record = parsed["records"][0]
        assert count_record["absolute_difference"] == 0.0
        assert count_record["relative_difference"] == 0.0
        assert count_record["relative_tolerance"] is None
    finally:
        report.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("level1", "level23", "self-test"))
    parser.add_argument("generated", nargs="?")
    parser.add_argument("native", nargs="?")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--suite")
    args = parser.parse_args()
    if args.mode == "self-test":
        self_test()
        print("BLAS result differ self-test passed")
        return 0
    if args.generated is None or args.native is None:
        parser.error("generated and native output paths are required")

    generated = Path(args.generated).read_text(encoding="utf-8", errors="replace")
    native = Path(args.native).read_text(encoding="utf-8", errors="replace")
    try:
        if args.mode == "level1":
            results, scalars = compare_level1(generated, native)
        else:
            results, scalars = compare_level23(generated, native)
    except Difference as error:
        print(f"BLAS numerical differential failed: {error}", file=sys.stderr)
        return 1
    if args.report is not None:
        suite = args.suite or Path(args.generated).parent.name
        write_report(
            args.report,
            suite,
            args.mode,
            results,
            scalars,
            Path(args.generated),
            Path(args.native),
        )
    calls = sum(item.generated for item in results if isinstance(item.generated, int))
    print(f"matched {len(results)} official BLAS result groups ({calls} computational calls)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
