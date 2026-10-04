# edloader

A `d3d11.dll` for Elite Dangerous that chains several d3d11 proxies in the order listed in `edloader.txt`
beside it — the ASI-loader idea (one loader, a text list of DLLs) applied to d3d11 proxies, which each
expect to *be* `d3d11.dll`.

Proof of concept. Tested under wine without the game (`tools/test.sh`); never run in the game yet.

## How the chain is wired

- Each proxy in the list is renamed (`d3d11_edvr.dll`, `d3d11_edhm.dll`, `edworld.dll`) and loaded on the
  first export call (never under the loader lock; only the system copy is loaded in DllMain).
- A proxy reaches "the original d3d11" by asking for `d3d11.dll` by name (3Dmigoto/EDHM does; EDVR with
  `advanced.real_dll = d3d11.dll`; edworld with `next = d3d11.dll`) — that is edloader. edloader looks at the
  module the call came from (its return address) and passes the call to the element after it; the last one
  goes to the system copy. The game, not in the list, goes to the first element.
- A proxy whose own setting points it at the system copy ends the chain there; edloader says so in its log
  (`did not call on`).
- A nested call from a module not in the list (e.g. EDHM loaded by EDVR's own `real_dll`) goes to the system
  copy, never back into the chain.
- Every other d3d11 export goes straight to the system copy.

## edloader.txt

```
# one dll per line, in call order from the game; '#' or ';' starts a comment
edworld.dll      # first: sees the game's own calls
d3d11_edvr.dll   # EDVR (chains on to EDHM through its own real_dll)
+some_plugin.dll # '+' = load only, not chained (not a d3d11 proxy)
```

## Files

| Path | What |
|---|---|
| `src/edloader.cc` | the loader |
| `tools/build.sh` | build through msvc-wine |
| `tools/test.sh <scratch>` | game stand-in -> edloader -> fake_a -> fake_b -> edworld -> system, under wine |
| `tools/lab-install.sh`, `tools/lab-revert.sh <run dir>` | put into / take out of the ed-lab clone (edworld + EDVR + EDHM) |
| `tools/gen_exports.py`, `tools/EDVR-LICENSE.txt` | EDVR's export thunk generator (MIT) |
