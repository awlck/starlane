#!/usr/bin/env python3
"""Regression harness for starlane-console.

Finds every .taf file under a testdata directory, feeds each one a fixed
sequence of commands through starlane-console, and records the exit code
and a hash of the transcript. Games run in a worker pool sized to the
number of available CPUs, so a full pass over the test corpus stays fast
even as the corpus grows.

Typical use: compare interpreter behavior before/after a change.

    python3 scripts/run_regression.py --output /tmp/before.txt
    # ... make your change, rebuild ...
    python3 scripts/run_regression.py --output /tmp/after.txt
    python3 scripts/run_regression.py --diff /tmp/before.txt /tmp/after.txt
"""

import argparse
import concurrent.futures
import hashlib
import os
import signal
import subprocess
import sys
from pathlib import Path

# The command sequence every game is put through. Deliberately exercises the awkward corners of
# the undo machinery rather than just taking a couple of turns:
#
#   * two UNDOs in a row, and an UNDO / real turn / UNDO sandwich. A single UNDO at depth one is
#     the easy case; consecutive ones are where an undo implementation that records changes rather
#     than whole states can get its bookkeeping wrong.
#   * enough turns to run past kMaxUndoStates (100), so the oldest states are being discarded while
#     play continues -- and so that a turn which throws does so on a full history.
#   * a RESTORE of a path that isn't there, for the cancelled/failed restore path.
#   * a RESTART, which rebuilds the world from the startup state.
DEFAULT_COMMANDS = (
    "look\nwait\nz\nlook\nundo\nundo\nlook\n"
    "wait\nundo\nwait\nz\nundo\n"
    + "z\n" * 110
    + "undo\nundo\nlook\n"
    "restore\n/nonexistent/starlane-regression-no-such-save\n"
    "look\nrestart\nlook\nundo\n"
)
DEFAULT_TIMEOUT = 25.0
# A seed of 0 tells starlane-core to seed from the OS (nondeterministic), so
# games with randomized text produce a different transcript on every run
# unless a nonzero seed is passed. Pick an arbitrary nonzero default so
# --output/--diff runs are reproducible out of the box.
DEFAULT_SEED = 1

# Well-known Windows NTSTATUS-style fatal exception codes (process.returncode
# on Windows is the raw exit code, which for a crash is usually one of these
# rather than a small POSIX-style number).
WINDOWS_CRASH_CODES = {
    0xC0000005: "STATUS_ACCESS_VIOLATION",
    0xC00000FD: "STATUS_STACK_OVERFLOW",
    0xC0000409: "STATUS_STACK_BUFFER_OVERRUN",
    0xC0000374: "STATUS_HEAP_CORRUPTION",
    0xC000001D: "STATUS_ILLEGAL_INSTRUCTION",
    0xC0000094: "STATUS_INTEGER_DIVIDE_BY_ZERO",
    0xC0000602: "STATUS_FAIL_FAST_EXCEPTION",
}


def describe_exit(returncode: int) -> str:
    """Turns a raw process exit code into a human-readable label."""
    if returncode == 0:
        return "ok"
    if returncode < 0:
        # POSIX: subprocess reports a negative value equal to -signal_number
        # when the child was killed by a signal.
        sig = -returncode
        try:
            name = signal.Signals(sig).name
        except ValueError:
            name = f"signal {sig}"
        return f"crash({name})"
    if returncode >= 0x80000000:
        # Windows: fatal exceptions surface as large NTSTATUS-style codes.
        name = WINDOWS_CRASH_CODES.get(returncode)
        return f"crash({name or hex(returncode)})"
    return f"exit({returncode})"


def find_games(testdata_dir: Path) -> list[Path]:
    return sorted(testdata_dir.rglob("*.taf"))


def run_one(binary: Path, game: Path, commands: str, timeout: float, seed: int) -> tuple[str, str, str]:
    """Runs a single game, returning (kind, code_or_message, output_hash)."""
    try:
        proc = subprocess.run(
            [str(binary), "--seed", str(seed), str(game)],
            input=commands,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        digest = hashlib.sha256(proc.stdout.encode("utf-8", "surrogateescape")).hexdigest()[:16]
        return ("run", str(proc.returncode), digest)
    except subprocess.TimeoutExpired:
        return ("timeout", "", "")
    except OSError as e:
        return ("error", str(e), "")


def run_all(binary: Path, games: list[Path], commands: str, timeout: float, jobs: int, seed: int):
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {
            pool.submit(run_one, binary, game, commands, timeout, seed): game
            for game in games
        }
        for future in concurrent.futures.as_completed(futures):
            game = futures[future]
            results[game] = future.result()
    return results


def label_for(result: tuple[str, str, str]) -> str:
    """Human-readable description of a (kind, code, digest) result."""
    kind, code, _digest = result
    if kind == "run":
        return describe_exit(int(code))
    if kind == "timeout":
        return "timeout"
    return f"error({code})"


def format_line(game: Path, testdata_dir: Path, result: tuple[str, str, str]) -> str:
    rel = game.relative_to(testdata_dir)
    digest = result[2] or "-"
    return f"{label_for(result):<20} {digest:<16} {rel}"


def write_results(path: Path, testdata_dir: Path, results: dict) -> None:
    # Tab-separated and using the raw (kind, code, digest) fields, so this
    # round-trips exactly through parse_results_file regardless of spaces in
    # game filenames or error messages.
    lines = []
    for game in sorted(results):
        kind, code, digest = results[game]
        rel = game.relative_to(testdata_dir)
        lines.append(f"{kind}\t{code}\t{digest}\t{rel}")
    path.write_text("\n".join(lines) + "\n")


def print_results(testdata_dir: Path, results: dict) -> None:
    for game in sorted(results):
        print(format_line(game, testdata_dir, results[game]))


def parse_results_file(path: Path) -> dict:
    entries = {}
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        kind, code, digest, rel = line.split("\t", 3)
        entries[rel] = (kind, code, digest)
    return entries


def is_healthy(result: tuple[str, str, str]) -> bool:
    kind, code, _digest = result
    return kind == "run" and code == "0"


def diff_results(before_path: Path, after_path: Path, crashes_only: bool = False) -> bool:
    before = parse_results_file(before_path)
    after = parse_results_file(after_path)
    all_keys = sorted(set(before) | set(after))

    if crashes_only:
        # In-development mode: only newly-introduced crashes/timeouts/errors
        # among games present in both runs count as a failure. New games
        # (no prior baseline), removed games, and behavior changes that
        # don't affect exit health (e.g. a transcript hash change) are
        # reported but don't fail the check.
        regressions = []
        for rel in all_keys:
            b = before.get(rel)
            a = after.get(rel)
            if b is None:
                print(f"{rel}: new game, {label_for(a)}")
            elif a is None:
                print(f"{rel}: removed (was {label_for(b)})")
            elif b != a:
                note = " -- REGRESSION" if is_healthy(b) and not is_healthy(a) else ""
                print(f"{rel}: before={label_for(b)} after={label_for(a)}{note}")
                if note:
                    regressions.append(rel)
        if regressions:
            print(f"\n{len(regressions)} game(s) newly crashing/failing vs baseline:")
            for rel in regressions:
                print(f"  {rel}")
            return False
        print(f"\nNo newly crashing games vs baseline ({len(all_keys)} game files compared)")
        return True

    identical = True
    missing = ("missing", "", "")
    for rel in all_keys:
        b = before.get(rel, missing)
        a = after.get(rel, missing)
        if b != a:
            identical = False
            print(f"{rel}:")
            print(f"  before: {label_for(b)} {b[2]}")
            print(f"  after:  {label_for(a)} {a[2]}")
    if identical:
        print(f"NONE - identical status for all {len(all_keys)} game files")
    return identical


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--testdata-dir", type=Path, default=Path("testdata"),
                         help="directory to search for .taf files (default: testdata)")
    parser.add_argument("--binary", type=Path, default=Path("build/starlane-console/starlane-console"),
                         help="path to the starlane-console binary")
    parser.add_argument("--commands", default=DEFAULT_COMMANDS,
                         help="literal command sequence to feed each game, \\n-separated")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                         help="per-game timeout in seconds (default: %(default)s)")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 1,
                         help="number of games to run in parallel (default: number of CPUs)")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED,
                         help="RNG seed to pass to starlane-console (default: %(default)s; "
                              "0 means nondeterministic and defeats output-hash diffing)")
    parser.add_argument("--output", "-o", type=Path,
                         help="write results to this file instead of (or in addition to) stdout")
    parser.add_argument("--diff", nargs=2, metavar=("BEFORE", "AFTER"), type=Path,
                         help="skip running games; instead diff two previously written result files")
    parser.add_argument("--crashes-only", action="store_true",
                         help="with --diff, only fail on games that were passing in BEFORE and are now "
                              "crashing/timing out/erroring in AFTER; report (but don't fail on) new "
                              "games, removed games, or non-health-affecting changes (e.g. transcript "
                              "hash changes)")
    args = parser.parse_args()

    if args.diff:
        identical = diff_results(*args.diff, crashes_only=args.crashes_only)
        return 0 if identical else 1

    if not args.binary.exists():
        parser.error(f"binary not found: {args.binary}")
    if not args.testdata_dir.is_dir():
        parser.error(f"testdata directory not found: {args.testdata_dir}")

    commands = args.commands.replace("\\n", "\n")
    games = find_games(args.testdata_dir)
    if not games:
        parser.error(f"no .taf files found under {args.testdata_dir}")

    results = run_all(args.binary, games, commands, args.timeout, args.jobs, args.seed)

    if args.output:
        write_results(args.output, args.testdata_dir, results)
        print(f"Wrote {len(results)} results to {args.output}")
    else:
        print_results(args.testdata_dir, results)

    failures = [g for g, r in results.items() if r[0] != "run" or r[1] != "0"]
    if failures:
        print(f"\n{len(failures)} game(s) did not exit cleanly:", file=sys.stderr)
        for g in sorted(failures):
            print(f"  {g.relative_to(args.testdata_dir)}: {label_for(results[g])}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
