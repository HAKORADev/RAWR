#!/bin/sh
# Install RAWR for the current user.
#   ./install.sh            -> ~/.local/bin/rawr
#   ./install.sh /usr/local -> /usr/local/bin/rawr (needs root)
set -e
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX="${1:-$HOME/.local}"
BIN="$PREFIX/bin"

if [ ! -x "$DIR/rawr" ]; then
    echo "rawr binary not found next to this script" >&2
    exit 1
fi

mkdir -p "$BIN"
cp "$DIR/rawr" "$BIN/rawr"
chmod +x "$BIN/rawr"
ln -sf "$BIN/rawr" "$BIN/cli.sh"
echo "installed: $BIN/rawr"

case ":$PATH:" in
    *":$BIN:"*) ;;
    *) echo "note: $BIN is not in your PATH" ;;
esac
