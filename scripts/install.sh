#!/usr/bin/env bash
#
# lynxdev: one-shot installer for the Atari Lynx dev environment.
#
# Clones (or updates) lynxdev with its games/ submodule (lynx_games), checks
# prerequisites, then runs ./setup.sh: the cc65 toolchain image, the patched
# Handy emulator, the Python env, and the selftest. At the end it opens the
# games menu (./lynx arcade). Re-running is safe: the checkout is updated and
# every step is idempotent.
#
# Once the repos are public:
#   curl -fsSL https://raw.githubusercontent.com/geekychris/lynxdev/main/scripts/install.sh | bash
# While they are private (needs the GitHub CLI, logged in: gh auth login):
#   gh api -H "Accept: application/vnd.github.raw" repos/geekychris/lynxdev/contents/scripts/install.sh | bash
#   curl -fsSL -H "Authorization: token $(gh auth token)" \
#     https://raw.githubusercontent.com/geekychris/lynxdev/main/scripts/install.sh | bash
# (the clone then authenticates through gh too; or use SSH: LYNX_DEV_REPO=git@github.com:geekychris/lynxdev.git)
#
# macOS (Apple Silicon / Intel) and Linux (x86_64 / arm64). Windows: run it
# inside WSL2 (Ubuntu).
#
# Environment knobs:
#   LYNX_DEV_SRC           install dir                  (default: $HOME/lynxdev)
#   LYNX_DEV_REF           git branch/tag/commit        (default: main)
#   LYNX_DEV_REPO          git remote                   (default: https://github.com/geekychris/lynxdev.git)
#   LYNX_DEV_AUTO_INSTALL  1 = install missing prerequisites with brew/apt/dnf/pacman (asks for sudo)
#   LYNX_DEV_QUICK         1 = skip the final selftest  (./setup.sh --quick)
#   LYNX_DEV_START         0 = don't open the games menu at the end (default: 1 with a terminal + display)
#   LYNX_DEV_USE_GH        0 = don't let git authenticate through the GitHub CLI (default: use it if logged in)

set -euo pipefail

SRC="${LYNX_DEV_SRC:-$HOME/lynxdev}"
REF="${LYNX_DEV_REF:-main}"
REPO="${LYNX_DEV_REPO:-https://github.com/geekychris/lynxdev.git}"
AUTO_INSTALL="${LYNX_DEV_AUTO_INSTALL:-0}"
QUICK="${LYNX_DEV_QUICK:-0}"
USE_GH="${LYNX_DEV_USE_GH:-1}"

c_reset=$'\033[0m'; c_bold=$'\033[1m'
c_green=$'\033[32m'; c_yellow=$'\033[33m'; c_red=$'\033[31m'; c_blue=$'\033[34m'
step() { printf "%s==>%s %s%s%s\n" "$c_blue" "$c_reset" "$c_bold" "$*" "$c_reset"; }
ok()   { printf "%s ✓%s %s\n" "$c_green" "$c_reset" "$*"; }
warn() { printf "%s ! %s%s\n" "$c_yellow" "$c_reset" "$*"; }
die()  { printf "%s ✗ %s%s\n" "$c_red" "$c_reset" "$*" >&2; exit 1; }

# curl | bash gets a minimal PATH; surface the usual extra bin dirs.
for _d in /opt/homebrew/bin /usr/local/bin "$HOME/.local/bin" /snap/bin; do
  if [ -d "$_d" ]; then
    case ":$PATH:" in *:"$_d":*) ;; *) PATH="$_d:$PATH" ;; esac
  fi
done
export PATH

# ---- platform -------------------------------------------------------------
case "$(uname -s)" in
  Darwin) OS=mac ;;
  Linux)
    OS=linux
    if [ -r /etc/os-release ]; then
      . /etc/os-release
      case "${ID:-}:${ID_LIKE:-}" in
        ubuntu*|debian*|linuxmint*|pop*|*:*debian*|*:*ubuntu*) OS=debian ;;
        fedora*|rhel*|centos*|rocky*|almalinux*|*:*fedora*|*:*rhel*) OS=fedora ;;
        arch*|manjaro*|*:*arch*) OS=arch ;;
      esac
    fi ;;
  *) die "unsupported OS $(uname -s): on Windows use WSL2 (wsl --install -d Ubuntu)" ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }

install_hint() {
  case "$OS:$1" in
    mac:git|mac:make|mac:cc|mac:c++) echo "xcode-select --install" ;;
    mac:docker)      echo "install Docker Desktop, Rancher Desktop, OrbStack (brew install --cask orbstack) or colima (brew install colima docker && colima start)" ;;
    mac:python3)     echo "brew install python@3.12  (or: brew install uv)" ;;
    mac:sdl2)        echo "brew install sdl2" ;;
    mac:*)           echo "brew install $1" ;;
    debian:docker)   echo "sudo apt-get install -y docker.io && sudo usermod -aG docker \$USER  (then log out/in)" ;;
    debian:python3)  echo "sudo apt-get install -y python3 python3-venv python3-pip" ;;
    debian:make|debian:cc|debian:c++) echo "sudo apt-get install -y build-essential" ;;
    debian:sdl2)     echo "sudo apt-get install -y libsdl2-dev" ;;
    debian:zlib)     echo "sudo apt-get install -y zlib1g-dev" ;;
    debian:*)        echo "sudo apt-get install -y $1" ;;
    fedora:docker)   echo "sudo dnf install -y moby-engine && sudo systemctl enable --now docker && sudo usermod -aG docker \$USER" ;;
    fedora:make|fedora:cc|fedora:c++) echo "sudo dnf install -y make gcc gcc-c++" ;;
    fedora:sdl2)     echo "sudo dnf install -y SDL2-devel" ;;
    fedora:zlib)     echo "sudo dnf install -y zlib-devel" ;;
    fedora:*)        echo "sudo dnf install -y $1" ;;
    arch:docker)     echo "sudo pacman -S --noconfirm docker && sudo systemctl enable --now docker && sudo usermod -aG docker \$USER" ;;
    arch:make|arch:cc|arch:c++) echo "sudo pacman -S --noconfirm base-devel" ;;
    arch:python3)    echo "sudo pacman -S --noconfirm python" ;;
    arch:sdl2)       echo "sudo pacman -S --noconfirm sdl2" ;;
    arch:zlib)       echo "sudo pacman -S --noconfirm zlib" ;;
    arch:*)          echo "sudo pacman -S --noconfirm $1" ;;
    *)               echo "install $1 with your package manager" ;;
  esac
}

# Opt-in only (LYNX_DEV_AUTO_INSTALL=1). sudo reads from /dev/tty so the
# password prompt works under `curl | bash`.
auto_install() {
  local pkg="$1" cmd
  step "installing $pkg (LYNX_DEV_AUTO_INSTALL=1)"
  case "$OS:$pkg" in
    mac:git|mac:make|mac:cc|mac:c++) xcode-select --install || true
                                     die "finish the Xcode command line tools install, then re-run" ;;
    mac:docker)  die "Docker on macOS needs an app: $(install_hint docker)" ;;
    mac:*)       have brew || die "Homebrew missing: https://brew.sh"
                 case "$pkg" in python3) brew install python@3.12 ;; *) brew install "$pkg" ;; esac
                 return ;;
    *:docker)    cmd="$(install_hint docker)"
                 bash -c "${cmd%%  (*}" </dev/tty
                 die "docker installed and $USER added to the docker group: log out and back in (or 'newgrp docker'), then re-run" ;;
  esac
  cmd="$(install_hint "$pkg")"
  [ "$OS" = debian ] && sudo apt-get update -qq </dev/tty
  bash -c "$cmd" </dev/tty
}

require() {
  local bin="$1" pkg="${2:-$1}"
  have "$bin" && return 0
  if [ "$AUTO_INSTALL" = 1 ]; then
    auto_install "$pkg"
    have "$bin" && return 0
  fi
  die "missing $bin: $(install_hint "$pkg")
     (or re-run with LYNX_DEV_AUTO_INSTALL=1 to install prerequisites automatically)"
}

# ---- prerequisites ---------------------------------------------------------
step "prerequisites ($OS, $(uname -m))"
require git
require curl
require make
require cc
require c++
require docker
docker info >/dev/null 2>&1 || die "docker is installed but the daemon isn't reachable.
     macOS: start Docker Desktop / Rancher Desktop / OrbStack / 'colima start'.
     Linux: sudo systemctl start docker, and make sure \$USER is in the docker group."
if ! have uv; then
  require python3
  python3 -c 'import sys; sys.exit(sys.version_info < (3,10))' \
    || die "Python >= 3.10 required (found $(python3 --version)); $(install_hint python3), or install uv: https://docs.astral.sh/uv/"
  if [ "$OS" = debian ] && ! python3 -c 'import ensurepip' >/dev/null 2>&1; then
    require __none__ python3-venv
  fi
fi
ok "git, curl, make, cc, c++, docker, python"
if [ "$OS" != mac ] && ! printf '#include <zlib.h>\nint main(void){return 0;}\n' | cc -x c - -o /dev/null -lz 2>/dev/null; then
  if [ "$AUTO_INSTALL" = 1 ]; then auto_install zlib; else die "zlib headers missing: $(install_hint zlib)"; fi
fi
if ! have sdl2-config; then
  if [ "$AUTO_INSTALL" = 1 ]; then auto_install sdl2 || true; fi
  have sdl2-config && ok "SDL2" \
    || warn "SDL2 missing: no emulator window (the arcade needs it; headless tests work): $(install_hint sdl2)"
else
  ok "SDL2 $(sdl2-config --version)"
fi
if ! have ffmpeg; then
  if [ "$AUTO_INSTALL" = 1 ]; then auto_install ffmpeg || true
  else warn "ffmpeg not found (optional: .mp4 videos; .gif works without): $(install_hint ffmpeg)"; fi
fi

# ---- source ----------------------------------------------------------------
# Private repos: let git authenticate through the GitHub CLI when it's logged
# in (only for these commands; nothing is written to your git config).
GIT=(git)
if [ "$USE_GH" = 1 ] && have gh && gh auth status >/dev/null 2>&1; then
  GIT=(git -c credential.helper= -c "credential.helper=!gh auth git-credential")
  ok "git authenticates through gh ($(gh api user -q .login 2>/dev/null || echo logged in))"
fi
step "source -> $SRC ($REF)"
if [ -d "$SRC/.git" ]; then
  "${GIT[@]}" -C "$SRC" fetch -q origin "$REF"
  if [ -n "$(git -C "$SRC" status --porcelain --untracked-files=no --ignore-submodules=all)" ]; then
    warn "local changes in $SRC: not updating the checkout (commit/stash them to pull $REF)"
  else
    git -C "$SRC" checkout -q "$REF" 2>/dev/null || git -C "$SRC" checkout -q -B "$REF" FETCH_HEAD
    git -C "$SRC" merge -q --ff-only FETCH_HEAD 2>/dev/null || git -C "$SRC" reset -q --hard FETCH_HEAD
  fi
else
  [ ! -e "$SRC" ] || die "$SRC exists and isn't a git checkout: set LYNX_DEV_SRC to another path"
  "${GIT[@]}" clone -q "$REPO" "$SRC" || die "git clone $REPO failed.
     The repos are private for now: log in with the GitHub CLI (gh auth login) and re-run,
     or use SSH: LYNX_DEV_REPO=git@github.com:geekychris/lynxdev.git (see docs/install.md)."
  git -C "$SRC" checkout -q "$REF" 2>/dev/null || {
    "${GIT[@]}" -C "$SRC" fetch -q origin "$REF" && git -C "$SRC" checkout -q -B "$REF" FETCH_HEAD; }
fi
if [[ "$REPO" == git@* ]]; then        # SSH: fetch the submodule over SSH too
  "${GIT[@]}" -C "$SRC" -c url."git@github.com:".insteadOf=https://github.com/ submodule update -q --init --recursive \
    || warn "games/ submodule not fetched (access to geekychris/lynx_games?)"
else
  "${GIT[@]}" -C "$SRC" submodule update -q --init --recursive \
    || warn "games/ submodule not fetched (access to geekychris/lynx_games?): the platform works without it"
fi
ok "$(git -C "$SRC" log -1 --format='%h %s')"
[ -n "$(ls "$SRC"/games/*/Makefile 2>/dev/null)" ] && ok "games: $(ls -d "$SRC"/games/*/ | wc -l | tr -d ' ')"

# ---- setup -----------------------------------------------------------------
args=()
[ "$QUICK" = 1 ] && args+=(--quick)
cd "$SRC"
./setup.sh "${args[@]+"${args[@]}"}"

# ---- the games menu ----------------------------------------------------------
has_display=0
if [ "$OS" = mac ] || [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then has_display=1; fi
START="${LYNX_DEV_START:-$has_display}"
cat <<EOF

${c_green}${c_bold}lynxdev is installed in $SRC${c_reset}

  cd $SRC
  ./lynx arcade             the games menu (build + play in a window)
  ./lynx new --game mygame  start your own game (games/mygame)
  ./lynx run mygame         build it and open it in a window
  ./lynx test mygame        headless test
  ./lynx devbench --open    web UI: screen, debugger, memory, Suzy/Mikey (:3690)
  claude                    Claude Code (MCP server "lynx" is preconfigured)

Docs: README.md, docs/install.md, docs/getting-started.md, docs/game-dev.md, docs/debugging.md
Re-run this installer any time to update.
EOF
if [ "$START" = 1 ] && (exec </dev/tty) 2>/dev/null && have sdl2-config; then
  step "the games menu (q quits)"
  ./lynx arcade </dev/tty || true
fi
