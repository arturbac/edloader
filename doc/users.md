# edloader for players

edloader lets Elite Dangerous use several d3d11 mods at once, for example EDHM, EDVR and edworld. Each of those
mods wants to be the game's `d3d11.dll`, and only one file can have that name. edloader takes the name and loads
the mods one after another, in the order you list them in a text file. Any one of them alone works too: edworld is
not needed for EDHM or EDVR.

## Install

edloader's `d3d11.dll` is in `edloader-<version>.zip` on the [Releases](https://github.com/arturbac/edloader/releases)
page, with an example list, `edloader.txt.example`.

1. Find the game's folder: `Products/elite-dangerous-odyssey-64`, the one with `EliteDangerous64.exe`.
2. Find edloader's folder: `edloader` in your user folder, the same one the game runs under.
   - Windows: `C:\Users\<you>\edloader`.
   - Linux, Steam with Proton: `steamapps/compatdata/359320/pfx/drive_c/users/steamuser/edloader`.

   Start the game once with edloader installed and it makes this folder with `plugins`, `config` and `logs` in it.
3. Move each mod's `d3d11.dll` out of the game's folder into `edloader\plugins`, under a name of its own, for
   example `d3d11_edvr.dll`, `edworld.dll`, `d3d11_edhm.dll`. Move EDHM's `d3dx.ini`, `d3dcompiler_47.dll`,
   `ShaderFixes` and `EDHM-ini` with it: without them it does not start (`cannot load (error 1114)` in `edloader.log`).
4. Copy edloader's `d3d11.dll` into the game's folder.
5. Write `edloader\edloader.txt`, one mod per line, in the order the game should reach them:

   ```
   # '#' or ';' starts a comment
   edworld.dll           # first: sees the game's own calls
   +d3dcompiler_47.dll   # EDHM's, before it
   d3d11_edhm.dll
   d3d11_edvr.dll        # after EDHM, see "EDHM and EDVR" below
   ```

   A line with `=` is a setting, not a mod; the only one is `accept_takeover` (below).

   A name alone is taken from `edloader\plugins`; a full path (`D:\mods\x.dll`) is taken as it is. A line starting
   with `+` loads a dll that is not a d3d11 mod (it is loaded, nothing is passed to it).
6. Each mod still reads its own settings. edworld reads `edworld.ini` from `edloader\config` and writes its log to
   `edloader\logs`. For other mods, see where they look: most read their files beside their dll, so keep those beside
   it in `edloader\plugins`.

## Each mod's "original d3d11"

Most mods pass the game's calls on to "the original d3d11.dll". For them to pass it on to the next mod in your
list, that setting must say `d3d11.dll` (the name, not a path to the system's copy):

- EDHM (3Dmigoto) does so by itself.
- EDVR: `real_dll` in the `[advanced]` section of its settings (`edvr-flat.ini`, beside its dll in `edloader\plugins`) set to the
  full path of edloader's `d3d11.dll` in the game's folder, for example
  `C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64\d3d11.dll`
  (under Proton the same folder as `Z:\` followed by its Linux path). EDVR takes a name alone from its own folder,
  where there is no `d3d11.dll`, and then goes to the system's copy.
- edworld does so by itself under edloader, from 1.2.0-beta.3; before that it needs `next = d3d11.dll` in `edworld.ini`.

A mod pointed at the system's copy ends the list there: the mods after it are not used, and `edloader.log` says
`did not call on` with the mod's name.

## EDHM and EDVR

Put EDHM before EDVR in the list. With EDVR before EDHM, EDHM (3Dmigoto) hands EDVR its own wrapped device; EDVR's flat
anti-aliasing then never finds the game's frame and stands down for the whole session (its log:
`flat stand-down: entered ... no-known-output-copy`). With EDHM first, EDVR gets the real device, as it does when
EDVR is the game's `d3d11.dll` and EDHM is chained behind it.

With EDVR's anti-aliasing on, set `ui_quality = off` in the `[fix]` section of `edvr-flat.ini` (F8: UI quality). EDVR
otherwise makes the cockpit panels bigger, edworld no longer finds them and the navballs are gone. The clouds are not
affected.

## EDHM-UI

EDHM-UI works in the game's folder. Its Install puts EDHM there as `d3d11.dll`, over edloader's, and its themes go to
`EDHM-ini` and `ShaderFixes` there. After a new theme copy those two folders into `edloader\plugins` again. After an
Install also move EDHM's `d3d11.dll` to `edloader\plugins\d3d11_edhm.dll` and copy edloader's `d3d11.dll` back. Under
Linux the two folders in the game's folder are links into EDHM-UI's own folder: the same links in `edloader\plugins`
make the copying of themes needless.

## A mod that takes the game's calls itself

Some mods do not wait to be called: the moment they are loaded they rewire the game so that its d3d11 calls go
straight to them, past the list. edloader notices this (the game's import of `D3D11CreateDevice` now leading to
that mod, or the system d3d11 changed) and **stops**: its log says `TAKEOVER:` with the mod's name, and the game
runs with no mod of the list called by edloader. Then either remove that mod from the list, or put it **first** in
the list and add the line

```
accept_takeover = true
```

to `edloader.txt`: edloader logs the takeover and still passes the calls on down the list, at your own risk.

## The Epic Games Store: Epic's overlay

The game from the Epic Games Store loads Epic's overlay, `EOSOVH-Win64-Shipping.dll`. A second or two after the
game has made its graphics device, the overlay rewires the game's `D3D11CreateDevice` and the `LoadLibrary` and
`GetProcAddress` imports of most of the dlls in the game. That is Epic's overlay at work, not a broken install.
edloader knows it: it does not stop for it and needs nothing set; the log says so once
(`EOSOVH-Win64-Shipping.dll is Epic's overlay, a known taker: accepted`) and counts the imports in one line
(`... imports of the loader's functions lead to EOSOVH-Win64-Shipping.dll`). The overlay's own device goes down the
list too (its line says `called from EOSOVH-Win64-Shipping.dll`). edworld (from 1.2.0-beta.7) does not follow a
device asked for from outside the game's folder (`not followed` in its log) and stays with the game's.

edloader 1.0.1 and older does not know the overlay: there the log fills with `TAKEOVER:` lines naming it, and
without `accept_takeover = true` in `edloader.txt` edloader stops at the first of them, so a device asked for after
it goes to the system d3d11 and no mod of the list sees it. With such an edloader add that line.

## Check that it works

Open `edloader\logs\edloader.log` after starting the game. It lists the mods it loaded (`chain[0] = ...`) and
`chain: game -> N proxy(ies) -> system d3d11.dll`. A mod it could not load is named with `cannot load`; a dll that
is not a d3d11 mod with `not a d3d11 proxy`. Without `edloader.txt` the log says `no ...edloader.txt` and the game
runs without any mod.

## After a verification of the game's files

Steam's and the launcher's verification removes from the game's folder everything that is not the game's, so
edloader's `d3d11.dll` with it. The mods, their settings and your list stay in `edloader`, outside the game's folder:
copy edloader's `d3d11.dll` into the game's folder again.

## Remove

Delete edloader's `d3d11.dll` from the game's folder. To go back to one mod, copy that mod's dll from
`edloader\plugins` into the game's folder as `d3d11.dll`, and for EDHM its other files too.
