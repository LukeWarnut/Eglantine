#!/bin/zsh

# SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# One cold shader cache run of a fixed route, for comparing builds (shader compile work):
#   mkw  Mario Kart World, boot to the Mushroom Cup load (~85 s)
#   oot  Ocarina of Time, boot to the newest save, then a short walk (~180 s)
#
# cold_cache_run.sh <mkw|oot> <eden-cli binary> <output log> [lab_ctl launch args...]
#
# The title's shader cache folder and eden_log.txt (which eden-cli overwrites) are backed up
# and restored afterwards, so the user's warm cache survives. $D is the lab_ctl diag folder and
# must hold qt-copy.ini (see the nx2-lab-automation skill). WARM=1 keeps the cache warm.
set -u
REPO=${REPO:-${0:A:h:h:h}}
D=${D:?set D to the lab diag folder}
route=$1 binary=$2 out=$3
shift 3
GAMES="/run/media/kraibse/SOINC/emulation/Switch 2/games"
case $route in
mkw)
  title=0400c3f00006e000
  game="$GAMES/mkw.nup"
  seconds=85
  # L+R at 35 s, then A x7 3 s apart: Single Player, Grand Prix, character, kart, Mushroom
  # Cup, 50cc, OK.
  keys="35:9,35:11,35.4:-9,35.4:-11"
  for t in 40 43 46 49 52 55 58 61; do keys+=",$t:4,$t.3:-4"; done
  ;;
oot)
  title=0400486024dec000
  game="$GAMES/The Legend of Zelda Ocarina of TIme [04004860224DEC000].nup"
  seconds=180
  # Title at ~84 s, newest save, confirm; the save loads by ~145 s; then 6 s each way.
  keys="84:4,84.2:-4,93:4,93.2:-4,97:4,97.2:-4"
  t=150
  for code in 82 80 81 79; do keys+=",$t:$code,$((t + 6)):-$code"; t=$((t + 6)); done
  ;;
*)
  echo "unknown route $route (mkw or oot)" >&2
  exit 2
  ;;
esac

cache=~/.cache/eglantine/shader/$title
log=~/.local/share/eglantine/log/eden_log.txt
backup=$(mktemp -d "${TMPDIR:-/tmp}/cold-cache-backup.XXXXXX")
[[ -d $cache ]] && cp -a "$cache" "$backup/cache"
[[ -f $log ]] && cp -a "$log" "$backup/eden_log.txt"
restore() {
  if [[ -d $backup/cache ]]; then
    rm -rf "$cache" && cp -a "$backup/cache" "$cache"
  fi
  [[ -f $backup/eden_log.txt ]] && cp -a "$backup/eden_log.txt" "$log"
  rm -rf "$backup"
}
trap restore EXIT INT TERM

if [[ ${WARM:-0} != 1 ]]; then
  find "$cache" \( -name 'vulkan*.bin' -o -name 'warm-compute.done' \) -delete 2>/dev/null
fi
[[ -f $D/qt-copy.ini ]] &&
  /usr/bin/python3 "$REPO/.claude/skills/nx2-lab-automation/scripts/mute_audio.py" "$D/qt-copy.ini"

cd "$REPO" || exit 1
L=(python3 -I tools/nx2/lab_ctl.py -d "$D")
pid=$($L launch --hidden --max-seconds $seconds --config "$D/qt-copy.ini" --binary "$binary" \
  --keys "$keys" --game "$game" "$@" | python3 -I -c 'import json, sys; print(json.load(sys.stdin)["pid"])')
# By pid: pgrep -f on the binary name would match this shell's own command line.
while kill -0 $pid 2>/dev/null; do sleep 1; done
cp "$D/emulator.log" "$out"
echo "$route $(basename $binary): $(grep -a -c 'Key script:' "$out") keys, log $out"
