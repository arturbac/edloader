#!/usr/bin/env bash
# Chain test without the game: game (edworld's test_app) -> edloader -> fake_a -> fake_b -> edworld -> system.
# edworld = its build for anyone (build/edworld/edworld.dll), not the one that works with EHT.
# Needs tools/build.sh here and, in an edworld checkout, tools/build.sh and tools/test.sh once (test_app.exe).
# Environment: MSVC_WINE_ENV (msvc-wine's env.sh), EDWORLD_DIR (that edworld checkout), EDWORLD_WINEPREFIX (a wine
# prefix kept warm between runs).
# Usage: tools/test.sh <scratch dir>
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
SCR=${1:?scratch dir}
need() { [[ -n ${!1:-} ]] || { echo "$0: set $1 ($2)" >&2; exit 1; }; }
need MSVC_WINE_ENV "msvc-wine's env.sh"
need EDWORLD_DIR "an edworld checkout, built and tested once"
need EDWORLD_WINEPREFIX "a wine prefix kept between runs"
EDW=$EDWORLD_DIR
source "$MSVC_WINE_ENV"
cd "$HERE"
mkdir -p build/test
for n in a b; do
  cl /nologo /O2 /MT /LD /std:c++latest /W4 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS "/DFAKE_NAME=\"fake_$n\"" \
     /Fobuild/test/fake_$n.obj /Febuild/test/fake_$n.dll test/fake_proxy.cc \
     /link /EXPORT:D3D11CreateDevice=fake_D3D11CreateDevice >/dev/null
done
RUN=$(mktemp -d "$SCR/edloader-test.XXXXXX")
# the one place, here moved by EDLOADER_DIR (the lab's override): edloader.txt, plugins\, config\, logs\
GAME=$RUN/game ROOT=$RUN/root ELSEWHERE=$RUN/elsewhere
mkdir -p "$GAME" "$ROOT/plugins" "$ELSEWHERE"
cp build/d3d11.dll "$EDW/build/test/edworld/test_app.exe" "$GAME/"
cp build/test/fake_a.dll build/test/fake_b.dll "$ROOT/plugins/"
cp "$EDW/build/edworld/edworld.dll" "$ELSEWHERE/"
win() { echo "Z:${1//\//\\}"; }
printf '# test chain\nfake_a.dll\nfake_b.dll   ; by name back to edloader\n%s   ; an absolute path\n' \
  "$(win "$ELSEWHERE/edworld.dll")" > "$ROOT/edloader.txt"
# warm prefix shared with edworld's test (made once, reused); a fresh one costs minutes in wineboot
export WINEPREFIX=$EDWORLD_WINEPREFIX
if [[ ! -f $WINEPREFIX/system.reg ]]; then
  mkdir -p "$WINEPREFIX"
  WINEDEBUG=-all wineboot -i >/dev/null 2>&1 || true
fi
wineserver -p600 >/dev/null 2>&1 || true
cd "$GAME"
EDLOADER_DIR=$(win "$ROOT")
export EDLOADER_DIR EDWORLD_TEST_NEXT=d3d11.dll WINEDEBUG=-all WINEDLLOVERRIDES="d3d11=n,b"
set +e
wine test_app.exe > test_app.out
set -e
grep -v "^ok" test_app.out
echo "--- order.txt"; cat order.txt
echo "--- edloader.log"; cat "$ROOT/logs/edloader.log"
fail() { echo "FAIL: $*"; exit 1; }
grep -q "PASSED" test_app.out || fail "edworld checks through the chain"
[[ $(tr '\n' ' ' < order.txt) == "fake_a fake_b " ]] || fail "order '$(tr '\n' ' ' < order.txt)'"
grep -q "hr 0x00000000 through 3 element(s)" "$ROOT/logs/edloader.log" || fail "chain of 3"
[[ -s $ROOT/logs/edworld.log ]] || fail "edworld.log not in logs"
for f in edloader.log edworld.log edloader.txt; do [[ ! -e $GAME/$f ]] || fail "$f beside the game"; done
[[ ! -e $ELSEWHERE/edworld.log && ! -e $ELSEWHERE/edworld.ini ]] || fail "edworld's files beside its dll"
PROFILE=$(wine cmd /c echo %USERPROFILE% 2>/dev/null | tr -d '\r')
DEFAULT_LOG=$(winepath -u "$PROFILE\\edloader\\logs\\edloader.log" 2>/dev/null)
grep -qF "EDLOADER_DIR=$EDLOADER_DIR:" "$DEFAULT_LOG" || fail "no EDLOADER_DIR line in $DEFAULT_LOG"
echo "chain test PASSED"
