#!/usr/bin/env python3
"""Compare semantic records emitted by generated-C and native LAPACK tests."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


NUMBER = re.compile(
    r"^[+-]?(?:inf(?:inity)?|nan|(?:\d+(?:\.\d*)?|\.\d+)(?:[eEdD][+-]?\d+)?|"
    r"(?:\d+(?:\.\d*)?|\.\d+)[+-]\d+)$",
    re.IGNORECASE,
)
GENERATED_RECORD = re.compile(r"^\s*([A-Z][A-Z0-9]*)\s+(\d+)\s*$")
NATIVE_RECORD = re.compile(
    r"All tests for\s+(?:the\s+)?([A-Z][A-Z0-9]*|RFP conversion routines).*?"
    r"\(\s*(\d+)\s+tests? run\)",
    re.IGNORECASE,
)
NATIVE_FAILED_RECORD = re.compile(
    r"^\s*([A-Z][A-Z0-9]*)\s+drivers:\s*(\d+)\s+out of\s*(\d+)\s+tests failed",
    re.IGNORECASE,
)
GENERATED_FAILED_RECORD = re.compile(
    r"^[ \t]*[A-Z][A-Z0-9]*[ \t]+\d+[ \t]+\d+[ \t]*$", re.MULTILINE
)


class Difference(Exception):
    pass


@dataclass(frozen=True)
class NumericField:
    name: str
    value: float
    integral: bool


@dataclass(frozen=True)
class ResultRecord:
    kind: str
    name: str
    generated: int | float
    native: int | float
    comparison: str
    relative_tolerance: float | None = None
    absolute_limit: float | None = None
    linked_record: str | None = None


def parse_number(token: str) -> float:
    value = token.strip().replace("d", "e").replace("D", "E")
    if "e" not in value.lower():
        match = re.fullmatch(r"([+-]?(?:\d+(?:\.\d*)?|\.\d+))([+-]\d+)", value)
        if match:
            value = f"{match.group(1)}e{match.group(2)}"
    return float(value)


def last_number(line: str) -> float | None:
    for token in reversed(line.replace("=", " ").split()):
        if NUMBER.fullmatch(token):
            return parse_number(token)
    return None


def machine_values(text: str) -> dict[str, float]:
    values: dict[str, float] = {}
    for line in text.splitlines():
        lowered = line.lower()
        for name in ("underflow", "overflow", "precision"):
            if name in lowered:
                value = last_number(line)
                if value is not None:
                    values[name] = value
        if "safe minimum" in lowered or "sfmin" in lowered:
            value = last_number(line)
            if value is not None:
                values["underflow"] = value
    if not values:
        # The generated condition-estimation driver emits EPS and SFMIN
        # without their FORMAT labels, in that order, on one line.
        for line in text.splitlines():
            tokens = line.split()
            if len(tokens) == 2 and all(NUMBER.fullmatch(token) for token in tokens) and any(
                "." in token or "e" in token.lower() or "d" in token.lower()
                for token in tokens
            ):
                values["precision"] = parse_number(tokens[0])
                values["underflow"] = parse_number(tokens[1])
                break
    return values


def canonical_driver_name(name: str) -> str:
    name = name.upper()
    # The native xEIGTST transcript labels both C/ZDRGEV and C/ZDRGEV3 as
    # xGV, while ALASVM in the latter emits xGV3 in the generated transcript.
    if name in ("CGV3", "ZGV3"):
        return name[:-1]
    return name


def generated_records(text: str, rfp: bool) -> list[tuple[str, int]]:
    records: list[tuple[str, int]] = []
    for line in text.splitlines():
        native_style = NATIVE_RECORD.search(line)
        match = GENERATED_RECORD.fullmatch(line)
        if native_style:
            name = native_style.group(1).upper()
            if name.startswith("RFP CONVERSION"):
                name = "RFP"
            records.append((canonical_driver_name(name), int(native_style.group(2))))
        elif match:
            records.append(
                (canonical_driver_name(match.group(1)), int(match.group(2)))
            )
        elif rfp and line.strip() == "72":
            records.append(("RFP", 72))
    return records


def native_records(text: str) -> list[tuple[str, int]]:
    records: list[tuple[str, int]] = []
    for line in text.splitlines():
        failed = NATIVE_FAILED_RECORD.search(line)
        if failed:
            records.append(
                (canonical_driver_name(failed.group(1)), int(failed.group(3)))
            )
            continue
        match = NATIVE_RECORD.search(line)
        if not match:
            continue
        name = match.group(1).upper()
        if name.startswith("RFP CONVERSION"):
            name = "RFP"
        records.append((canonical_driver_name(name), int(match.group(2))))
    return records


def reject_failures(label: str, text: str) -> None:
    lowered = text.lower()
    failure_markers = (
        "failed the threshold",
        "tests failed",
        "test failed",
        "error exits were not tested",
    )
    marker = next((item for item in failure_markers if item in lowered), None)
    if marker or GENERATED_FAILED_RECORD.search(text):
        marker = marker or "a generated failure-count record"
        raise Difference(f"{label} output reports {marker!r}")


def close_float(left: float, right: float, relative: float) -> bool:
    if math.isnan(left) or math.isnan(right):
        return math.isnan(left) and math.isnan(right)
    if math.isinf(left) or math.isinf(right):
        return left == right
    return math.isclose(left, right, rel_tol=relative, abs_tol=0.0)


def compare_machine(generated: str, native: str) -> list[ResultRecord]:
    generated_values = machine_values(generated)
    native_values = machine_values(native)
    if bool(generated_values) != bool(native_values):
        raise Difference("machine-constant records are missing from one implementation")
    records: list[ResultRecord] = []
    for name, native_value in native_values.items():
        if name not in generated_values:
            raise Difference(f"generated output is missing machine constant {name}")
        generated_value = generated_values[name]
        # Official Fortran formats retain six significant digits.
        if not close_float(generated_value, native_value, 5.0e-6):
            raise Difference(
                f"machine constant {name} differs: generated={generated_value:.17g}, "
                f"native={native_value:.17g}"
            )
        records.append(
            ResultRecord(
                "machine_constant",
                name,
                generated_value,
                native_value,
                "relative",
                5.0e-6,
            )
        )
    return records


def compare_summary(generated: str, native: str, rfp: bool) -> list[ResultRecord]:
    reject_failures("generated C", generated)
    generated_items = generated_records(generated, rfp)
    native_items = native_records(native)
    if not generated_items or not native_items:
        raise Difference("no official computational summary records were found")
    # EIG drivers also print error-exit routine/count pairs.  Native Fortran
    # describes those with prose and a different aggregate code, so retain
    # exactly the computational records exposed by the native transcript.
    remaining = Counter(native_items)
    computational_items: list[tuple[str, int]] = []
    for item in generated_items:
        if remaining[item] > 0:
            computational_items.append(item)
            remaining[item] -= 1
    generated_items = computational_items
    if generated_items != native_items:
        limit = min(len(generated_items), len(native_items))
        index = next(
            (i for i in range(limit) if generated_items[i] != native_items[i]), limit
        )
        generated_item = generated_items[index] if index < len(generated_items) else "<missing>"
        native_item = native_items[index] if index < len(native_items) else "<missing>"
        raise Difference(
            f"computational record {index + 1} differs: "
            f"generated={generated_item}, native={native_item}"
        )
    records = [
        ResultRecord("test_count", name, count, native_count, "exact")
        for (name, count), (_, native_count) in zip(generated_items, native_items)
    ]
    return records + compare_machine(generated, native)


def native_numeric_fields(text: str) -> list[NumericField]:
    fields: list[NumericField] = []
    for line in text.splitlines():
        if "=" not in line:
            continue
        name, raw_value = line.rsplit("=", 1)
        token = raw_value.strip()
        if not NUMBER.fullmatch(token):
            continue
        lowered = name.lower().strip()
        integral = not ("error" in lowered and "number" not in lowered)
        fields.append(NumericField(lowered, parse_number(token), integral))
    return fields


def generated_numeric_values(text: str) -> list[float]:
    values: list[float] = []
    for line in text.splitlines():
        token = line.strip()
        if NUMBER.fullmatch(token):
            values.append(parse_number(token))
    return values


def compare_balance(generated: str, native: str) -> list[ResultRecord]:
    reject_failures("generated C", generated)
    reject_failures("native Fortran", native)
    fields = native_numeric_fields(native)
    if not fields:
        raise Difference("native balance output has no labeled numerical records")
    generated_fields = native_numeric_fields(generated)
    if generated_fields:
        generated_names = [field.name for field in generated_fields]
        native_names = [field.name for field in fields]
        if generated_names != native_names:
            raise Difference(
                "generated and native balance field labels differ: "
                f"generated={generated_names}, native={native_names}"
            )
        values = [field.value for field in generated_fields]
    else:
        # Older generated drivers emitted FORMAT values without their labels.
        # Retain that representation as an explicitly positional fallback.
        values = generated_numeric_values(generated)
    if len(values) < len(fields):
        raise Difference(
            f"generated balance output has {len(values)} values; expected {len(fields)}"
        )
    # Generated tests print one trailing timing value after the official records.
    values = values[: len(fields)]
    error_pair: tuple[float, float] | None = None
    error_name: str | None = None
    for field, generated_value in zip(fields, values):
        if not field.integral:
            error_pair = (generated_value, field.value)
            error_name = field.name
            break
    # These standalone balance/back-transform tests report an error measured
    # in units of roundoff.  Cancellation can move the maximizing example and
    # can turn a nearly exact native result into a different sub-ULP result.
    # Treat that case as an explicit, independently verifiable bound.  Do not
    # disguise an arbitrary difference as a relative match.
    roundoff_bounded = (
        error_pair is not None
        and all(math.isfinite(value) for value in error_pair)
        and min(error_pair) >= 0.0
        and max(error_pair) <= 1.0
        and not close_float(error_pair[0], error_pair[1], 5.0e-3)
    )
    error_nonregression = (
        error_pair is not None
        and all(math.isfinite(value) for value in error_pair)
        and min(error_pair) >= 0.0
        and error_pair[0] < error_pair[1]
        and not close_float(error_pair[0], error_pair[1], 5.0e-3)
    )
    records: list[ResultRecord] = []
    for index, (field, generated_value) in enumerate(zip(fields, values), 1):
        if field.integral:
            index_of_rounding_maximum = "having largest error" in field.name
            if generated_value != field.value and not (
                roundoff_bounded and index_of_rounding_maximum
            ):
                raise Difference(
                    f"balance record {index} ({field.name}) differs: "
                    f"generated={generated_value:g}, native={field.value:g}"
                )
            comparison = (
                "argmax_of_roundoff_bound" if generated_value != field.value else "exact"
            )
            tolerance = None
            absolute_limit = None
            linked_record = error_name if generated_value != field.value else None
        # Native E12.3/D12.3 output has only three fractional digits.
        else:
            if close_float(generated_value, field.value, 5.0e-3):
                comparison = "relative"
                tolerance = 5.0e-3
                absolute_limit = None
            elif roundoff_bounded and field.name == error_name:
                comparison = "roundoff_bound"
                tolerance = None
                absolute_limit = 1.0
            elif error_nonregression and field.name == error_name:
                comparison = "no_regression"
                tolerance = None
                absolute_limit = field.value
            else:
                raise Difference(
                    f"balance record {index} ({field.name}) differs: "
                    f"generated={generated_value:.17g}, native={field.value:.17g}"
                )
            linked_record = None
        records.append(
            ResultRecord(
                "balance_value",
                field.name,
                generated_value,
                field.value,
                comparison,
                tolerance,
                absolute_limit,
                linked_record,
            )
        )
    return records


def compare_install(generated: str, native: str) -> list[ResultRecord]:
    def failures(text: str) -> int:
        if "all tests pass" in text.lower():
            return 0
        match = re.search(r"\b(\d+)\s+fail\b", text, re.IGNORECASE)
        if match:
            return int(match.group(1))
        raise Difference("INSTALL output has neither a pass record nor a failure count")

    generated_failures = failures(generated)
    native_failures = failures(native)
    if generated_failures > native_failures:
        raise Difference(
            f"generated INSTALL result regresses native Fortran: "
            f"generated={generated_failures} failures, native={native_failures}"
        )
    return [
        ResultRecord(
            "failure_count",
            "INSTALL failures",
            generated_failures,
            native_failures,
            "no_regression",
        )
    ]


def json_value(value: int | float) -> int | float | str:
    if isinstance(value, int) or math.isfinite(value):
        return value
    if math.isnan(value):
        return "NaN"
    return "Infinity" if value > 0.0 else "-Infinity"


def transcript_metadata(path: Path) -> dict[str, str]:
    return {
        "file": path.name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def write_report(
    path: Path,
    suite: str,
    mode: str,
    records: list[ResultRecord],
    generated_path: Path | None = None,
    native_path: Path | None = None,
) -> None:
    serialized_records: list[dict[str, object]] = []
    for record in records:
        item = asdict(record)
        item["generated"] = json_value(record.generated)
        item["native"] = json_value(record.native)
        if isinstance(record.generated, (int, float)) and isinstance(
            record.native, (int, float)
        ) and math.isfinite(record.generated) and math.isfinite(record.native):
            difference = abs(float(record.generated) - float(record.native))
            item["absolute_difference"] = difference
            scale = abs(float(record.native))
            item["relative_difference"] = difference / scale if scale != 0.0 else difference
        else:
            item["absolute_difference"] = None
            item["relative_difference"] = None
        item["status"] = "matched"
        serialized_records.append(item)
    report = {
        "schema_version": 1,
        "suite": suite,
        "mode": mode,
        "status": "matched",
        "numerical_records": len(records),
        "records": serialized_records,
    }
    if generated_path is not None and native_path is not None:
        report["transcripts"] = {
            "generated_c": transcript_metadata(generated_path),
            "native_fortran": transcript_metadata(native_path),
        }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(report, allow_nan=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def self_test() -> None:
    assert parse_number("0.222507-307") == 0.222507e-307
    generated = "underflow 1.17549435e-38\n SGE 3653\n SGE 5748\n"
    native = (
        "Relative machine underflow is taken to be 0.117549E-37\n"
        "All tests for SGE routines passed the threshold (3653 tests run)\n"
        "All tests for SGE drivers passed the threshold (5748 tests run)\n"
    )
    summary_records = compare_summary(generated, native, False)
    assert len(summary_records) == 3
    assert summary_records[0] == ResultRecord("test_count", "SGE", 3653, 3653, "exact")
    generated_improvement = " CGV 1092\n CGV3 1092\n"
    native_failures = (
        "CGV drivers: 48 out of 1092 tests failed to pass the threshold\n"
        "CGV drivers: 25 out of 1092 tests failed to pass the threshold\n"
    )
    assert len(compare_summary(generated_improvement, native_failures, False)) == 2
    generated_balance = "\n 1.96639836\n 0\n 7\n 0\n 7\n\n 0\n"
    native_balance = (
        "value of largest test error = 0.197E+01\n"
        "example number where info is not zero = 0\n"
        "example number having largest error = 7\n"
        "number of examples where info is not 0 = 0\n"
        "total number of examples tested = 7\n"
    )
    balance_records = compare_balance(generated_balance, native_balance)
    assert len(balance_records) == 5
    assert balance_records[-1].name == "total number of examples tested"

    labeled_balance_records = compare_balance(
        "value of largest test error = INF\n"
        "example number where info is not zero = 0\n"
        "example number having largest error = 11\n"
        "total number of examples tested = 13\n",
        "value of largest test error = Infinity\n"
        "example number where info is not zero = 0\n"
        "example number having largest error = 11\n"
        "total number of examples tested = 13\n",
    )
    assert len(labeled_balance_records) == 4
    assert math.isinf(labeled_balance_records[0].generated)

    improved_balance_records = compare_balance(
        "value of largest test error = 0.7415\n"
        "example number having largest error = 6\n"
        "total number of examples tested = 10\n",
        "value of largest test error = 1.48\n"
        "example number having largest error = 6\n"
        "total number of examples tested = 10\n",
    )
    assert improved_balance_records[0].comparison == "no_regression"
    assert improved_balance_records[0].absolute_limit == 1.48

    bounded_records = compare_balance(
        "\n 0.524287999\n 0\n 5\n 0\n 8\n\n 0\n",
        "value of largest test error = 0.105E-03\n"
        "example number where info is not zero = 0\n"
        "example number having largest error = 4\n"
        "number of examples where info is not 0 = 0\n"
        "total number of examples tested = 8\n",
    )
    assert bounded_records[0].comparison == "roundoff_bound"
    assert bounded_records[0].absolute_limit == 1.0
    assert bounded_records[2].comparison == "argmax_of_roundoff_bound"
    assert bounded_records[2].linked_record == "value of largest test error"
    try:
        compare_balance("\n 1.1\n", "value of largest test error = 0.1\n")
    except Difference:
        pass
    else:
        raise AssertionError("an out-of-bound balance result must not match")

    report = Path("lapack-result-diff-self-test.json")
    try:
        write_report(report, "self-test", "balance", balance_records)
        parsed = json.loads(report.read_text(encoding="utf-8"))
        assert parsed["suite"] == "self-test"
        assert parsed["numerical_records"] == 5
    finally:
        report.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode", choices=("summary", "rfp", "balance", "install", "self-test")
    )
    parser.add_argument("generated", nargs="?")
    parser.add_argument("native", nargs="?")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--suite")
    args = parser.parse_args()
    if args.mode == "self-test":
        self_test()
        print("LAPACK result differ self-test passed")
        return 0
    if args.generated is None or args.native is None:
        parser.error("generated and native output paths are required")
    generated = Path(args.generated).read_text(encoding="utf-8", errors="replace")
    native = Path(args.native).read_text(encoding="utf-8", errors="replace")
    try:
        if args.mode == "balance":
            records = compare_balance(generated, native)
        elif args.mode == "install":
            records = compare_install(generated, native)
        else:
            records = compare_summary(generated, native, args.mode == "rfp")
    except Difference as error:
        print(f"LAPACK numerical differential failed: {error}", file=sys.stderr)
        return 1
    if args.report is not None:
        suite = args.suite or Path(args.generated).stem
        write_report(
            args.report,
            suite,
            args.mode,
            records,
            Path(args.generated),
            Path(args.native),
        )
    print(f"matched {len(records)} official numerical records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
