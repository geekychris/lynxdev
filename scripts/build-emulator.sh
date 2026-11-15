#!/usr/bin/env bash
# Build Handy (libretro-handy core) natively with the lynxh harness:
# deterministic frame stepping, control socket, guest log, breakpoints,
# input recording, states. See emulator/README.md.
#
# Output: third_party/handy/build/lynxh       headless (no SDL)
#         third_party/handy/build/lynxh-sdl   window (SDL2), if SDL2 is found
#
# Needs: a C/C++ compiler and make; SDL2 for the window build.
#   macOS:  xcode-select --install; brew install sdl2
#   Debian: sudo apt install build-essential libsdl2-dev
# No BIOS is needed or fetched: the HLE boot is used (lynxboot.img is
# copyrighted; never download or commit it).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -f "$ROOT/scripts/lib.sh" ]; then
  # shellcheck source=lib.sh
  source "$ROOT/scripts/lib.sh"
fi
# pins (lib.sh owns them; fallbacks so this script also runs on its own)
HANDY_REPO="${HANDY_REPO:-https://github.com/libretro/libretro-handy.git}"
HANDY_COMMIT="${HANDY_COMMIT:-bc55d462f0b2d6b073ea93dc552ebd73cec60fd1}"
THIRD_PARTY="${THIRD_PARTY:-$ROOT/third_party}"
HANDY_DIR="${HANDY_DIR:-$THIRD_PARTY/handy}"
LYNXH_BIN="${LYNXH_BIN:-$HANDY_DIR/build/lynxh}"
LYNXH_SDL_BIN="${LYNXH_SDL_BIN:-$HANDY_DIR/build/lynxh-sdl}"

if ! declare -F step >/dev/null; then
  step() { printf '==> %s\n' "$*"; }
  ok()   { printf '  ✓ %s\n' "$*"; }
  warn() { printf '  ! %s\n' "$*" >&2; }
  die()  { printf '  ✗ %s\n' "$*" >&2; exit 1; }
fi
have() { command -v "$1" >/dev/null 2>&1; }
ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4; }

step "Handy (libretro-handy @ ${HANDY_COMMIT:0:10}) + lynxh harness"
have git || die "git is required"
have make || die "make is required (macOS: xcode-select --install; Linux: build-essential)"
CXX="${CXX:-c++}"; CC="${CC:-cc}"
have "$CXX" || die "a C++ compiler is required (macOS: xcode-select --install; Linux: build-essential)"
want_sdl=1; [ -n "${LYNXH_NO_SDL:-}" ] && want_sdl=0
have sdl2-config || { warn "sdl2-config not found: building the headless emulator only (macOS: brew install sdl2; Debian: apt install libsdl2-dev)"; want_sdl=0; }

# the pinned source (fetched once, never built in)
if [ ! -d "$HANDY_DIR/.git" ]; then
  mkdir -p "$HANDY_DIR"
  git -C "$HANDY_DIR" init -q
  git -C "$HANDY_DIR" remote add origin "$HANDY_REPO"
fi
if [ "$(git -C "$HANDY_DIR" rev-parse HEAD 2>/dev/null || true)" != "$HANDY_COMMIT" ]; then
  git -C "$HANDY_DIR" fetch -q --depth 1 origin "$HANDY_COMMIT"
  git -C "$HANDY_DIR" -c advice.detachedHead=false checkout -q -f FETCH_HEAD
fi
ok "source $HANDY_DIR"

# a private copy with our patches + harness
B="$THIRD_PARTY/handy-build"
SRC="$B/src"
rm -rf "$SRC"; mkdir -p "$SRC" "$B/obj"
( cd "$HANDY_DIR" && git archive HEAD ) | tar -x -C "$SRC"
for p in "$ROOT"/emulator/patches/*.patch; do
  [ -e "$p" ] || continue
  ( cd "$SRC" && patch -s -p1 <"$p" ) || die "patch failed: $(basename "$p")"
  ok "applied $(basename "$p")"
done
cp "$ROOT"/emulator/ext/*.h "$ROOT"/emulator/ext/*.inc "$SRC/lynx/"

LC="$SRC/libretro-common"
INC=(-I"$SRC/lynx" -I"$SRC/libretro" -I"$SRC" -I"$LC/include")
CXXFLAGS=(-O2 -g -DLYNXH -DWANT_CRC32 -DHAVE_STDINT_H -Wno-everything -std=gnu++11 "${INC[@]}")
CFLAGS=(-O2 -g -Wno-everything "${INC[@]}")
CORE_CXX=(lynx/lynxdec.cpp lynx/cart.cpp lynx/memmap.cpp lynx/mikie.cpp lynx/ram.cpp lynx/rom.cpp
          lynx/susie.cpp lynx/system.cpp lynx/eeprom.cpp blip/Blip_Buffer.cpp blip/Stereo_Buffer.cpp)
CORE_C=(compat/compat_posix_string.c compat/compat_snprintf.c compat/compat_strcasestr.c compat/compat_strl.c
        compat/fopen_utf8.c encodings/encoding_utf.c file/file_path.c file/file_path_io.c streams/file_stream.c
        streams/file_stream_transforms.c string/stdstring.c time/rtime.c vfs/vfs_implementation.c)
log="$B/build.log"; : >"$log"

# compile in parallel with make (one rule per object)
mk="$B/Makefile"
{
  echo "OBJS :="
  for f in "${CORE_CXX[@]}" lynx/lynxh.cpp; do
    o="$B/obj/$(echo "$f" | tr '/' '_').o"
    echo "OBJS += $o"
    echo "$o: $SRC/$f; \$(CXX) ${CXXFLAGS[*]} -c \$< -o \$@"
  done
  for f in "${CORE_C[@]}"; do
    o="$B/obj/lc_$(echo "$f" | tr '/' '_').o"
    echo "OBJS += $o"
    echo "$o: $LC/$f; \$(CC) ${CFLAGS[*]} -c \$< -o \$@"
  done
  echo "all: \$(OBJS)"
} >"$mk"
cp "$ROOT/emulator/ext/lynxh.cpp" "$SRC/lynx/lynxh.cpp"
make -s -f "$mk" -j"$(ncpu)" CXX="$CXX" CC="$CC" all >>"$log" 2>&1 || { tail -40 "$log"; die "compile failed (full log: $log)"; }
OBJS=("$B"/obj/*.o)

"$CXX" "${CXXFLAGS[@]}" -o "$B/lynxh" "$ROOT/emulator/ext/lynxh_headless_main.cpp" "${OBJS[@]}" -lm >>"$log" 2>&1 \
  || { tail -30 "$log"; die "link (headless) failed (full log: $log)"; }
ok "headless emulator built"
if [ "$want_sdl" = 1 ]; then
  # shellcheck disable=SC2046
  "$CXX" "${CXXFLAGS[@]}" $(sdl2-config --cflags) -o "$B/lynxh-sdl" "$ROOT/emulator/ext/lynxh_sdl_main.cpp" "${OBJS[@]}" \
    $(sdl2-config --libs) -lm >>"$log" 2>&1 || { tail -30 "$log"; die "link (SDL2) failed (full log: $log)"; }
  ok "window emulator built"
fi

install_bin() {   # copy next to the live binary, then rename over it (running emulators keep the old file)
  local from="$1" to="$2" tmp="$2.new.$$"
  mkdir -p "$(dirname "$to")"
  cp "$from" "$tmp" && chmod +x "$tmp" && mv -f "$tmp" "$to" || { rm -f "$tmp"; die "could not install $to"; }
  ok "installed $to"
}
install_bin "$B/lynxh" "$LYNXH_BIN"
[ "$want_sdl" = 1 ] && install_bin "$B/lynxh-sdl" "$LYNXH_SDL_BIN"
cp "$log" "$(dirname "$LYNXH_BIN")/build.log"
ok "done"
