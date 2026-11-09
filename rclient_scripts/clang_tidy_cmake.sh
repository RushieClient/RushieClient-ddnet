#!/usr/bin/env bash
# Configure and build the project, then check all compilation database entries.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${CLANG_TIDY_BUILD_DIR:-$ROOT/clang-tidy}"
JOBS="${JOBS:-6}"

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "JOBS must be a positive integer." >&2
  exit 2
fi

bash "$SCRIPT_DIR/clang_tidy.sh" --install-only

packages=(cmake ninja-build pkg-config git python3 clang-20
  libavcodec-dev libavformat-dev libavutil-dev libfreetype6-dev libglew-dev
  libmariadb-dev libminiupnpc-dev libnotify-dev libsdl2-dev libsqlite3-dev
  libswresample-dev libswscale-dev libwebsockets-dev libx264-dev
  libvulkan-dev glslang-tools libgtest-dev libgmock-dev libssl-dev)
missing=()
for package in "${packages[@]}"; do
  if [[ "$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true)" != 'install ok installed' ]]; then
    missing+=("$package")
  fi
done
if ! command -v cargo >/dev/null || ! command -v rustc >/dev/null; then
  missing+=(cargo rustc)
fi
if (( ${#missing[@]} )); then
  sudo apt-get update
  sudo apt-get install -y "${missing[@]}"
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_C_CLANG_TIDY= -DCMAKE_CXX_CLANG_TIDY= \
  -DCMAKE_BUILD_TYPE=Debug \
  -DANTIBOT=ON -DAUTOUPDATE=ON -DDISCORD=ON -DDOWNLOAD_GTEST=ON \
  -DMYSQL=ON -DSTEAM=ON -DUPNP=ON -DVIDEORECORDER=ON -DVULKAN=ON -DWEBSOCKETS=ON \
  -Werror=dev

export CARGO_HTTP_MULTIPLEXING=false
cmake --build "$BUILD_DIR" --config Debug --target everything testrunner --parallel "$JOBS" -- -k 0
bash "$SCRIPT_DIR/clang_tidy.sh" --all -fix "$@"
