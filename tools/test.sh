#!/usr/bin/env bash
# Chain test without the game: game (edworld's test_app) -> edloader -> fake_a -> fake_b -> edworld -> system.
# Needs tools/build.sh here and in ../edworld (and ../edworld/tools/test.sh once, for test_app.exe).
# Usage: tools/test.sh <scratch dir>
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
EDW=$HERE/../edworld
SCR=${1:?scratch dir}
source /ext/artur/msvc-wine/env.sh
cd "$HERE"
mkdir -p build/test
for n in a b; do
  cl /nologo /O2 /MT /LD /std:c++latest /W4 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS "/DFAKE_NAME=\"fake_$n\"" \
     /Fobuild/test/fake_$n.obj /Febuild/test/fake_$n.dll test/fake_proxy.cc \
     /link /EXPORT:D3D11CreateDevice=fake_D3D11CreateDevice >/dev/null
done
RUN=$(mktemp -d "$SCR/edloader-test.XXXXXX")
cp build/d3d11.dll build/test/fake_a.dll build/test/fake_b.dll "$RUN/"
cp "$EDW/build/d3d11.dll" "$RUN/edworld.dll"
cp "$EDW/build/test/test_app.exe" "$RUN/"
printf '# test chain\nfake_a.dll\nfake_b.dll   ; by name back to edloader\nedworld.dll\n' > "$RUN/edloader.txt"
export WINEPREFIX="$RUN/pfx"
WINEDEBUG=-all wineboot -i >/dev/null 2>&1 || true
cd "$RUN"
set +e
EDWORLD_TEST_NEXT=d3d11.dll WINEDEBUG=-all WINEDLLOVERRIDES="d3d11=n,b" wine test_app.exe | grep -v "^ok"
rc=$?
set -e
echo "--- order.txt"; cat order.txt
echo "--- edloader.log"; cat edloader.log
echo "--- edworld.log (chain lines)"; grep -i "chain\|attach" edworld.log
order=$(tr '\n' ' ' < order.txt)
[[ $order == "fake_a fake_b " ]] || { echo "FAIL: order '$order'"; exit 1; }
grep -q "PASSED" <(EDWORLD_TEST_NEXT=d3d11.dll WINEDEBUG=-all WINEDLLOVERRIDES="d3d11=n,b" wine test_app.exe) \
  && echo "chain test PASSED" || { echo "FAIL: edworld checks through the chain"; exit 1; }
