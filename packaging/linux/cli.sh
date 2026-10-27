#!/bin/sh
# RAWR convenience wrapper for Linux.
# Put this next to the rawr binary and call ./cli.sh <game folder>
set -e
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$DIR/rawr" "$@"
