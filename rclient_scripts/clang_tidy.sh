#!/usr/bin/env bash
# Install clang-tidy 20 and check code using an existing CMake build directory.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v apt-get >/dev/null || ! command -v sudo >/dev/null; then
  echo "This script requires Ubuntu/WSL with apt-get and sudo." >&2
  exit 2
fi

if ! command -v clang-tidy-20 >/dev/null || ! command -v run-clang-tidy-20 >/dev/null || ! command -v clang-apply-replacements-20 >/dev/null; then
  sudo apt-get update
  sudo apt-get install -y wget ca-certificates gnupg lsb-release
  if apt-cache show clang-tidy-20 >/dev/null 2>&1 && apt-cache show clang-tools-20 >/dev/null 2>&1; then
    sudo apt-get install -y clang-tidy-20 clang-tools-20
  else
    llvm_installer="$(mktemp)"
    trap 'rm -f -- "$llvm_installer"' EXIT
    wget -qO "$llvm_installer" https://apt.llvm.org/llvm.sh
    sudo bash "$llvm_installer" 20 all
  fi
fi

cd "$ROOT"
if [[ "${1:-}" == "--install-only" ]]; then
  exit 0
elif [[ "${1:-}" == "--all" ]]; then
  shift
  build_dir="${CLANG_TIDY_BUILD_DIR:-$ROOT/clang-tidy}"
  jobs="${JOBS:-6}"
  if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "Missing $build_dir/compile_commands.json. Configure the project with CMake and build its generated headers first." >&2
    exit 2
  fi
  if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive integer." >&2
    exit 2
  fi
  echo "Checking all files from $build_dir/compile_commands.json with $jobs workers."
  run-clang-tidy-20 -p "$build_dir" -j "$jobs" \
    -clang-tidy-binary clang-tidy-20 \
    -clang-apply-replacements-binary clang-apply-replacements-20 \
    '-warnings-as-errors=*' "$@"
else
  python3 "$ROOT/rclient_scripts/clang_tidy.py" "$@"
fi
