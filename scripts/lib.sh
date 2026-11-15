# Shared helpers for lynxdev scripts. Source this; don't run it.
# shellcheck shell=bash

set -euo pipefail

LYNX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LYNX_ROOT

# ---- pinned upstream versions (bump deliberately, then re-run setup) ----
# Toolchain image: cc65 (pinned commit, after V2.19; same commit as a8dev).
# Bump TOOLCHAIN_IMAGE's number whenever docker/Dockerfile or the pin changes.
CC65_COMMIT="eebf54af545f4ae52f8489a2f6d4d5f93e7942d2"
TOOLCHAIN_IMAGE="lynx-dev-toolchain:1"

# Handy (libretro-handy fork, zlib licence), patched with the lynxh harness
# (emulator/). These two lines belong to the emulator work (lynx-emu).
HANDY_REPO="https://github.com/libretro/libretro-handy.git"
HANDY_COMMIT="${HANDY_COMMIT:-}"

THIRD_PARTY="$LYNX_ROOT/third_party"
HANDY_DIR="$THIRD_PARTY/handy"
# headless and window (SDL2) builds of the patched emulator
LYNXH_BIN="${LYNXH_BIN:-${LYNX_EMULATOR:-$HANDY_DIR/build/lynxh}}"
LYNXH_SDL_BIN="${LYNXH_SDL_BIN:-${LYNX_EMULATOR_WINDOW:-$HANDY_DIR/build/lynxh-sdl}}"

if [ -t 1 ]; then
  _c_b=$'\033[1m'; _c_g=$'\033[32m'; _c_y=$'\033[33m'; _c_r=$'\033[31m'; _c_0=$'\033[0m'
else
  _c_b=""; _c_g=""; _c_y=""; _c_r=""; _c_0=""
fi

step() { printf '%s==> %s%s\n' "$_c_b" "$*" "$_c_0"; }
ok()   { printf '%s  ✓ %s%s\n' "$_c_g" "$*" "$_c_0"; }
warn() { printf '%s  ! %s%s\n' "$_c_y" "$*" "$_c_0" >&2; }
die()  { printf '%s  ✗ %s%s\n' "$_c_r" "$*" "$_c_0" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

host_os()   { uname -s | tr '[:upper:]' '[:lower:]'; }   # darwin | linux
host_arch() { uname -m; }                                  # arm64 | x86_64 | aarch64

ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4; }

# Clone `repo` into `dir` and check out exactly `commit` (shallow).
git_checkout_pinned() {
  local repo="$1" dir="$2" commit="$3"
  if [ ! -d "$dir/.git" ]; then
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q -f FETCH_HEAD
  fi
}
