#!/usr/bin/env bash
# RAWR corpus sweep: copy each extracted game, run RAWR, verify the result.
set -u
RAWR="${1:-/workspace/RAWR/build/rawr}"
CORPUS="${2:-/workspace/corpus/extracted}"
WORK=/workspace/rawr_sweep
rm -rf "$WORK"; mkdir -p "$WORK"

pass=0; fail=0; skip=0
printf "%-34s %-10s %-9s %s\n" "GAME" "RESULT" "METHOD" "DETAIL"
printf "%s\n" "--------------------------------------------------------------------------------"

for d in "$CORPUS"/*/; do
    name=$(basename "$d")
    [ -d "$d" ] || continue
    # The game root is whichever directory actually holds the wrapper bits.
    inner=$(find "$d" -maxdepth 2 \( -iname "*.rwg" -o -iname "ReflexiveArcade" \) -printf "%h\n" 2>/dev/null | sort -u | tail -1)
    [ -n "$inner" ] || inner=$(find "$d" -maxdepth 1 -mindepth 1 -type d | head -1)
    [ -n "$inner" ] || inner="$d"

    rm -rf "$WORK/t"; mkdir -p "$WORK/t"
    cp -r "$inner" "$WORK/t/game" 2>/dev/null || { printf "%-34s %-10s\n" "$name" "COPYFAIL"; continue; }

    out=$("$RAWR" --json "$WORK/t/game" 2>&1)
    status=$(echo "$out" | python3 -c "import json,sys
try:
    d=json.load(sys.stdin); r=d['results'][0]; print(r['status']+'|'+r.get('method','')+'|'+r.get('detail','')[:60])
except Exception as e: print('parsefail||')" 2>/dev/null)
    st=$(echo "$status" | cut -d'|' -f1)
    method=$(echo "$status" | cut -d'|' -f2)
    detail=$(echo "$status" | cut -d'|' -f3)

    if [ "$st" = "cleaned" ]; then
        # verify: wrapper gone, exe is a PE, no RWG left
        left=$(find "$WORK/t/game" -maxdepth 2 -iname '*.rwg' -o -maxdepth 2 -iname 'ReflexiveArcade' -type d | wc -l)
        pe=$(file -b "$WORK/t/game"/*.exe 2>/dev/null | head -1 | cut -c1-40)
        if [ "$left" != "0" ]; then
            printf "%-34s %-10s %-9s %s\n" "$name" "LEFTOVER" "$method" "wrapper bits still present"
            fail=$((fail+1)); continue
        fi
        case "$pe" in
            *PE32*|*MS-DOS*) printf "%-34s %-10s %-9s %s\n" "$name" "PASS" "$method" "$pe"; pass=$((pass+1));;
            *) printf "%-34s %-10s %-9s %s\n" "$name" "BADPE" "$method" "${pe:0:50}"; fail=$((fail+1));;
        esac
    elif [ "$st" = "already-clean" ]; then
        printf "%-34s %-10s %-9s %s\n" "$name" "SKIP" "-" "$detail"; skip=$((skip+1))
    else
        printf "%-34s %-10s %-9s %s\n" "$name" "FAIL" "$method" "$detail"; fail=$((fail+1))
    fi
done

printf "%s\n" "--------------------------------------------------------------------------------"
printf "PASS=%d FAIL=%d SKIP=%d TOTAL=%d\n" "$pass" "$fail" "$skip" "$((pass+fail+skip))"
