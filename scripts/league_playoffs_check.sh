#!/usr/bin/env bash
#
# league_playoffs_check.sh -- does our port of the Mod's season-end play-offs decide as the
# Mod's own code does? (src/league_playoffs.c; docs/WORLD_2026_27.md, phase 4)
#
#   scripts/league_playoffs_check.sh [SEASONS]      # default 20000
#
# Builds, natively, the Mod's tools/dplay.c and tools/gplay.c as they are (MOD=<its checkout>,
# default D:\Projects\GitHub\swos-9697-mod) and our port, gives both the same random final
# tables for Italy 2026-27's 14 divisions, season after season, and compares every move.
# The Mod's side walks the pack's whole knockout spec (its compile_spec, every kind); ours
# the part world_import.py compiles (promotion, play-out) -- the rest must not change them.
# Then the B-team rules (its sec_post): built cases, and random ones against the Mod's.
# Needs the world's data (bin/data_json/worlds/2627/mod/, not in git): skipped without it.
set -euo pipefail
cd "$(dirname "$0")/.."
N=${1:-20000}
MOD=${MOD:-/mnt/d/Projects/GitHub/swos-9697-mod}
DATA=bin/data_json/worlds/2627/mod
T=tests/league_playoffs
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
if [ ! -d "$DATA" ] || [ ! -f "$MOD/tools/gplay.c" ]; then
    echo "SKIP: needs $DATA and the Mod's tools ($MOD)"; exit 0
fi
python3 -I - "$MOD" "$OUT" <<'PY'
import importlib.util, sys
mod, out = sys.argv[1], sys.argv[2]
def load(name, path):
    s = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
sys.path.insert(0, mod + "/tools")
g = load("gplay", mod + "/tools/gplay.py")
w = load("world_import", "scripts/world_import.py")
lg = w.mod_league()
nd = len(lg["divisions"])
open(out + "/mod.spec", "wb").write(bytes(g.compile_spec(lg["ko_playoffs"], nd)))
open(out + "/ours.spec", "wb").write(w.italian_playoffs(20, None)["spec"])
PY
CC="gcc -O1 -w"
$CC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -c -o "$OUT/mod_gp.o" $T/mod_gp.c
$CC -DDPLAY_C="\"$MOD/tools/dplay.c\"" -c -o "$OUT/mod_dp.o" $T/mod_dp.c
$CC -c -o "$OUT/mod_main.o" $T/mod_main.c
$CC -o "$OUT/mod" "$OUT/mod_main.o" "$OUT/mod_dp.o" "$OUT/mod_gp.o"
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/ours" $T/ours.c
"$OUT/mod" "$OUT/mod.spec" "$N" > "$OUT/mod.txt"
"$OUT/ours" "$OUT/ours.spec" "$N" > "$OUT/ours.txt" 2>/dev/null   # (its [playoffs] log)
if cmp -s "$OUT/mod.txt" "$OUT/ours.txt"; then
    moves=$(awk '{ m += NF - 1 } END { print m }' "$OUT/ours.txt")
    echo "PASS: $N seasons, $moves moves, every one the Mod's"
else
    echo "FAIL: the first season that differs (Mod, then ours):"
    diff "$OUT/mod.txt" "$OUT/ours.txt" | head -4
    exit 1
fi

# The B-team rules (the Mod's sec_post): built cases, then random ones against the Mod's.
$CC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/mod_sec" $T/mod_sec.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/seconds" $T/seconds.c
"$OUT/seconds" 2>/dev/null
"$OUT/mod_sec" "$N" > "$OUT/mod_sec.txt"
"$OUT/seconds" random "$N" > "$OUT/sec.txt" 2> "$OUT/sec.log"
if cmp -s "$OUT/mod_sec.txt" "$OUT/sec.txt"; then
    echo "PASS: B teams, $N cases ($(grep -o '^[0-9]* of' "$OUT/sec.log" | cut -d' ' -f1) changed by the rules), every move the Mod's"
else
    echo "FAIL: B teams, the first case that differs (Mod, then ours):"
    diff "$OUT/mod_sec.txt" "$OUT/sec.txt" | head -4
    exit 1
fi
