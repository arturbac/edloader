#!/usr/bin/env bash
# Builds edloader's d3d11.dll on Linux through msvc-wine. Exports as edworld (EDVR's generator, Windows list
# read from a released EDVR d3d11.dll).   Usage: tools/build.sh   (output: build/d3d11.dll)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
RELEASE_DLL=${RELEASE_DLL:-/ext/artur/ed-frontier/edvr-test-20261002/d3d11.dll}
source /ext/artur/msvc-wine/env.sh
cd "$HERE"
VER="$(git describe --tags --always --dirty 2>/dev/null || echo dev)"
mkdir -p build/gen
OBJ=$(mktemp -d build/obj.XXXXXX)
python3 tools/gen_exports.py --self-test >/dev/null
python3 tools/gen_exports_from_release.py --source "$RELEASE_DLL" --tag d3d11 --out build/gen \
    --wrap D3D11CreateDevice --wrap D3D11CreateDeviceAndSwapChain
ml64 /nologo /c /Fo$OBJ/thunks.obj build/gen/edvr_thunks_d3d11.asm
cl /nologo /c /O2 /MT /std:c++latest /EHsc /W4 /Z7 \
    /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE \
    "/DEDLOADER_VERSION=\"$VER\"" /I"build/gen" /Fo$OBJ/ src/edloader.cc
link /nologo /DLL /MACHINE:X64 /INCREMENTAL:NO /DEBUG:FULL /OPT:REF /OPT:ICF \
    /PDB:build/d3d11.pdb /DEF:build/gen/edvr_d3d11.def /OUT:build/d3d11.dll $OBJ/*.obj kernel32.lib
ls -la build/d3d11.dll
