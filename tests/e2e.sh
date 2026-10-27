#!/usr/bin/env bash
# End to end test: build the synthetic fixture, unwrap it with RAWR and
# check that the original game program came back.
#
#   ./tests/e2e.sh [path to rawr binary]
set -u
RAWR="${1:-$(dirname "$0")/../build/rawr}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail=0
say() { printf '%s\n' "$*"; }
ok()  { printf '  ok   %s\n' "$*"; }
bad() { printf '  FAIL %s\n' "$*"; fail=$((fail+1)); }

say "RAWR end to end test"
say "  binary: $RAWR"

python3 "$HERE/make_fixture.py" "$WORK/game" > "$WORK/fixture.log" || {
    say "could not build the fixture"; cat "$WORK/fixture.log"; exit 1
}
ok "fixture built"

"$RAWR" --dry-run "$WORK/game" > "$WORK/dry.log" 2>&1
if grep -q "dry run" "$WORK/dry.log"; then ok "dry run reported itself"; else bad "dry run output"; fi
if [ -f "$WORK/game/FixtureGame.RWG" ]; then ok "dry run changed nothing"; else bad "dry run modified the folder"; fi

"$RAWR" "$WORK/game" > "$WORK/run.log" 2>&1
rc=$?
if [ $rc -eq 0 ]; then ok "the unwrap succeeded"; else bad "the unwrap failed (rc=$rc)"; cat "$WORK/run.log"; fi

if [ -f "$WORK/game/FixtureGame.exe" ]; then ok "the game program is in place"; else bad "no game program"; fi
if [ ! -f "$WORK/game/FixtureGame.RWG" ]; then ok "the payload container was removed"; else bad "the payload is still there"; fi
if [ ! -d "$WORK/game/ReflexiveArcade" ]; then ok "the support folder was removed"; else bad "the support folder is still there"; fi

python3 - "$WORK/game/FixtureGame.exe" "$WORK/fixture_game_original.bin" <<'PY'
import sys
recovered = open(sys.argv[1], "rb").read()
original = open(sys.argv[2], "rb").read()
# The recovered program must contain the original code section unchanged.
def text_of(data):
    import struct
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + opt
    for i in range(count):
        off = table + i * 40
        if data[off:off + 5] == b".text":
            raw = struct.unpack_from("<I", data, off + 20)[0]
            size = struct.unpack_from("<I", data, off + 16)[0]
            return data[raw:raw + size]
    return b""
a, b = text_of(recovered), text_of(original)
if a == b:
    print("  ok   the recovered code section is byte identical to the original")
else:
    print("  FAIL the recovered code section differs")
    sys.exit(1)
PY
[ $? -eq 0 ] || fail=$((fail+1))

# Running it a second time must be harmless.
"$RAWR" "$WORK/game" > "$WORK/again.log" 2>&1
if grep -q "already clean\|already-clean" "$WORK/again.log"; then
    ok "a second run reports the folder as already clean"
else
    bad "a second run did not recognise the clean folder"
fi

say ""
if [ $fail -eq 0 ]; then
    say "end to end test passed"
    exit 0
fi
say "end to end test FAILED ($fail problems)"
exit 1
