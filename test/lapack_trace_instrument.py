#!/usr/bin/env python3
"""Create computation-preserving LAPACK test-driver trace variants.

The official drivers normally print only ratios which exceed THRESH or retain
only a maximum over many examples.  This utility widens selected reporting
guards and traces condition-estimation and balance maxima without changing the
official inputs.  Each value receives a list-directed identity marker.  The
same instrumented source is compiled as generated C and native Fortran so the
two result streams can be aligned by test identity.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys


REPORT_GUARD = re.compile(
    r"^(?P<indent>\s*)IF\s*\(\s*(?P<value>(?:RESULT|DUMMA)"
    r"\s*\(\s*[A-Z0-9]+\s*\)|RESID|RES)"
    r"\s*\.G[ET]\.\s*THRESH\s*\)\s*(?P<then>THEN)?\s*$",
    re.IGNORECASE,
)
WRITE_FORMAT = re.compile(r"(?P<prefix>\bFMT\s*=\s*)\d+", re.IGNORECASE)
TRACE_VALUE = re.compile(
    r"(?:RESULT|DUMMA)\s*\(\s*[A-Z0-9]+\s*\)|\bRESID\b|\bRES\b",
    re.IGNORECASE,
)
EC_RMAX_GUARD = re.compile(
    r"^\s*IF\s*\(\s*(?P<value>RESID|RES|VMAX)\s*\.GT\.\s*"
    r"RMAX(?:\s*\(\s*\d+\s*\))?\s*\)",
    re.IGNORECASE,
)

EXPECTED_GUARDS = {
    "schksy_aa.f": 2,
    "dchksy_aa.f": 2,
    "cchkhe_aa.f": 2,
    "cchksy_aa.f": 2,
    "zchkhe_aa.f": 2,
    "zchksy_aa.f": 2,
    "schktr.f": 6,
    "dchktr.f": 6,
    "cchktr.f": 6,
    "zchktr.f": 6,
    "sdrvrf1.f": 1,
    "ddrvrf1.f": 1,
    "cdrvrf1.f": 1,
    "zdrvrf1.f": 1,
}


class InstrumentationError(Exception):
    pass


def normalize_result_writes(lines: list[str]) -> int:
    replacements = 0
    for index, line in enumerate(lines):
        body = line.rstrip("\r\n")
        if WRITE_FORMAT.search(body) is None:
            continue
        end = index + 1
        while end < len(lines) and re.match(r"^ {5}[^ 0]", lines[end]):
            end += 1
        statement = " ".join(
            candidate.rstrip("\r\n") for candidate in lines[index:end]
        )
        if TRACE_VALUE.search(statement) is None:
            continue
        body = WRITE_FORMAT.sub(r"\g<prefix>*", body, count=1)
        value_line = None
        value_match = None
        for candidate_index in range(index, end):
            candidate_body = lines[candidate_index].rstrip("\r\n")
            candidate_match = TRACE_VALUE.search(candidate_body)
            if candidate_match is not None:
                value_line = candidate_index
                value_match = candidate_match
                break
        if value_line is None or value_match is None:
            raise InstrumentationError("cannot locate traced value in WRITE statement")
        if value_line == index:
            candidate_body = body
            value_match = TRACE_VALUE.search(candidate_body)
            if value_match is None:
                raise InstrumentationError(
                    "cannot preserve a traced value while normalizing WRITE"
                )
        else:
            candidate_body = lines[value_line].rstrip("\r\n")
        ending = (
            "\r\n"
            if lines[value_line].endswith("\r\n")
            else "\n"
            if lines[value_line].endswith("\n")
            else ""
        )
        candidate_body = (
            candidate_body[: value_match.start()]
            + "'@', "
            + candidate_body[value_match.start() :]
        )
        if len(candidate_body) > 72:
            marker_start = candidate_body.index("'@', ")
            prefix = candidate_body[:marker_start].rstrip()
            continuation = "     $   " + candidate_body[marker_start:].lstrip()
            if len(continuation) > 72:
                raise InstrumentationError(
                    "cannot wrap a trace-marked fixed-form WRITE statement"
                )
            if re.fullmatch(r" {5}\S", prefix):
                lines[value_line] = continuation + ending
            elif not prefix.endswith(","):
                raise InstrumentationError(
                    "cannot split a trace-marked fixed-form WRITE statement"
                )
            elif value_line == index:
                body = prefix
            else:
                lines[value_line] = prefix + ending
            if not re.fullmatch(r" {5}\S", prefix):
                lines.insert(value_line + 1, continuation + ending)
        elif value_line == index:
            body = candidate_body
        else:
            lines[value_line] = candidate_body + ending
        lines[index] = body + (
            "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
        )
        replacements += 1
    return replacements


def instrument(source: Path, destination: Path, automatic: bool = False) -> int:
    expected = EXPECTED_GUARDS.get(source.name)
    if expected is None and not automatic:
        raise InstrumentationError(f"unsupported trace source: {source.name}")
    output: list[str] = []
    replacements = 0
    for line in source.read_text(encoding="utf-8").splitlines(keepends=True):
        body = line.rstrip("\r\n")
        ending = line[len(body) :]
        match = REPORT_GUARD.fullmatch(body)
        if match is None:
            output.append(line)
            continue
        then = " THEN" if match.group("then") is not None else ""
        if automatic:
            value = match.group("value")
            output.append(
                f"{match.group('indent')}IF( {value}.GE.0 .OR.{ending}"
            )
            output.append(f"     $    {value}.NE.{value} ){then}{ending}")
        else:
            output.append(f"{match.group('indent')}IF( .TRUE. ){then}{ending}")
        replacements += 1
    if automatic and replacements == 0:
        raise InstrumentationError(f"{source.name}: no supported reporting guards")
    if automatic:
        normalize_result_writes(output)
    if expected is not None and replacements != expected:
        raise InstrumentationError(
            f"{source.name}: expected {expected} reporting guards, found {replacements}"
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(output), encoding="utf-8")
    return replacements


def instrument_ec_source(source: Path, destination: Path) -> int:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    output: list[str] = []
    sites = 0
    routine = source.stem.upper()
    for line in lines:
        body = line.rstrip("\r\n")
        match = EC_RMAX_GUARD.match(body)
        if match is not None:
            sites += 1
            ending = (
                "\r\n"
                if line.endswith("\r\n")
                else "\n"
                if line.endswith("\n")
                else ""
            )
            trace = (
                f"      WRITE( *, * ) '{routine}', KNT, {sites}, '@', "
                f"{match.group('value').upper()}"
            )
            if len(trace) > 72:
                raise InstrumentationError(
                    f"{source.name}: EC trace exceeds fixed-form column 72"
                )
            output.append(trace + ending)
        output.append(line)
    if sites == 0:
        raise InstrumentationError(f"{source.name}: no EC result sites")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(output), encoding="utf-8")
    return sites


def instrument_ec_directory(
    source_directory: Path,
    destination_directory: Path,
    manifest: Path,
    expected_files: int,
    expected_sites: int,
) -> None:
    patterns = (
        "[sd]get3[1-9].f",
        "[cz]get3[5-8].f",
        "[sdcz]syl01.f",
    )
    sources = sorted(
        {source for pattern in patterns for source in source_directory.glob(pattern)}
    )
    names: list[str] = []
    sites = 0
    for source in sources:
        sites += instrument_ec_source(source, destination_directory / source.name)
        names.append(source.stem)
    if len(names) != expected_files or sites != expected_sites:
        raise InstrumentationError(
            f"instrumented EC coverage differs: files={len(names)}/{expected_files}, "
            f"sites={sites}/{expected_sites}"
        )
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text("".join(f"{name}\n" for name in names), encoding="utf-8")


def instrument_balance_directory(
    source_directory: Path,
    destination_directory: Path,
    manifest: Path,
    expected_files: int,
    expected_sites: int,
) -> None:
    stems = (
        "chkbl",
        "chkbk",
        "chkgl",
        "chkgk",
    )
    sources = sorted(
        source_directory / f"{precision}{stem}.f"
        for precision in "sdcz"
        for stem in stems
    )
    missing = [source.name for source in sources if not source.is_file()]
    if missing:
        raise InstrumentationError(
            "missing balance trace sources: " + ", ".join(missing)
        )
    names: list[str] = []
    sites = 0
    for source in sources:
        sites += instrument_ec_source(source, destination_directory / source.name)
        names.append(source.stem)
    if len(names) != expected_files or sites != expected_sites:
        raise InstrumentationError(
            f"instrumented balance coverage differs: files={len(names)}/"
            f"{expected_files}, sites={sites}/{expected_sites}"
        )
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text("".join(f"{name}\n" for name in names), encoding="utf-8")


def zero_threshold(source: Path, destination: Path, line_number: int) -> None:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    if line_number < 1 or line_number > len(lines):
        raise InstrumentationError(f"threshold line {line_number} is outside {source}")
    original = lines[line_number - 1]
    match = re.match(r"^(\s*)\S+(.*)$", original.rstrip("\r\n"))
    if match is None or "threshold" not in match.group(2).lower():
        raise InstrumentationError(
            f"line {line_number} in {source} is not a documented threshold"
        )
    ending = "\r\n" if original.endswith("\r\n") else "\n" if original.endswith("\n") else ""
    lines[line_number - 1] = f"{match.group(1)}0.0{match.group(2)}{ending}"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(lines), encoding="utf-8")


def zero_all_thresholds(source: Path, destination: Path) -> int:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    replacements = 0
    for index, original in enumerate(lines):
        body = original.rstrip("\r\n")
        match = re.match(r"^(\s*)\S+(.*threshold.*)$", body, re.IGNORECASE)
        if match is None:
            continue
        ending = "\r\n" if original.endswith("\r\n") else "\n" if original.endswith("\n") else ""
        lines[index] = f"{match.group(1)}0.0{match.group(2)}{ending}"
        replacements += 1
    if replacements == 0:
        raise InstrumentationError(f"no documented thresholds found in {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(lines), encoding="utf-8")
    return replacements


def self_test() -> None:
    sample = "                        IF( RESULT( K ).GE.THRESH ) THEN"
    match = REPORT_GUARD.fullmatch(sample)
    assert match is not None
    assert match.group("indent") == "                        "
    assert REPORT_GUARD.fullmatch("IF( RESULT( 10 ).GE.THRESH ) THEN") is not None
    assert REPORT_GUARD.fullmatch("IF( INFO.GE.THRESH ) THEN") is None
    normalized, count = WRITE_FORMAT.subn(
        r"\g<prefix>*", "WRITE( NOUNIT, FMT = 9993 )N, J, RESULT( J )", count=1
    )
    assert count == 1
    assert normalized == "WRITE( NOUNIT, FMT = * )N, J, RESULT( J )"
    lines = [
        "               WRITE( NOUNIT, FMT = 9993 )N, JTYPE,\n",
        "     $            J, RESULT( J )\n",
    ]
    assert normalize_result_writes(lines) == 1
    assert "FMT = *" in lines[0]
    assert "'@', RESULT( J )" in lines[1]
    ec_match = EC_RMAX_GUARD.match("         IF( VMAX.GT.RMAX( 2 ) ) THEN")
    assert ec_match is not None
    assert ec_match.group("value") == "VMAX"


def relink(link_script: Path, destination: Path, replacements: list[str]) -> None:
    command = shlex.split(link_script.read_text(encoding="utf-8"))
    replacement_paths: dict[str, Path] = {}
    for specification in replacements:
        name, separator, path = specification.partition("=")
        if not separator or not name.endswith(".o") or not path:
            raise InstrumentationError(
                f"invalid replacement {specification!r}; expected object.o=/path/to/object.o"
            )
        replacement_paths[name] = Path(path).resolve()
    replaced: set[str] = set()
    for index, argument in enumerate(command):
        name = Path(argument).name
        if name in replacement_paths:
            command[index] = str(replacement_paths[name])
            replaced.add(name)
    missing = sorted(replacement_paths.keys() - replaced)
    if missing:
        raise InstrumentationError(
            f"objects are absent from {link_script}: {', '.join(missing)}"
        )
    try:
        output_index = command.index("-o") + 1
    except (ValueError, IndexError) as error:
        raise InstrumentationError(f"cannot find output in {link_script}") from error
    destination.parent.mkdir(parents=True, exist_ok=True)
    command[output_index] = str(destination.resolve())
    # CMake link scripts contain paths relative to the target's binary
    # directory, not relative to CMakeFiles/<target>.dir itself.
    subprocess.run(command, cwd=link_script.parent.parent.parent, check=True)


def instrument_directory(
    source_directory: Path,
    destination_directory: Path,
    manifest: Path,
    expected_files: int,
    expected_guards: int,
) -> None:
    names: list[str] = []
    guards = 0
    for source in sorted(source_directory.glob("*.[fF]")):
        text = source.read_text(encoding="utf-8")
        count = sum(REPORT_GUARD.fullmatch(line) is not None for line in text.splitlines())
        if count == 0:
            continue
        actual = instrument(
            source, destination_directory / source.name, automatic=True
        )
        guards += actual
        names.append(source.stem)
    if len(names) != expected_files or guards != expected_guards:
        raise InstrumentationError(
            f"instrumented directory coverage differs: files={len(names)}/"
            f"{expected_files}, guards={guards}/{expected_guards}"
        )
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text("".join(f"{name}\n" for name in names), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    instrument_parser = subparsers.add_parser("instrument")
    instrument_parser.add_argument("source", type=Path)
    instrument_parser.add_argument("destination", type=Path)
    automatic_parser = subparsers.add_parser("instrument-auto")
    automatic_parser.add_argument("source", type=Path)
    automatic_parser.add_argument("destination", type=Path)
    input_parser = subparsers.add_parser("zero-threshold")
    input_parser.add_argument("source", type=Path)
    input_parser.add_argument("destination", type=Path)
    input_parser.add_argument("--line", type=int, required=True)
    all_input_parser = subparsers.add_parser("zero-thresholds")
    all_input_parser.add_argument("source", type=Path)
    all_input_parser.add_argument("destination", type=Path)
    relink_parser = subparsers.add_parser("relink")
    relink_parser.add_argument("link_script", type=Path)
    relink_parser.add_argument("destination", type=Path)
    relink_parser.add_argument("replacement", nargs="*")
    directory_parser = subparsers.add_parser("instrument-directory")
    directory_parser.add_argument("source", type=Path)
    directory_parser.add_argument("destination", type=Path)
    directory_parser.add_argument("manifest", type=Path)
    directory_parser.add_argument("--expected-files", type=int, required=True)
    directory_parser.add_argument("--expected-guards", type=int, required=True)
    ec_directory_parser = subparsers.add_parser("instrument-ec-directory")
    ec_directory_parser.add_argument("source", type=Path)
    ec_directory_parser.add_argument("destination", type=Path)
    ec_directory_parser.add_argument("manifest", type=Path)
    ec_directory_parser.add_argument("--expected-files", type=int, required=True)
    ec_directory_parser.add_argument("--expected-sites", type=int, required=True)
    balance_directory_parser = subparsers.add_parser(
        "instrument-balance-directory"
    )
    balance_directory_parser.add_argument("source", type=Path)
    balance_directory_parser.add_argument("destination", type=Path)
    balance_directory_parser.add_argument("manifest", type=Path)
    balance_directory_parser.add_argument("--expected-files", type=int, required=True)
    balance_directory_parser.add_argument("--expected-sites", type=int, required=True)
    subparsers.add_parser("self-test")
    args = parser.parse_args()
    try:
        if args.command == "instrument":
            count = instrument(args.source, args.destination)
            print(f"instrumented {count} reporting guards in {args.source.name}")
        elif args.command == "instrument-auto":
            count = instrument(args.source, args.destination, automatic=True)
            print(f"instrumented {count} reporting guards in {args.source.name}")
        elif args.command == "zero-threshold":
            zero_threshold(args.source, args.destination, args.line)
        elif args.command == "zero-thresholds":
            count = zero_all_thresholds(args.source, args.destination)
            print(f"zeroed {count} thresholds in {args.source.name}")
        elif args.command == "relink":
            relink(args.link_script, args.destination, args.replacement)
        elif args.command == "instrument-directory":
            instrument_directory(
                args.source,
                args.destination,
                args.manifest,
                args.expected_files,
                args.expected_guards,
            )
        elif args.command == "instrument-ec-directory":
            instrument_ec_directory(
                args.source,
                args.destination,
                args.manifest,
                args.expected_files,
                args.expected_sites,
            )
        elif args.command == "instrument-balance-directory":
            instrument_balance_directory(
                args.source,
                args.destination,
                args.manifest,
                args.expected_files,
                args.expected_sites,
            )
        else:
            self_test()
            print("LAPACK trace instrumentation self-test passed")
    except InstrumentationError as error:
        print(f"LAPACK trace instrumentation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
