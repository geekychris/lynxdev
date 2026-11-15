#!/usr/bin/env bash
# The games menu: same as ./lynx arcade (see ./lynx arcade --help).
exec "$(cd "$(dirname "$0")/.." && pwd)/lynx" arcade "$@"
