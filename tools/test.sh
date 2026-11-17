#!/usr/bin/env bash
# Chain test without the game: game (edworld's test_app) -> edloader -> fake_a -> fake_b -> edworld -> system.
# edworld = its build for anyone (edworld/edworld.dll), not the one that works with EHT.
# Needs a CMake build here (EDLOADER_TESTS on) and one of edworld (EDWORLD_TESTS on, its tools/test.sh run once).
# Environment: EDWORLD_DIR (an edworld checkout: its test data), EDWORLD_BUILD (that checkout's build directory),
# EDWORLD_WINEPREFIX (a wine prefix kept warm between runs).
# Usage: tools/test.sh <build dir> <scratch dir>
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(cd "${1:?build dir}" && pwd)
SCR=${2:?scratch dir}
need() { [[ -n ${!1:-} ]] || { echo "$0: set $1 ($2)" >&2; exit 1; }; }
need EDWORLD_DIR "an edworld checkout"
need EDWORLD_BUILD "its build directory, built and tested once"
need EDWORLD_WINEPREFIX "a wine prefix kept between runs"
EDW=$EDWORLD_DIR
EDWB=$EDWORLD_BUILD
for f in "$BUILD/d3d11.dll" "$BUILD/test/fake_a.dll" "$BUILD/test/fake_b.dll" "$BUILD/test/fake_intruder.dll" \
         "$BUILD/test/fake_hooker.dll" "$BUILD/test/fake_reshade.dll" "$BUILD/test/fake_jumper.dll" "$EDWB/test/edworld/test_app.exe" "$EDWB/edworld/edworld.dll" \
         "$EDWB/test/test_foreign.dll"; do
  [[ -f $f ]] || { echo "$0: no $f (build first)" >&2; exit 1; }
done
cd "$HERE"
RUN=$(mktemp -d "$SCR/edloader-test.XXXXXX")
# the one place, here moved by EDLOADER_DIR (the lab's override): edloader.txt, plugins\, config\, logs\
GAME=$RUN/game ROOT=$RUN/root ELSEWHERE=$RUN/elsewhere
mkdir -p "$GAME" "$ROOT/plugins" "$ELSEWHERE"
cp "$BUILD/d3d11.dll" "$EDWB/test/edworld/test_app.exe" "$EDW/test/data/edsm_factions_shinrarta.json" "$GAME/"
# edworld's test_app makes an overlay's device from another folder (edworld from 1.2.0-beta.7)
mkdir -p "$GAME/overlay"
cp "$EDWB/test/test_foreign.dll" "$GAME/overlay/"
cp "$BUILD/test/fake_a.dll" "$BUILD/test/fake_b.dll" "$ROOT/plugins/"
cp "$EDWB/edworld/edworld.dll" "$ELSEWHERE/"
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
# the game's device and then the overlay's, each down the whole list
[[ $(tr '\n' ' ' < order.txt) == "fake_a fake_b "* ]] || fail "order '$(tr '\n' ' ' < order.txt)'"
grep -q "hr 0x00000000 through 3 element(s)" "$ROOT/logs/edloader.log" || fail "chain of 3"
[[ -s $ROOT/logs/edworld.log ]] || fail "edworld.log not in logs"
for f in edloader.log edworld.log edloader.txt; do [[ ! -e $GAME/$f ]] || fail "$f beside the game"; done
[[ ! -e $ELSEWHERE/edworld.log && ! -e $ELSEWHERE/edworld.ini ]] || fail "edworld's files beside its dll"
PROFILE=$(wine cmd /c echo %USERPROFILE% 2>/dev/null | tr -d '\r')
DEFAULT_LOG=$(winepath -u "$PROFILE\\edloader\\logs\\edloader.log" 2>/dev/null)
grep -qF "EDLOADER_DIR=$EDLOADER_DIR:" "$DEFAULT_LOG" || fail "no EDLOADER_DIR line in $DEFAULT_LOG"
# a dll that takes the game's import in its DllMain: edloader says so and stops, unless the list accepts it
cp "$BUILD/test/fake_intruder.dll" "$ROOT/plugins/"
for accept in no yes; do
  : > order.txt
  : > "$ROOT/logs/edloader.log"
  printf '+fake_intruder.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
  if [[ $accept == yes ]]; then echo "accept_takeover = true" >> "$ROOT/edloader.txt"; fi
  set +e
  wine test_app.exe > test_app_$accept.out
  set -e
  echo "--- edloader.log, intruder, accept $accept"; cat "$ROOT/logs/edloader.log"
  grep -q "TAKEOVER: after loading fake_intruder.dll: fake_intruder.dll took the game's import of D3D11CreateDevice" \
    "$ROOT/logs/edloader.log" || fail "the intruder named (accept $accept)"
  if [[ $accept == no ]]; then
    grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stops at an intruder"
    [[ ! -s order.txt ]] || fail "the list called after a takeover"
  else
    grep -q "accepted (accept_takeover = true)" "$ROOT/logs/edloader.log" || fail "accept_takeover"
    ! grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stopped although the list accepts a takeover"
    [[ $(tr '\n' ' ' < order.txt) == "fake_a "* ]] || fail "the list not called with accept_takeover"
  fi
done
# the same intruder under the name of Epic's overlay, a known taker: accepted without accept_takeover, said once
cp "$BUILD/test/fake_intruder.dll" "$ROOT/plugins/EOSOVH-Win64-Shipping.dll"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf '+EOSOVH-Win64-Shipping.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
set +e
wine test_app.exe > test_app_known.out
set -e
echo "--- edloader.log, known taker"; cat "$ROOT/logs/edloader.log"
grep -q "EOSOVH-Win64-Shipping.dll is Epic's overlay, a known taker: accepted" "$ROOT/logs/edloader.log" \
  || fail "Epic's overlay accepted as a known taker"
! grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stopped at a known taker"
[[ $(grep -c "a known taker: accepted" "$ROOT/logs/edloader.log") == 1 ]] || fail "a known taker told more than once"
[[ $(tr '\n' ' ' < order.txt) == "fake_a "* ]] || fail "the list not called after a known taker"
# a dll that hooks edloader's own D3D11CreateDevice in its DllMain, as 3Dmigoto (EDHM) does: put back, the list runs
cp "$BUILD/test/fake_hooker.dll" "$ROOT/plugins/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf '+fake_hooker.dll\nfake_a.dll\nfake_b.dll\n' > "$ROOT/edloader.txt"
set +e
wine test_app.exe > test_app_hooker.out
set -e
echo "--- edloader.log, hooker"; cat "$ROOT/logs/edloader.log"
grep -q "fake_hooker.dll hooked edloader's own D3D11CreateDevice inline; put back" "$ROOT/logs/edloader.log" \
  || fail "a hook on edloader's own export put back"
[[ $(tr '\n' ' ' < order.txt) == "fake_a fake_b "* ]] || fail "order with a hooker '$(tr '\n' ' ' < order.txt)'"
# ReShade, known by its export: it hooks the system d3d11 inline through a stub of no module; accepted without
# accept_takeover, said once, and the list still called, the calls reaching the system copy through ReShade's hook
cp "$BUILD/test/fake_reshade.dll" "$ROOT/plugins/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf '+fake_reshade.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
set +e
wine test_app.exe > test_app_reshade.out
set -e
echo "--- edloader.log, ReShade"; cat "$ROOT/logs/edloader.log"
grep -q "the system d3d11's D3D11CreateDevice was hooked inline (a jump to ?); ReShade is loaded (fake_reshade.dll), a known taker: accepted" \
  "$ROOT/logs/edloader.log" || fail "ReShade accepted as a known taker"
! grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stopped at ReShade"
[[ $(grep -c "a known taker: accepted" "$ROOT/logs/edloader.log") == 1 ]] || fail "ReShade told more than once"
[[ $(tr '\n' ' ' < order.txt) == "fake_a reshade "* ]] || fail "order with ReShade '$(tr '\n' ' ' < order.txt)'"
# a hook with jmp [rip+rel32], rel32 far from 0: where it leads is read from the function's own address, so the log
# names the dll it leads into (counted from a copy of the bytes it would read a pointer from elsewhere); a dll of the
# list, so edloader stops unless accept_takeover, and with it the list is called through the hook
cp "$BUILD/test/fake_jumper.dll" "$ROOT/plugins/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf 'accept_takeover = true\n+fake_jumper.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
set +e
wine test_app.exe > test_app_jumper.out
set -e
echo "--- edloader.log, jumper"; cat "$ROOT/logs/edloader.log"
grep -q "the system d3d11's D3D11CreateDevice was hooked inline (a jump to fake_jumper.dll); accepted (accept_takeover = true)" \
  "$ROOT/logs/edloader.log" || fail "a relative jump read from the function's address"
! grep -q "failed (exception" "$ROOT/logs/edloader.log" || fail "a step of edloader failed"
[[ $(tr '\n' ' ' < order.txt) == "fake_a jumper "* ]] || fail "order with a jumper '$(tr '\n' ' ' < order.txt)'"
# no NUL in the log (a line cut at its buffer's end)
! grep -qaP '\x00' "$ROOT/logs/edloader.log" || fail "a NUL in edloader.log"
echo "chain test PASSED"
