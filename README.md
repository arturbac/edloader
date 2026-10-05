# edloader

A `d3d11.dll` for Elite Dangerous that chains several d3d11 proxies in the order listed in `edloader.txt` — the
ASI-loader idea (one loader, a text list of DLLs) applied to d3d11 proxies, which each expect to *be*
`d3d11.dll`.

Proof of concept. Tested under wine without the game (`tools/test.sh`); never run in the game yet.

## How the chain is wired

- Each proxy in the list is renamed (`d3d11_edvr.dll`, `d3d11_edhm.dll`, `edworld.dll`), kept in `plugins\`,
  and loaded on the first export call (never under the loader lock; only the system copy is loaded in DllMain).
- A proxy reaches "the original d3d11" by asking for `d3d11.dll` by name (3Dmigoto/EDHM does; EDVR with
  `advanced.real_dll = d3d11.dll`; edworld with `next = d3d11.dll`) — that is edloader. edloader looks at the
  module the call came from (its return address) and passes the call to the element after it; the last one
  goes to the system copy. The game, not in the list, goes to the first element.
- A proxy whose own setting points it at the system copy ends the chain there; edloader says so in its log
  (`did not call on`).
- A nested call from a module not in the list (e.g. EDHM loaded by EDVR's own `real_dll`) goes to the system
  copy, never back into the chain.
- Every other d3d11 export goes straight to the system copy.

## Where its files are

One place, inside the wine prefix: `%USERPROFILE%\edloader` (e.g. `C:\users\steamuser\edloader` under Proton).
A verification of the game's files removes everything in the game's folder that is not the game's and never
touches the prefix, so after one only edloader's `d3d11.dll` has to be put back into the game's folder.

| Path | What |
|---|---|
| `edloader.txt` | the list, below |
| `plugins\` | the plugins' dlls; a relative name in the list is taken from here |
| `config\` | the plugins' settings (edworld: `edworld.ini`) |
| `logs\` | `edloader.log` and the plugins' logs (edworld: `edworld.log`) |

edloader makes the folders when they are missing, and hands `config\` and `logs\` to the plugins in the
environment variables `EDLOADER_CONFIG_DIR` and `EDLOADER_LOG_DIR`. Without a list, `logs\edloader.log` says
where it looked and every call goes to the system d3d11.

## edloader.txt

```
# one dll per line, in call order from the game; '#' or ';' starts a comment
edworld.dll      # first: sees the game's own calls (plugins\edworld.dll)
d3d11_edvr.dll   # EDVR (chains on to EDHM through its own real_dll)
Z:\some\dir\x.dll # an absolute path is taken as it is
+some_plugin.dll # '+' = load only, not chained (not a d3d11 proxy)
```

## Files

| Path | What |
|---|---|
| `src/edloader.cc` | the loader |
| `tools/build.sh` | build through msvc-wine (environment: `MSVC_WINE_ENV`, `RELEASE_DLL`) |
| `tools/test.sh <scratch>` | game stand-in -> edloader -> fake_a -> fake_b -> edworld -> system under wine; checks every file lands in the one place (environment: `MSVC_WINE_ENV`, `EDWORLD_DIR`, `EDWORLD_WINEPREFIX`) |
| `tools/gen_exports.py`, `tools/EDVR-LICENSE.txt` | EDVR's export thunk generator (MIT) |
| `NOTICE` | third-party work, file by file, with the licences |

## Credits

edloader builds on the work of the [EDVR unofficial patch](https://github.com/characterecho-sean/edvr-unofficial-patch)
team (MIT): the proxy loading discipline and the export thunk generator (`tools/gen_exports.py`, unchanged).
What comes from where, file by file, with the licence text: `NOTICE`.
