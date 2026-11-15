# Installing lynxdev

Everything you need to build and run Atari Lynx games: the cc65 compiler
(in Docker), a patched Handy emulator (built from source), the Python tools
(CLI, test harness, MCP server, DevBench web UI) and the games
(`games/`, a git submodule = [lynx_games](https://github.com/geekychris/lynx_games)).
No Lynx boot ROM is needed or downloaded: the emulator boots cartridges
with a high-level emulation of it.

Time: 3-10 minutes (the emulator compiles in about a minute; the cc65 image
in 2-5 minutes the first time).

## One-line install

The installer checks the prerequisites, clones lynxdev with its games to
`~/lynxdev`, runs `./setup.sh` (toolchain image, emulator, Python env,
selftest) and opens the games menu.

**While the repos are private** (now), you need access to
geekychris/lynxdev and geekychris/lynx_games and the
[GitHub CLI](https://cli.github.com/) logged in (`gh auth login`). Either:

```sh
gh api -H "Accept: application/vnd.github.raw" repos/geekychris/lynxdev/contents/scripts/install.sh | bash
```

or with curl and your gh token:

```sh
curl -fsSL -H "Authorization: token $(gh auth token)" \
  https://raw.githubusercontent.com/geekychris/lynxdev/main/scripts/install.sh | bash
```

The installer then clones through gh as well (it passes gh as git's
credential helper for its own git commands only; your git config isn't
changed). With SSH keys instead: add `LYNX_DEV_REPO=git@github.com:geekychris/lynxdev.git`
before `bash` (the submodule is fetched over SSH too).

**Once the repos are public**, no authentication is needed:

```sh
curl -fsSL https://raw.githubusercontent.com/geekychris/lynxdev/main/scripts/install.sh | bash
```

### Knobs

| Variable | Default | |
|---|---|---|
| `LYNX_DEV_SRC` | `~/lynxdev` | where to clone |
| `LYNX_DEV_REF` | `main` | branch, tag or commit |
| `LYNX_DEV_REPO` | `https://github.com/geekychris/lynxdev.git` | remote (SSH: `git@github.com:geekychris/lynxdev.git`) |
| `LYNX_DEV_AUTO_INSTALL` | `0` | `1`: install missing prerequisites with brew / apt / dnf / pacman (asks for sudo) |
| `LYNX_DEV_QUICK` | `0` | `1`: skip the selftest at the end |
| `LYNX_DEV_START` | `1` with a display | `0`: don't open the games menu |
| `LYNX_DEV_USE_GH` | `1` | `0`: don't authenticate git through gh |

Pass them before `bash`, e.g. `... | LYNX_DEV_AUTO_INSTALL=1 LYNX_DEV_SRC=~/src/lynxdev bash`.
Re-running the installer updates the checkout (unless you have local
changes) and re-runs setup; every step is idempotent.

## Manual install

### Prerequisites

| | macOS | Debian / Ubuntu | Fedora | Arch |
|---|---|---|---|---|
| git, make, C/C++ compiler | `xcode-select --install` | `sudo apt install git build-essential` | `sudo dnf install git make gcc gcc-c++` | `sudo pacman -S git base-devel` |
| zlib headers | (included) | `sudo apt install zlib1g-dev` | `sudo dnf install zlib-devel` | `sudo pacman -S zlib` |
| Docker (any engine) | Docker Desktop, OrbStack, Rancher Desktop or colima | `sudo apt install docker.io` + `sudo usermod -aG docker $USER` | `moby-engine` | `docker` |
| Python >= 3.10 (or [uv](https://docs.astral.sh/uv/)) | `brew install python@3.12` or `brew install uv` | `sudo apt install python3 python3-venv` | `python3` | `python` |
| SDL2 (the emulator window; headless works without) | `brew install sdl2` | `sudo apt install libsdl2-dev` | `SDL2-devel` | `sdl2` |
| ffmpeg (optional: `.mp4` videos; `.gif` needs nothing) | `brew install ffmpeg` | `sudo apt install ffmpeg` | `ffmpeg` | `ffmpeg` |

The Docker image is built for your machine (arm64 or amd64); nothing else
runs in Docker. `LYNX_NATIVE=1` builds with a cc65 on your PATH instead of
Docker; it must be as new as the pinned one (cc65 2.19 git, commit
`eebf54a` in scripts/lib.sh). The packaged cc65 2.18 (Homebrew, Debian) is
too old: some games don't compile and sprites draw wrong (bin/lynx-make
warns).

### Steps

```sh
git clone --recurse-submodules https://github.com/geekychris/lynxdev.git
cd lynxdev
./setup.sh          # toolchain image, emulator, .venv, then ./lynx selftest (--quick skips it)
./lynx doctor       # checks every piece
./lynx arcade       # the games menu
```

Private repos: `gh repo clone geekychris/lynxdev -- --recurse-submodules`
works too, or set up gh as your git credential helper once with
`gh auth setup-git`.

Already cloned without `--recurse-submodules`? `git submodule update --init`
(setup.sh does it for you).

What `setup.sh` does, step by step (each is also a script you can run alone):

| Step | Script | Result |
|---|---|---|
| games submodule | `git submodule update --init` | `games/` |
| toolchain | `scripts/build-toolchain-image.sh` | Docker image `lynx-dev-toolchain:1` (cc65 at a pinned commit) |
| emulator | `scripts/build-emulator.sh` | `third_party/handy/build/lynxh` (headless) and `lynxh-sdl` (window) |
| Python | `scripts/setup-python.sh` | `.venv` with the lynxdev package (CLI, MCP server, DevBench) |
| selftest | `./lynx selftest` | builds and tests every project and game |

### Windows

Use WSL2 with Ubuntu (`wsl --install -d Ubuntu`), then follow the
Debian/Ubuntu column inside it, with Docker Desktop's WSL integration
switched on for that distro (or docker-ce inside WSL). The emulator window
needs WSLg (Windows 11) or an X server; headless builds, tests, DevBench
and MCP work either way. This path follows the same steps as the Linux one
but hasn't been tested on a Windows machine yet: tell us what breaks.

## Checking it: `./lynx doctor`

A healthy install looks like this (macOS):

```
==> lynxdev doctor (darwin/arm64, brew)
  ✓ git
  ✓ make
  ✓ C++ compiler (Apple clang version 21.0.0 (clang-2100.3.34.2))
  ✓ uv 0.6.14 (makes .venv)
  ✓ docker running
  ✓ toolchain lynx-dev-toolchain:1: cc65 V2.19 - Git eebf54a
  ✓ SDL2 2.32.10 (emulator window)
  ✓ Handy (headless) ~/lynxdev/third_party/handy/build/lynxh
  ✓ Handy (window) ~/lynxdev/third_party/handy/build/lynxh-sdl
  ✓ Handy at the pinned commit
  ✓ HLE boot (no boot ROM needed; never download one)
  ✓ python env (.venv) with lynxdev
  ✓ headless emulator boots and runs 50 frames
  ✓ games/ submodule checked out (2 games)
  ✓ all good
```

Every problem it finds comes with the command that fixes it, and at the end
one command that installs all missing system packages.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `docker is installed but the daemon isn't reachable` | start Docker Desktop / OrbStack / `colima start`; Linux: `sudo systemctl start docker` |
| `permission denied` on the Docker socket (Linux) | `sudo usermod -aG docker $USER`, log out and back in |
| `git clone ... failed` / `Repository not found` | the repos are private: `gh auth login` and use the gh commands above, or SSH |
| `games/ submodule not fetched` | you have lynxdev but not lynx_games access; the platform works, the games are missing. `git submodule update --init` once you have access |
| `no project 'x' in projects/; games/ ... isn't checked out` | `git submodule update --init` |
| `SDL2 missing: no emulator window` | install SDL2 (table above), then `scripts/build-emulator.sh` |
| `emulator not built` | `scripts/build-emulator.sh` (the error shows its log tail) |
| `python env missing` | `scripts/setup-python.sh` |
| `ffmpeg not found` with an `.mp4` | `brew install ffmpeg` / `apt install ffmpeg`, or record a `.gif` |
| Build fails on a fresh machine with Docker Desktop on Linux | `docker run hello-world` first; corporate proxies need Docker's proxy settings |
| The window opens and closes at once | run `./lynx run hello` from a terminal and read the error; on Linux check `DISPLAY` |
| Port 3690 busy (DevBench) | `./lynx devbench --port 3691` |
| Anything else | `./lynx doctor`; build logs are in `build/` |
