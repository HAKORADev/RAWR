#!/bin/sh
# Runs the whole RAWR test suite. Called by `rawr selftest <tests folder>`.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RAWR="${1:-$HERE/../build/rawr}"
echo "== RAWR test suite =="
"$RAWR" selftest || exit 1
echo
sh "$HERE/e2e.sh" "$RAWR" || exit 1
