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

`tools/build.sh` builds `build/d3d11.dll` with msvc-wine (cl, ml64, link) on Linux. Environment:

- `MSVC_WINE_ENV`: msvc-wine's `env.sh`;
- `RELEASE_DLL`: a released EDVR `d3d11.dll`, whose export list EDVR's generator (`tools/gen_exports.py`, MIT,
  unchanged) turns into the export thunks; no EDVR binary becomes part of edloader.

The version in the log is `git describe` of the tree it was built from.

## Test

`tools/test.sh <scratch dir>` runs under wine without the game: a stand-in for the game creates a device through
edloader -> two fake proxies -> edworld -> the system copy, and checks the order of the calls and that every file
lands in its one place. Environment: `MSVC_WINE_ENV`, `EDWORLD_DIR` (an edworld checkout with its build),
`EDWORLD_WINEPREFIX` (a wine prefix kept between runs).

## Files

| Path | What |
|---|---|
| `src/edloader.cc` | the loader |
| `tools/build.sh`, `tools/gen_exports_from_release.py` | the build; the export list read from a released EDVR dll |
| `tools/gen_exports.py`, `tools/EDVR-LICENSE.txt` | EDVR's export thunk generator (MIT) |
| `tools/test.sh`, `test/fake_proxy.cc` | the test under wine |
| `NOTICE` | third-party work, file by file, with the licences |
