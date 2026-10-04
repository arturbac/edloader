#!/usr/bin/env bash
# Puts edloader into the ed-lab clone with the chain: game -> edworld -> EDVR -> (EDVR's own real_dll) EDHM.
# EDVR (the clone's current d3d11.dll) becomes d3d11_edvr.dll, edworld becomes edworld.dll (next = d3d11.dll).
# Backup goes to the run directory; tools/lab-revert.sh <run dir> puts it back. Does NOT start the game.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
EDW=$HERE/../edworld
GAME=/ext/artur/ed-lab/game/Products/elite-dangerous-odyssey-64
RUN=/ext/artur/ed-lab/tests/edloader-$(date +%Y%m%d-%H%M%S)
[[ -f $HERE/build/d3d11.dll && -f $EDW/build/d3d11.dll ]] || { echo "build edloader and edworld first"; exit 1; }
[[ ! -L $GAME/d3d11.dll ]] || { echo "$GAME/d3d11.dll is a symlink; refusing to write through it"; exit 1; }
if pgrep -f '[E]liteDangerous64|[E]DLaunch' >/dev/null; then echo "a game is running; not touching files"; exit 1; fi
mkdir -p "$RUN/orig"
for f in d3d11.dll d3d11.pdb d3d11_edvr.dll edworld.dll edworld.ini edloader.txt; do
  [[ -e $GAME/$f || -L $GAME/$f ]] && cp -a "$GAME/$f" "$RUN/orig/"
done
[[ -e $GAME/d3d11_edvr.dll ]] || cp -a "$GAME/d3d11.dll" "$GAME/d3d11_edvr.dll"
cp "$HERE/build/d3d11.dll" "$HERE/build/d3d11.pdb" "$GAME/"
cp "$EDW/build/d3d11.dll" "$GAME/edworld.dll"
cat > "$GAME/edloader.txt" <<TXT
# edloader chain, installed by edloader/tools/lab-install.sh
edworld.dll      # read-only panel observer; next = d3d11.dll (back to edloader)
d3d11_edvr.dll   # EDVR; its edvr-flat.ini real_dll chains on to d3d11_edhm.dll itself
TXT
cat > "$GAME/edworld.ini" <<INI
next = d3d11.dll
share = Z:\\dev\\shm\\edworld
log_interval_ms = 1000
INI
{ echo "edloader: $(git -C "$HERE" describe --always --dirty)"; echo "edworld: $(git -C "$EDW" describe --always --dirty)"
  echo "installed: $(date -Is)"; cat "$GAME/edloader.txt" "$GAME/edworld.ini"; } > "$RUN/RUN.txt"
echo "installed; run dir $RUN; logs: $GAME/edloader.log, $GAME/edworld.log"
