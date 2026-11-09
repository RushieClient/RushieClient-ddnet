#!/usr/bin/env python3
"""Check RClient C/C++ files using a configured clang-tidy compilation database.

Run from Ubuntu/WSL after configuring the project:
    python3 rclient_scripts/clang_tidy.py
    python3 rclient_scripts/clang_tidy.py --fix
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
RCLIENT = ROOT / "src/game/client/components/rclient"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "clang-tidy",
                        help="CMake build directory with compile_commands.json")
    parser.add_argument("-j", "--jobs", type=int, default=min(os.cpu_count() or 1, 6),
                        help="number of parallel checks (default: up to 6)")
    parser.add_argument("--fix", action="store_true",
                        help="apply compatible automatic fixes after analysis")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")

    build_dir = args.build_dir.resolve()
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        print(f"Compilation database not found: {database}\n"
              "Configure CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and build "
              "the generated headers first. Use --build-dir to select it.", file=sys.stderr)
        return 2

    binaries = ("run-clang-tidy-20", "clang-tidy-20", "clang-apply-replacements-20")
    required = binaries if args.fix else binaries[:2]
    missing = [binary for binary in required if not shutil.which(binary)]
    if missing:
        print(f"Missing tools: {', '.join(missing)}. Install clang-tidy-20 in Ubuntu/WSL.",
              file=sys.stderr)
        return 2

    try:
        with database.open(encoding="utf-8") as file:
            entries = json.load(file)
        sources = set()
        for entry in entries:
            source = Path(entry["file"])
            if not source.is_absolute():
                source = Path(entry["directory"]) / source
            source = source.resolve()
            if source.is_relative_to(RCLIENT) and "external" not in source.relative_to(RCLIENT).parts:
                sources.add(source)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Cannot read compilation database: {error}", file=sys.stderr)
        return 2
    if not sources:
        print("No RClient source files found in the compilation database. "
              "Configure the client build in this environment and try again.", file=sys.stderr)
        return 2

    # run-clang-tidy selects compilation database entries with Python regexes.
    source_filter = r"^" + re.escape(str(RCLIENT)) + r"/(?!external/)"
    command = ["run-clang-tidy-20", "-p", str(build_dir), "-j", str(args.jobs),
               "-clang-tidy-binary", "clang-tidy-20", "-warnings-as-errors", "*",
               source_filter]
    if args.fix:
        command += ["-fix", "-clang-apply-replacements-binary", "clang-apply-replacements-20"]
    print(f"Checking {len(sources)} RClient source files with {args.jobs} workers. "
          "Included headers are checked through these files.", flush=True)
    return subprocess.run(command, cwd=ROOT, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
