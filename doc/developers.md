# edloader for developers

How edloader chains d3d11 proxies, what a plugin can rely on, and how edloader is built and tested. Players:
`doc/users.md`.

## The chain

- edloader is the game's `d3d11.dll`. In `DllMain` it loads only the system's `d3d11.dll`; everything else waits
  for the first export call, so nothing is loaded under the loader lock.
- At the first export call it reads `%USERPROFILE%\edloader\edloader.txt` and loads each listed dll in order. A
  dll that exports `D3D11CreateDevice` becomes a chain element; one that does not, and one listed with `+`, is
  kept loaded and not chained. edloader itself in the list is skipped.
- The game, not in the list, reaches the first element. An element reaches "the original d3d11" by asking for
  `d3d11.dll` by name (`LoadLibrary("d3d11.dll")`, `GetProcAddress`): that is edloader. edloader looks at the module
  the call came from (its return address) and passes the call to the element after it; the last element's call
  goes to the system copy.
- `D3D11CreateDevice` and `D3D11CreateDeviceAndSwapChain` go through the chain; every other d3d11 export goes
  straight to the system copy.
- An element that sends its call to the system copy itself (its own setting) ends the chain there; after the
  outermost call edloader logs `did not call on` with its name.
- A nested call from a module not in the list (for example a dll an element loads by its own path) goes to the
  system copy, never back into the chain.

## Takeovers

A dll in the list runs its own code the moment edloader loads it (C++ static constructors and `DllMain`). It can
take the game's d3d11 calls past the list from there: by rewriting the game exe's import of `D3D11CreateDevice`, or
by an inline hook on the system copy's function. edloader cannot prevent either, so it watches for them:

- At start it logs the modules already in the process, where the game's imports of `D3D11CreateDevice` and
  `D3D11CreateDeviceAndSwapChain` lead, and compares the system copy's first 16 bytes of each with the dll's file
  on disk (export table, file offset; a relocation inside those bytes makes them not comparable, and says so). A
  hook made before edloader started shows there.
- After each dll of the list and at each device the game asks for, it compares the import and the bytes with the
  last check; a change is a `TAKEOVER:` line naming the module the import or the jump now leads to. Where a jump
  leads (`jump_target`: `E9 rel32`, `FF 25 [rip+rel32]`, `48 B8 imm64` + `FF E0`) is counted from the function's own
  address, not from the copy of its bytes; the pointer of `FF 25` is read only from readable memory
  (`test/fake_jumper.cc`: a `FF 25` hook whose pointer lies far from the function, the log naming the dll).
- Reading the file on disk checks every offset against the file (a cut or damaged dll is "not comparable").
- edloader's own steps in the game's process (the start, the list, the checks at each device) each run under a
  handler that catches hardware faults and C++ exceptions alike: the step is logged as `failed (exception ...)`
  and skipped, the game goes on. The checks at a device run one thread at a time. An import put back on a page that
  holds code keeps its execute right while it is written.
- On a takeover edloader stops: every call goes to the system copy, no element of the list is called. The line
  `accept_takeover = true` in `edloader.txt` (settings are read before any dll is loaded) keeps the list passed on,
  with the takeover logged. A plugin that takes over is then best first in the list: its own calls to `d3d11.dll`
  by name come from the first element and go on down the list.
- The stop's advice depends on the taker: a dll edloader loaded from the list (`list_files`, filled before the
  checks after its loading) gets "remove it, or put it first"; anything else (a hook before edloader started, a
  jump into memory of no module, a dll from outside the list) gets `accept_takeover = true` alone. While stopped the
  device line says `through 0 element(s)` and `did not call on` is not reported: no element was called.
- ReShade is a known taker by its export `ReShadeRegisterAddon` (`reshade_loaded` in `src/takeovers.cc`): its hook
  on the system copy leads into memory of no module, so a takeover with no module told (`?`), or one leading into the
  dll with that export, is accepted while that dll is loaded, when ReShade is loaded beside the list (as `dxgi.dll` in
  the game's folder): a dll of edloader's own list that hooks the system copy stops edloader as any other, ReShade
  too. `test/fake_reshade.cc` stands in for it.
- A few dlls are known takers (`known_takers` in `src/takeovers.cc`; Epic's overlay `EOSOVH-Win64-Shipping.dll`):
  their takeover is accepted without the setting, told once, and the loader's imports leading to them are left as
  they are and counted in one line per sweep when the count changes, instead of a line each.

`test/fake_intruder.cc` is such a dll: it rewrites the game's import in its `DllMain`; the test checks both cases,
and copied as `EOSOVH-Win64-Shipping.dll` the known taker.

## What a plugin gets

- Its dll in `%USERPROFILE%\edloader\plugins` (a relative name in the list is taken from there).
- Before any plugin is loaded, edloader sets two environment variables in the game process:
  - `EDLOADER_CONFIG_DIR`: `%USERPROFILE%\edloader\config`, for the plugin's settings;
  - `EDLOADER_LOG_DIR`: `%USERPROFILE%\edloader\logs`, for its logs.

  A plugin that reads them keeps its files out of the game's folder, which a verification of the game's files
  empties of everything not the game's. Without them (no edloader) it falls back to beside its dll; edworld does so.
- To chain on, the plugin asks for `d3d11.dll` by name, as above. A plugin that wants to stay usable without
  edloader names that dll in its settings, as edworld's `next` does.
- edloader makes `edloader`, `config`, `logs` and `plugins` when they are missing, and writes
  `logs\edloader.log` (UTC times, one line per event).

## Build

On Linux with CMake, Ninja, LLVM (clang-cl, lld-link, llvm-lib, llvm-ml64, llvm-rc, llvm-mt) and Python 3; no
MSVC compiler is run. The headers and libraries come from MSVC and the Windows SDK in one directory in clang's
`/winsysroot` layout (`VC/Tools/MSVC/<version>`, `Windows Kits/10`), e.g. the one msvc-wine installs; its path is
`MSVC_ROOT` (a cache variable, or the environment variable of the same name).

```
MSVC_ROOT=<dir> cmake --preset edloader-release
cmake --build build/edloader-release
```

The dll is `<build dir>/d3d11.dll`. The export thunks come from the Windows d3d11 export names in
`tools/d3d11_exports.txt`, turned into thunks by EDVR's generator (`tools/gen_exports.py`, MIT, unchanged).
Presets: `edloader-debug` and `edloader-release` build in `build/<preset>`; `kdev-edloader-debug` and
`kdev-edloader-release` are the same without a build directory, for an IDE that chooses its own. The version in
the log is `git describe` of the tree, taken at each build. `EDLOADER_TESTS` (on) builds the test's dlls.

## Test

`tools/test.sh <build dir> <scratch dir>` runs under wine without the game: a stand-in for the game creates a device through
edloader -> two fake proxies -> edworld -> the system copy, and checks the order of the calls and that every file
lands in its one place. Environment: `EDWORLD_DIR` (an edworld checkout), `EDWORLD_BUILD` (its build directory, with the test's programs),
`EDWORLD_WINEPREFIX` (a wine prefix kept between runs).

## Files

| Path | What |
|---|---|
| `src/runtime.h`, `src/log_paths.cc` | what the parts share (the chain's elements, the paths, the log); the log, the folders, edloader's one place (root_dir) |
| `src/chain.cc`, `src/chain.h` | the system copy in DllMain, the list (edloader.txt) loaded on the first export call, edloader's own steps guarded |
| `src/takeovers.cc`, `src/takeovers.h` | who else takes the game's calls: the game's imports, the loader's functions, edloader's own exports, known takers |
| `src/pe_read.cc`, `src/pe_read.h` | reading modules: names, a jump at a function's start, a function's bytes on disk |
| `src/proxy.cc` | the exported D3D11CreateDevice and ...AndSwapChain (the call passed on to the next element), DllMain |
| `CMakeLists.txt`, `test/CMakeLists.txt`, `CMakePresets.json` | the build |
| `cmake/clang-cl-msvc-sdk.cmake`, `cmake/version.cmake` | the toolchain (clang-cl with MSVC's and the Windows SDK's headers and libraries), the version from `git describe` |
| `tools/d3d11_exports.txt`, `tools/gen_exports_from_list.py` | the Windows d3d11 export names, and the run of EDVR's generator on them |
| `tools/gen_exports.py`, `tools/EDVR-LICENSE.txt` | EDVR's export thunk generator (MIT) |
| `tools/test.sh`, `test/fake_proxy.cc` | the test under wine |
| `NOTICE` | third-party work, file by file, with the licences |
