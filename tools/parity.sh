#!/bin/sh
# parity.sh - native vs browser-reference driving parity for every replay.
#
#   tools/parity.sh [godot-binary]
#
# The browser reference (Three.js + Rapier) has been removed; its replay traces
# and chaos envelopes (spawn nudged +-1 um and +-1 mm) are frozen in
# bench/replays/reference/, recorded from git a59773d. For each replay in
# native/godot/data/replays.json this runs the native build headless and gates
# it with tools/compare-replay.mjs. Exit 1 if any replay fails.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GODOT=${1:-$ROOT/.toolchain/bin/godot}
REF=$ROOT/bench/replays/reference
OUT=$ROOT/bench/replays
status=0

replays=$(python3 -c "import json; d=json.load(open('$ROOT/native/godot/data/replays.json'))['replays']; print(' '.join(f'{k}:{v[\"vehicle\"]}' for k, v in d.items()))")
for entry in $replays; do
    id=${entry%%:*}
    v=${entry##*:}
    base="$REF/reference-$id-v$v"
    if [ ! -f "$base.json" ]; then
        echo "$id: no frozen reference trace ($base.json) - new replays cannot be gated against the removed browser build" >&2
        status=1
        continue
    fi
    "$GODOT" --headless --fixed-fps 60 --path "$ROOT/native/godot" -- --replay="$id" --trace="$OUT/native-$id.json" >/dev/null 2>&1 || exit 2
    node "$ROOT/tools/compare-replay.mjs" "$base.json" "$OUT/native-$id.json" "$base"-p*.json || status=1
done
exit $status
