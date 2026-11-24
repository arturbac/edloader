#!/usr/bin/env bash
# Chain test without the game: game (edworld's test_app) -> edloader -> fake_a -> fake_b -> edworld -> system.
# edworld = its build for anyone (edworld/edworld.dll), not the one that works with EHT.
# Needs a CMake build here (EDLOADER_TESTS on) and one of edworld (EDWORLD_TESTS on, its tools/test.sh run once).
# Environment: EDWORLD_DIR (an edworld checkout: its test data), EDWORLD_BUILD (that checkout's build directory),
# EDWORLD_WINEPREFIX (a wine prefix kept warm between runs), WINE_COMMON_TIMEOUT (seconds for one wine run, 600 unless
# set); with no DISPLAY, WINE_USE_EGL=1 unless set. Needs edplugin_common cloned in the checkout (its
# tools/wine-common.sh).
# Usage: tools/test.sh <build dir> <scratch dir>
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
COMMON=$HERE/edplugin_common/tools/wine-common.sh
[[ -f $COMMON ]] || { echo "$0: no $COMMON; in $HERE:" \
  "git clone https://github.com/arturbac/edplugin_common.git edplugin_common" >&2; exit 1; }
. "$COMMON"
BUILD=$(cd "${1:?build dir}" && pwd)
SCR=${2:?scratch dir}
need() { [[ -n ${!1:-} ]] || { echo "$0: set $1 ($2)" >&2; exit 1; }; }
need EDWORLD_DIR "an edworld checkout"
need EDWORLD_BUILD "its build directory, built and tested once"
EDW=$EDWORLD_DIR
EDWB=$EDWORLD_BUILD
for f in "$BUILD/d3d11.dll" "$BUILD/test/fake_a.dll" "$BUILD/test/fake_b.dll" "$BUILD/test/fake_c.dll" "$BUILD/test/fake_intruder.dll" \
         "$BUILD/test/fake_swap.dll" "$BUILD/test/swap_app.exe" \
         "$BUILD/test/fake_hooker.dll" "$BUILD/test/fake_reshade.dll" "$BUILD/test/fake_jumper.dll" "$BUILD/test/fake_malformed.dll" \
         "$EDWB/test/edworld/test_app.exe" "$EDWB/edworld/edworld.dll" \
         "$EDWB/test/test_foreign.dll"; do
  [[ -f $f ]] || { echo "$0: no $f (build first)" >&2; exit 1; }
done
cd "$HERE"
# warm prefix shared with edworld's test (made once, reused); a fresh one costs minutes in wineboot
wine_common_prefix EDWORLD_WINEPREFIX
RUN=$(wine_common_run_dir "$SCR" edloader-test)
# the one place, here moved by EDLOADER_DIR (the lab's override): edloader.txt, plugins\, config\, logs\
GAME=$RUN/game ROOT=$RUN/root ELSEWHERE=$RUN/elsewhere
mkdir -p "$GAME" "$ROOT/plugins" "$ELSEWHERE"
cp "$BUILD/d3d11.dll" "$EDWB/test/edworld/test_app.exe" "$EDW/test/data/edsm_factions_shinrarta.json" "$GAME/"
# edworld's test_app makes an overlay's device from another folder (edworld from 1.2.0-beta.7)
mkdir -p "$GAME/overlay"
cp "$EDWB/test/test_foreign.dll" "$GAME/overlay/"
cp "$BUILD/test/fake_a.dll" "$BUILD/test/fake_b.dll" "$BUILD/test/fake_c.dll" "$ROOT/plugins/"
cp "$EDWB/edworld/edworld.dll" "$ELSEWHERE/"
printf '# test chain\nfake_a.dll\nfake_b.dll   ; by name back to edloader\n%s   ; an absolute path\n' \
  "$(wine_common_win_path "$ELSEWHERE/edworld.dll")" > "$ROOT/edloader.txt"
cd "$GAME"
EDLOADER_DIR=$(wine_common_win_path "$ROOT")
# EDLOADER_WINDOW=test: the window of a stop made, not shown, the clipboard left alone, closed at once (nothing on the desktop)
export EDLOADER_DIR EDWORLD_TEST_NEXT=d3d11.dll WINEDEBUG=-all WINEDLLOVERRIDES="d3d11=n,b" EDLOADER_WINDOW=test
set +e
wine_common_exec -- test_app.exe > test_app.out
set -e
grep -v "^ok" test_app.out
echo "--- order.txt"; cat order.txt
echo "--- edloader.log"; cat "$ROOT/logs/edloader.log"
fail() { echo "FAIL: $*"; exit 1; }
grep -q "PASSED" test_app.out || fail "edworld checks through the chain"
# the game's device and then the overlay's, each down the whole list
[[ $(tr '\n' ' ' < order.txt) == "fake_a fake_b "* ]] || fail "order '$(tr '\n' ' ' < order.txt)'"
grep -q "hr 0x00000000 through 3 element(s)" "$ROOT/logs/edloader.log" || fail "chain of 3"
! grep -qE "window (made|shown|closed|not)" "$ROOT/logs/edloader.log" || fail "a window without a stop"
[[ -s $ROOT/logs/edworld.log ]] || fail "edworld.log not in logs"
for f in edloader.log edworld.log edloader.txt; do [[ ! -e $GAME/$f ]] || fail "$f beside the game"; done
[[ ! -e $ELSEWHERE/edworld.log && ! -e $ELSEWHERE/edworld.ini ]] || fail "edworld's files beside its dll"
PROFILE=$(wine_common_exec -- cmd /c echo %USERPROFILE% 2>/dev/null | tr -d '\r') || fail "the prefix's user profile not read"
DEFAULT_LOG=$(wine_common_exec -- winepath -u "$PROFILE\\edloader\\logs\\edloader.log" 2>/dev/null) \
  || fail "the default log's path not made"
grep -qF "EDLOADER_DIR=$EDLOADER_DIR:" "$DEFAULT_LOG" || fail "no EDLOADER_DIR line in $DEFAULT_LOG"
# a second start: each last session's log set aside in a folder of its name under logs\ (edplugin_common's log)
: > order.txt
set +e
wine_common_exec -- test_app.exe > test_app_again.out
set -e
grep -q "PASSED" test_app_again.out || fail "edworld checks through the chain, the second start"
set_aside() { find "$ROOT/logs/$1" -maxdepth 1 -type f -regex ".*/$1\.[0-9]\{8\}T[0-9]\{6\}Z\.log" 2> /dev/null; }
LAST=$(set_aside edloader)
[[ $(wc -l <<< "$LAST") == 1 && -f $LAST ]] || fail "edloader's last session not set aside once in logs/edloader ($LAST)"
grep -q "hr 0x00000000 through 3 element(s)" "$LAST" || fail "the first session's lines not in $LAST"
[[ $(grep -c " root: " "$ROOT/logs/edloader.log") == 1 ]] || fail "edloader.log not this session's alone"
[[ -n $(set_aside edworld) ]] || fail "edworld's last session not set aside in logs/edworld"
echo "--- set aside: $(basename "$LAST"), $(set_aside edworld | xargs -r -n1 basename | tr '\n' ' ')"
# a dll that takes the game's import in its DllMain: edloader says so and stops, unless the list accepts it
cp "$BUILD/test/fake_intruder.dll" "$ROOT/plugins/"
for accept in no yes; do
  : > order.txt
  : > "$ROOT/logs/edloader.log"
  printf '+fake_intruder.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
  if [[ $accept == yes ]]; then echo "accept_takeover = true" >> "$ROOT/edloader.txt"; fi
  set +e
  wine_common_exec -- test_app.exe > test_app_$accept.out
  set -e
  echo "--- edloader.log, intruder, accept $accept"; cat "$ROOT/logs/edloader.log"
  grep -q "TAKEOVER: after loading fake_intruder.dll: fake_intruder.dll took the game's import of D3D11CreateDevice" \
    "$ROOT/logs/edloader.log" || fail "the intruder named (accept $accept)"
  if [[ $accept == no ]]; then
    grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stops at an intruder"
    [[ ! -s order.txt ]] || fail "the list called after a takeover"
    [[ $(grep -c "window made (EDLOADER_WINDOW=test" "$ROOT/logs/edloader.log") == 1 ]] || fail "the window once at a takeover"
    grep -q "window closed" "$ROOT/logs/edloader.log" || fail "the window closed at a takeover"
  else
    grep -q "accepted (accept_takeover = true)" "$ROOT/logs/edloader.log" || fail "accept_takeover"
    ! grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stopped although the list accepts a takeover"
    ! grep -qE "window (made|shown|closed|not)" "$ROOT/logs/edloader.log" || fail "a window at an accepted takeover"
    [[ $(tr '\n' ' ' < order.txt) == "fake_a "* ]] || fail "the list not called with accept_takeover"
  fi
done
# the same intruder under the name of Epic's overlay, a known taker: accepted without accept_takeover, said once
cp "$BUILD/test/fake_intruder.dll" "$ROOT/plugins/EOSOVH-Win64-Shipping.dll"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf '+EOSOVH-Win64-Shipping.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
set +e
wine_common_exec -- test_app.exe > test_app_known.out
set -e
echo "--- edloader.log, known taker"; cat "$ROOT/logs/edloader.log"
grep -q "EOSOVH-Win64-Shipping.dll is Epic's overlay, a known taker: accepted" "$ROOT/logs/edloader.log" \
  || fail "Epic's overlay accepted as a known taker"
! grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stopped at a known taker"
[[ $(grep -c "a known taker: accepted" "$ROOT/logs/edloader.log") == 1 ]] || fail "a known taker told more than once"
[[ $(tr '\n' ' ' < order.txt) == "fake_a "* ]] || fail "the list not called after a known taker"
! grep -qE "window (made|shown|closed|not)" "$ROOT/logs/edloader.log" || fail "a window with a known taker"
# a dll that hooks edloader's own D3D11CreateDevice in its DllMain, as 3Dmigoto (EDHM) does: put back, the list runs
cp "$BUILD/test/fake_hooker.dll" "$ROOT/plugins/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf '+fake_hooker.dll\nfake_a.dll\nfake_b.dll\n' > "$ROOT/edloader.txt"
set +e
wine_common_exec -- test_app.exe > test_app_hooker.out
set -e
echo "--- edloader.log, hooker"; cat "$ROOT/logs/edloader.log"
grep -q "fake_hooker.dll hooked edloader's own D3D11CreateDevice inline; put back" "$ROOT/logs/edloader.log" \
  || fail "a hook on edloader's own export put back"
[[ $(tr '\n' ' ' < order.txt) == "fake_a fake_b "* ]] || fail "order with a hooker '$(tr '\n' ' ' < order.txt)'"
! grep -qE "window (made|shown|closed|not)" "$ROOT/logs/edloader.log" || fail "a window with a hooker"
# ReShade, known by its export: it hooks the system d3d11 inline through a stub of no module; accepted without
# accept_takeover, said once, and the list still called, the calls reaching the system copy through ReShade's hook
cp "$BUILD/test/fake_reshade.dll" "$ROOT/plugins/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf '+fake_reshade.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
set +e
wine_common_exec -- test_app.exe > test_app_reshade.out
set -e
echo "--- edloader.log, ReShade"; cat "$ROOT/logs/edloader.log"
grep -q "the system d3d11's D3D11CreateDevice was hooked inline (a jump to ?); ReShade is loaded (fake_reshade.dll), a known taker: accepted" \
  "$ROOT/logs/edloader.log" || fail "ReShade accepted as a known taker"
! grep -q "edloader stops" "$ROOT/logs/edloader.log" || fail "edloader stopped at ReShade"
[[ $(grep -c "a known taker: accepted" "$ROOT/logs/edloader.log") == 1 ]] || fail "ReShade told more than once"
[[ $(tr '\n' ' ' < order.txt) == "fake_a reshade "* ]] || fail "order with ReShade '$(tr '\n' ' ' < order.txt)'"
! grep -qE "window (made|shown|closed|not)" "$ROOT/logs/edloader.log" || fail "a window with ReShade"
# a hook with jmp [rip+rel32], rel32 far from 0: where it leads is read from the function's own address, so the log
# names the dll it leads into (counted from a copy of the bytes it would read a pointer from elsewhere); a dll of the
# list, so edloader stops unless accept_takeover, and with it the list is called through the hook
cp "$BUILD/test/fake_jumper.dll" "$ROOT/plugins/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf 'accept_takeover = true\n+fake_jumper.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
set +e
wine_common_exec -- test_app.exe > test_app_jumper.out
set -e
echo "--- edloader.log, jumper"; cat "$ROOT/logs/edloader.log"
grep -q "the system d3d11's D3D11CreateDevice was hooked inline (a jump to fake_jumper.dll); accepted (accept_takeover = true)" \
  "$ROOT/logs/edloader.log" || fail "a relative jump read from the function's address"
! grep -q ": failed (" "$ROOT/logs/edloader.log" || fail "a step of edloader failed"
[[ $(tr '\n' ' ' < order.txt) == "fake_a jumper "* ]] || fail "order with a jumper '$(tr '\n' ' ' < order.txt)'"
# a module whose image is out of shape while loaded (its imports past its end, its headers' page without rights):
# edloader's scans pass it over with a line once, no step fails, the process ends by itself and the chain works
cp "$BUILD/test/fake_malformed.dll" "$ROOT/plugins/"
for mode in imports-out header-noaccess; do
  : > order.txt
  : > malformed.txt
  : > "$ROOT/logs/edloader.log"
  printf '+fake_malformed.dll\nfake_a.dll\n' > "$ROOT/edloader.txt"
  set +e
  wine_common_exec FAKE_MALFORMED=$mode -- test_app.exe > "test_app_malformed_$mode.out"
  r=$?
  set -e
  echo "--- edloader.log, malformed $mode (test_app exit $r)"; cat "$ROOT/logs/edloader.log"
  [[ $(cat malformed.txt) == "$mode" ]] || fail "malformed $mode: the fake did not change its image"
  case $mode in
    imports-out) want="after the list: fake_malformed.dll has its imports out of shape (read up to there); passed over" ;;
    header-noaccess) want="after the list: fake_malformed.dll has headers out of shape; passed over" ;;
  esac
  grep -qF -- "$want" "$ROOT/logs/edloader.log" || fail "malformed $mode: no '$want'"
  # said once a session, whatever the number of sweeps
  n=$(grep -c -- "fake_malformed.dll has " "$ROOT/logs/edloader.log")
  (( n == 1 )) || fail "malformed $mode: the line said $n times"
  ! grep -q ": failed (" "$ROOT/logs/edloader.log" || fail "malformed $mode: a step of edloader failed"
  (( r == 0 || r == 1 )) || fail "malformed $mode: test_app ended with $r"
  [[ $(tr '\n' ' ' < order.txt) == "fake_a "* ]] || fail "malformed $mode: order '$(tr '\n' ' ' < order.txt)'"
done
# the marks: ^ first, ! last, ^! one dll at both ends; a mark wherever its line stands
marked() {  # <label> <list text>: one run of test_app with that list
  : > order.txt
  : > loaded.txt
  : > "$ROOT/logs/edloader.log"
  printf '%b' "$2" > "$ROOT/edloader.txt"
  set +e
  wine_common_exec -- test_app.exe > "test_app_$1.out"
  set -e
  echo "--- edloader.log, $1"; cat "$ROOT/logs/edloader.log"
}
order_is() { [[ $(tr '\n' ' ' < order.txt) == "$1"* ]] || fail "$2: order '$(tr '\n' ' ' < order.txt)'"; }
log_has() { grep -qF -- "$1" "$ROOT/logs/edloader.log" || fail "$2: no '$1' in edloader.log"; }
log_lacks() { ! grep -qF -- "$1" "$ROOT/logs/edloader.log" || fail "$2: '$1' in edloader.log"; }
marked first 'fake_a.dll\n^fake_b.dll\n'
order_is "fake_b fake_a " "^ first"
log_has "device made" "^ first"
log_has "chain[0] = fake_b.dll (first, ^)" "^ first"
marked last '!fake_a.dll\nfake_b.dll\n'
order_is "fake_b fake_a " "! last"
log_has "device made" "! last"
log_has "chain[1] = fake_a.dll (last, !)" "! last"
marked both 'fake_b.dll\n^! fake_a.dll   # spaces after the marks\n'
order_is "fake_a fake_b fake_a fake_a fake_b fake_a " "^! both ends"
log_has "through 3 element(s), device made" "^! both ends"
log_has "chain[0] = fake_a.dll (first, ^)" "^! both ends"
log_has "chain[2] = fake_a.dll (last, !)" "^! both ends"
log_lacks "did not call on" "^! both ends"
log_lacks "nested call" "^! both ends"
# ^ and ! on two dlls, one between: the marks, not the lines, give the order
marked ends 'fake_c.dll\n!fake_a.dll\n^fake_b.dll\n'
order_is "fake_b fake_c fake_a fake_b fake_c fake_a " "^ and ! on two dlls"
log_has "chain[0] = fake_b.dll (first, ^)" "^ and ! on two dlls"
log_has "chain[1] = fake_c.dll" "^ and ! on two dlls"
log_has "chain[2] = fake_a.dll (last, !)" "^ and ! on two dlls"
log_has "through 3 element(s), device made" "^ and ! on two dlls"
log_lacks "did not call on" "^ and ! on two dlls"
log_lacks "nested call" "^ and ! on two dlls"
[[ $(tr '\n' ' ' < loaded.txt) == "fake_c fake_a fake_b " ]] || fail "^ and ! on two dlls: loaded '$(tr '\n' ' ' < loaded.txt)', not by the lines"
marked alone '^!fake_a.dll\n'
order_is "fake_a fake_a fake_a fake_a " "^! alone"
log_has "device made" "^! alone"
# a mark on a dll that gives no element: logged, the rest of the list as it is
marked no_dll '^no_such.dll\nfake_a.dll\n'
order_is "fake_a " "a mark on a missing dll"
log_has "the place it asks for (^) is left to the rest of the list" "a mark on a missing dll"
log_lacks "MISCONFIGURATION" "a mark on a missing dll"
# one dll loaded twice by + lines: as before, a line about it
marked plain_twice '+fake_a.dll\n+fake_a.dll\nfake_b.dll\n'
order_is "fake_b " "+ twice"
log_has "lines 1 and 2 load the same dll (fake_a.dll): loaded once, as before" "+ twice"
log_lacks "MISCONFIGURATION" "+ twice"
# the list's own errors: edloader stops before loading any dll, every call to the system copy, accept_takeover or not
misconfigured() {  # <label> <list text> <the error's text>
  marked "$1" "$2"
  log_has "$3" "$1"
  log_has "MISCONFIGURATION: edloader stops" "$1"
  log_has "chain: game -> 0 proxy(ies)" "$1"
  log_has "MISCONFIGURATION of edloader.txt: the list skipped" "$1"
    log_lacks "chain[" "$1"
  [[ ! -s order.txt ]] || fail "$1: the list called"
  [[ ! -s loaded.txt ]] || fail "$1: the list loaded ($(tr '\n' ' ' < loaded.txt))"
  log_has "device made" "$1"
  [[ $(grep -c "window made (EDLOADER_WINDOW=test" "$ROOT/logs/edloader.log") == 1 ]] || fail "$1: the window not once"
  log_has "window closed" "$1"
  log_has ", its OK there)" "$1"
}
misconfigured two_first '^fake_a.dll\nfake_b.dll\n^!fake_b.dll\n' \
  "2 dlls ask for the first place (^), which one dll can have: line 1 fake_a.dll, line 3 fake_b.dll"
log_has "lines 2 and 3 name the same dll (fake_b.dll)" "two_first: each mistake its line"
log_has "not shown, the clipboard left alone, closed at once; 3 line(s) of what stopped edloader, the log's Unix path" "two_first: the window's text"
misconfigured two_last '!fake_a.dll\n!fake_b.dll\n' "2 dlls ask for the last place (!)"
misconfigured plain_marked '+^fake_a.dll\nfake_b.dll\n' "line 1 '+^fake_a.dll': '+' (loaded, not chained) together with"
misconfigured marked_plain '^+fake_a.dll\n' "'+' (loaded, not chained) together with"
misconfigured twice 'fake_a.dll\nfake_b.dll\nfake_a.dll\n' "lines 1 and 3 name the same dll (fake_a.dll)"
misconfigured bad_marks '!^fake_a.dll\n' "line 1 '!^fake_a.dll': marks other than"
misconfigured apart '^ !fake_a.dll\n' "line 1 '^ !fake_a.dll': marks other than"
misconfigured plus_plus '++fake_a.dll\n' "line 1 '++fake_a.dll': marks other than"
misconfigured no_name 'fake_a.dll\n^! # nothing after it\n' "line 2 '^!': a mark with no dll after it"
misconfigured plain_no_name 'fake_a.dll\n+   ; nothing after it\n' "line 2 '+': a mark with no dll after it"
misconfigured accepted 'accept_takeover = true\n^fake_a.dll\n^fake_b.dll\n' "ask for the first place"
log_lacks "TAKEOVER accepted" "accept_takeover with the list's errors"
# one dll by two paths (here a link to it): found when the second is loaded, the dlls before it loaded, none called
ln -s "$ROOT/plugins/fake_a.dll" "$ELSEWHERE/link_a.dll"
marked two_paths "fake_a.dll\n$(wine_common_win_path "$ELSEWHERE/link_a.dll" | sed 's/\\/\\\\/g')\n"
log_has "MISCONFIGURATION: line 2" "one dll by two paths"
log_has "is a dll already in the chain, by another path" "one dll by two paths"
log_has "the dlls before line 2 are loaded" "one dll by two paths"
log_has "chain: game -> 0 proxy(ies)" "one dll by two paths"
log_has "MISCONFIGURATION of edloader.txt: the list skipped), device made" "one dll by two paths"
[[ ! -s order.txt ]] || fail "one dll by two paths: the list called"
[[ $(tr '\n' ' ' < loaded.txt) == "fake_a " ]] || fail "one dll by two paths: loaded '$(tr '\n' ' ' < loaded.txt)'"
# the same with a takeover accepted before it: accept_takeover does not lift the list's error
marked two_paths_accepted "accept_takeover = true\n+fake_intruder.dll\nfake_a.dll\n$(wine_common_win_path "$ELSEWHERE/link_a.dll" | sed 's/\\/\\\\/g')\n"
log_has "fake_intruder.dll took the game's import of D3D11CreateDevice" "takeover and the list's error"
log_has "MISCONFIGURATION: edloader stops" "takeover and the list's error"
log_lacks "TAKEOVER accepted (accept_takeover" "takeover and the list's error"
! grep -q fake_a order.txt || fail "takeover and the list's error: the list called"
# a byte order mark (Notepad) before the first line: the first line still read
marked bom '\xEF\xBB\xBF^fake_b.dll\nfake_a.dll\n'
order_is "fake_b fake_a " "a byte order mark"
log_lacks "cannot load" "a byte order mark"
# D3D11CreateDeviceAndSwapChain: ^! on a dll with it, one without it passed over and not told as ending the chain
cp "$BUILD/test/fake_swap.dll" "$ROOT/plugins/"
cp "$BUILD/test/swap_app.exe" "$GAME/"
: > order.txt
: > "$ROOT/logs/edloader.log"
printf 'fake_a.dll\n^!fake_swap.dll\n' > "$ROOT/edloader.txt"
set +e
wine_common_exec -- swap_app.exe > swap_app.out
set -e
echo "--- edloader.log, swap"; cat swap_app.out "$ROOT/logs/edloader.log"
[[ $(tr '\n' ' ' < order.txt) == "fake_swap fake_swap " ]] || fail "swap: order '$(tr '\n' ' ' < order.txt)'"
log_has "D3D11CreateDeviceAndSwapChain: hr 0x00000000" "swap"
log_has "first to fake_swap.dll" "swap"
log_lacks "did not call on" "swap"
log_lacks "nested call" "swap"
# no NUL in the log (a line cut at its buffer's end)
! grep -qaP '\x00' "$ROOT/logs/edloader.log" || fail "a NUL in edloader.log"
echo "chain test PASSED"
