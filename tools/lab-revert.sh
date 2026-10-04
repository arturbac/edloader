#!/usr/bin/env bash
# Puts back what tools/lab-install.sh saved in <run dir>/orig; logs and the files it added go to the run dir.
set -euo pipefail
RUN=${1:?run dir}
GAME=/ext/artur/ed-lab/game/Products/elite-dangerous-odyssey-64
if pgrep -f '[E]liteDangerous64|[E]DLaunch' >/dev/null; then echo "a game is running; not touching files"; exit 1; fi
[[ -d $RUN/orig ]] || { echo "no $RUN/orig"; exit 1; }
mkdir -p "$RUN/added"
for f in edloader.log edworld.log; do [[ -f $GAME/$f ]] && mv "$GAME/$f" "$RUN/"; done
for f in d3d11_edvr.dll edworld.dll edworld.ini edloader.txt; do
  [[ -e $GAME/$f && ! -e $RUN/orig/$f ]] && mv "$GAME/$f" "$RUN/added/"
done
cp -a "$RUN/orig/." "$GAME/"
echo "reverted from $RUN"
