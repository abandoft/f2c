#!/usr/bin/env python3
"""Create computation-preserving traces for official Reference BLAS tests."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


LEVEL23_RESULT = re.compile(
    r"^(?P<indent>\s*)ERRMAX\s*=\s*MAX\s*\(\s*ERRMAX\s*,\s*ERR\s*\)\s*$",
    re.IGNORECASE,
)
SUBROUTINE = re.compile(r"^\s{0,6}SUBROUTINE\s+([A-Z0-9]+)", re.IGNORECASE)
ARRAY_DIFFERENCE = re.compile(
    r"^\s*SD\s*=\s*SCOMP\s*\(\s*I\s*\)\s*-\s*STRUE\s*\(\s*I\s*\)\s*$",
    re.IGNORECASE,
)
DSDOT_DIFFERENCE = re.compile(
    r"^\s*SD\s*=\s*SCOMP\s*-\s*STRUE\s*$", re.IGNORECASE
)
INTEGER_COMPARISON = re.compile(
    r"^\s*IF\s*\(\s*ICOMP\s*\.EQ\.\s*ITRUE\s*\)\s*GO\s+TO\s+40\s*$",
    re.IGNORECASE,
)
NRM2_COMPARISON = re.compile(
    r"^\s*IF\s*\(\s*\(\s*TRAT\s*\.NE\.\s*TRAT\s*\)\s*\.OR\.",
    re.IGNORECASE,
)


class InstrumentationError(Exception):
    pass


def fixed_write(arguments: str, unit: str = "*") -> str:
    compact = arguments.replace(", ", ",").replace("( I )", "(I)")
    line = f"      WRITE({unit},*){compact}"
    if len(line) > 72:
        raise InstrumentationError(f"trace statement exceeds fixed-form column 72: {line}")
    return line + "\n"


def instrument_level23(source: Path, destination: Path, expected_sites: int) -> None:
    output: list[str] = []
    sites = 0
    for line in source.read_text(encoding="utf-8").splitlines(keepends=True):
        body = line.rstrip("\r\n")
        if LEVEL23_RESULT.fullmatch(body) is not None:
            sites += 1
            # Level 2/3 drivers connect the default output unit to their
            # summary file.  Unit 0 is the diagnostic stream in both the
            # generated libc I/O layer and gfortran, keeping traces separate.
            output.append(fixed_write(f"SNAME, NC, {sites}, '@', ERR", "0"))
        output.append(line)
    if sites != expected_sites:
        raise InstrumentationError(
            f"{source.name}: expected {expected_sites} BLAS result sites, found {sites}"
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(output), encoding="utf-8")


def instrument_level1(source: Path, destination: Path, expected_sites: int) -> None:
    output: list[str] = []
    routine = ""
    complex_suite = source.stem[0].lower() in {"c", "z"}
    sites = 0
    for line in source.read_text(encoding="utf-8").splitlines(keepends=True):
        body = line.rstrip("\r\n")
        match = SUBROUTINE.match(body)
        if match is not None:
            routine = match.group(1).upper()
        output.append(line)
        if routine == "STEST" and ARRAY_DIFFERENCE.fullmatch(body) is not None:
            identity = "ICASE, N, INCX, INCY, "
            if complex_suite:
                identity += "MODE, "
            identity += "I"
            for site, value in enumerate(
                ("SCOMP( I )", "STRUE( I )", "SD", "SSIZE( I )"), 1
            ):
                output.append(
                    fixed_write(f"'STEST', {identity}, {site}, '@', {value}")
                )
                sites += 1
        elif routine == "TESTDSDOT" and DSDOT_DIFFERENCE.fullmatch(body) is not None:
            for site, value in enumerate(("SCOMP", "STRUE", "SD", "SSIZE"), 1):
                output.append(
                    fixed_write(
                        f"'TESTDSDOT', ICASE, N, INCX, INCY, 1, {site}, '@', {value}"
                    )
                )
                sites += 1
        elif routine == "ITEST1" and INTEGER_COMPARISON.fullmatch(body) is not None:
            identity = "ICASE, N, INCX, INCY, "
            if complex_suite:
                identity += "MODE, "
            for site, value in enumerate(("ICOMP", "ITRUE", "ICOMP-ITRUE"), 1):
                output.insert(
                    len(output) - 1,
                    fixed_write(f"'ITEST1', {identity}{site}, '@', {value}"),
                )
                sites += 1
        elif routine.endswith("B1NRM2") and NRM2_COMPARISON.match(body) is not None:
            output.insert(
                len(output) - 1,
                fixed_write(f"'{routine}', N, INCX, IV, IW, 1, '@', TRAT"),
            )
            sites += 1
    if sites != expected_sites:
        raise InstrumentationError(
            f"{source.name}: expected {expected_sites} BLAS result sites, found {sites}"
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(output), encoding="utf-8")


def self_test() -> None:
    assert LEVEL23_RESULT.fullmatch("   ERRMAX = MAX( ERRMAX, ERR )")
    assert ARRAY_DIFFERENCE.fullmatch("         SD = SCOMP(I) - STRUE(I)")
    assert DSDOT_DIFFERENCE.fullmatch("         SD = SCOMP - STRUE")
    assert INTEGER_COMPARISON.fullmatch("      IF (ICOMP.EQ.ITRUE) GO TO 40")
    assert NRM2_COMPARISON.match(
        "            IF ((TRAT.NE.TRAT).OR.(TRAT.GE.THRESH)) THEN"
    )
    assert len(fixed_write("SNAME, NC, 6, '@', ERR").rstrip()) <= 72


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("level1", "level23", "self-test"))
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("destination", nargs="?", type=Path)
    parser.add_argument("--expected-sites", type=int)
    args = parser.parse_args()
    if args.mode == "self-test":
        self_test()
        print("BLAS trace instrumentation self-test passed")
        return 0
    if args.source is None or args.destination is None or args.expected_sites is None:
        parser.error("source, destination and --expected-sites are required")
    if args.expected_sites <= 0:
        parser.error("--expected-sites must be positive")
    try:
        if args.mode == "level1":
            instrument_level1(args.source, args.destination, args.expected_sites)
        else:
            instrument_level23(args.source, args.destination, args.expected_sites)
    except InstrumentationError as error:
        print(f"BLAS trace instrumentation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
