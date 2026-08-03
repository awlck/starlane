#!/usr/bin/env python3
"""Fetches third-party game files referenced by testdata/*/source.txt.

Most games under testdata/ were pulled from the adrift.co game directory and
are not ours to redistribute, so they aren't checked into the repo. Instead,
each game's directory has a source.txt describing where to download it from.
This script reads every source.txt, downloads the referenced file(s), and
(for .blorb and .zip downloads) extracts the actual .taf game file, leaving
each game's testdata/<game>/ directory populated as if the file had been
downloaded by hand.

source.txt grammar, one entry per non-empty line:

    <url>                       -- bare URL; saved as <game>/<game>.taf
    "<name>": <url>             -- URL with a suggested source filename
    extract file "<name>.taf"   -- pulls <name>.taf out of the *previous*
                                    entry's download, which must be a .zip

A run is idempotent: any game whose expected output file(s) already exist on
disk are left alone and not re-downloaded, so re-running this script (e.g. on
a warm GitHub Actions cache) is cheap.
"""

import argparse
import concurrent.futures
import re
import sys
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

USER_AGENT = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36")
PERJOB_TIMEOUT = 15.0
RETRIES = 3
GLOBAL_TIMEOUT = 180.0

ENTRY_RE = re.compile(r'^"([^"]+)":\s*(\S+)$')
EXTRACT_RE = re.compile(r'^extract file "([^"]+)"$')


class SourceFileError(ValueError):
    pass


def parse_source_file(path: Path) -> list[dict]:
    entries = []
    for lineno, raw_line in enumerate(path.read_text().splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        m = EXTRACT_RE.match(line)
        if m:
            if not entries or "extract" in entries[-1]:
                raise SourceFileError(f"{path}:{lineno}: 'extract file' with no preceding download entry")
            entries[-1]["extract"] = m.group(1)
            continue
        m = ENTRY_RE.match(line)
        if m:
            entries.append({"name": m.group(1), "url": m.group(2)})
            continue
        entries.append({"name": None, "url": line})
    if not entries:
        raise SourceFileError(f"{path}: no entries found")
    return entries


def output_path_for(entry: dict, game_dir: Path, index: int, total: int) -> Path:
    """The final .taf path this entry should produce, before we know the
    downloaded content's actual type. Only used to decide whether the entry
    can be skipped because it's already been fetched."""
    if "extract" in entry:
        if entry['extract'].endswith('.blorb'):
            return (game_dir / entry['extract']).with_suffix('.taf')
        return game_dir / entry["extract"]
    if entry["name"]:
        stem = Path(entry["name"]).stem
        return game_dir / f"{stem}.taf"
    suffix = "" if total == 1 else f"-{index}"
    return game_dir / f"{game_dir.name}{suffix}.taf"


def sniff_kind(data: bytes) -> str:
    if data[:4] == b"FORM":
        return "blorb"
    if data[:4] == b"PK\x03\x04" or data[:4] == b"PK\x05\x06":
        return "zip"
    return "taf"


def download(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    last_error = None
    for attempt in range(RETRIES):
        try:
            with urllib.request.urlopen(req, timeout=PERJOB_TIMEOUT) as resp:
                return resp.read()
        except (urllib.error.URLError, TimeoutError) as e:
            last_error = e
    raise RuntimeError(f"failed to download {url} after {RETRIES} attempts: {last_error}")


def fetch_entry(game_dir: Path, entry: dict, out_path: Path) -> str:
    data = download(entry["url"])
    kind = sniff_kind(data)

    if kind == "zip":
        if "extract" not in entry:
            raise SourceFileError(f"{game_dir}: zip download with no 'extract file' instruction")
        import io
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            member = entry["extract"]
            names = z.namelist()
            if member not in names:
                # Fall back to a case-insensitive match: source.txt is
                # hand-written and zip member casing varies by archive.
                matches = [n for n in names if n.lower() == member.lower()]
                if len(matches) != 1:
                    raise SourceFileError(f"{game_dir}: '{member}' not found in downloaded zip "
                                           f"(contains: {', '.join(names)})")
                member = matches[0]
            data = z.read(member)
            kind = sniff_kind(data)

    if kind == "blorb":
        blorb_path = out_path.with_suffix(".blorb")
        blorb_path.write_bytes(data)
        export_taf_from_blorb(blorb_path, out_path.with_suffix(".taf"))
        blorb_path.unlink()
        return "blorb"

    out_path.write_bytes(data)
    return "taf"


def export_taf_from_blorb(blorb_path: Path, out_path: Path) -> None:
    script = Path(__file__).with_name("blorbtool.py")
    import subprocess
    subprocess.run(
        [sys.executable, str(script), str(blorb_path), "export", "ADRI", str(out_path)],
        check=True, capture_output=True, text=True,
    )


def process_game(source_txt: Path) -> list[str]:
    game_dir = source_txt.parent
    entries = parse_source_file(source_txt)
    messages = []
    for index, entry in enumerate(entries, start=1):
        out_path = output_path_for(entry, game_dir, index, len(entries))
        if out_path.exists():
            continue
        try:
            kind = fetch_entry(game_dir, entry, out_path)
            messages.append(f"{game_dir.name}: fetched {out_path.name} ({kind})")
        except Exception as e:
            messages.append(f"{game_dir.name}: FAILED to fetch {entry['url']}: {e}")
    return messages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--testdata-dir", type=Path, default=Path("testdata"),
                         help="directory to search for source.txt files (default: testdata)")
    parser.add_argument("--jobs", "-j", type=int, default=8,
                         help="number of downloads to run in parallel (default: %(default)s)")
    args = parser.parse_args()

    source_files = sorted(args.testdata_dir.rglob("source.txt"))
    if not source_files:
        parser.error(f"no source.txt files found under {args.testdata_dir}")

    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(process_game, f): f for f in source_files}
        for future in concurrent.futures.as_completed(futures, timeout=GLOBAL_TIMEOUT):
            source_txt = futures[future]
            try:
                for message in future.result():
                    print(message)
                    if "FAILED" in message:
                        failed = True
            except SourceFileError as e:
                print(f"{source_txt}: {e}")
                failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
