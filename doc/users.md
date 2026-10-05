# edloader for players

edloader lets Elite Dangerous use several d3d11 mods at once, for example EDHM, EDVR and edworld. Each of those
mods wants to be the game's `d3d11.dll`, and only one file can have that name. edloader takes the name and loads
the mods one after another, in the order you list them in a text file.

## Install

1. Find the game's folder: `Products/elite-dangerous-odyssey-64`, the one with `EliteDangerous64.exe`.
2. Find edloader's folder: `edloader` in your user folder, the same one the game runs under.
   - Windows: `C:\Users\<you>\edloader`.
   - Linux, Steam with Proton: `steamapps/compatdata/359320/pfx/drive_c/users/steamuser/edloader`.

   Start the game once with edloader installed and it makes this folder with `plugins`, `config` and `logs` in it.
3. Move each mod's `d3d11.dll` out of the game's folder into `edloader\plugins`, under a name of its own, for
   example `d3d11_edhm.dll`, `d3d11_edvr.dll`, `edworld.dll`.
4. Copy edloader's `d3d11.dll` into the game's folder.
5. Write `edloader\edloader.txt`, one mod per line, in the order the game should reach them:

   ```
   # '#' or ';' starts a comment
   edworld.dll       # first: sees the game's own calls
   d3d11_edvr.dll
   d3d11_edhm.dll
   ```

   A name alone is taken from `edloader\plugins`; a full path (`D:\mods\x.dll`) is taken as it is. A line starting
   with `+` loads a dll that is not a d3d11 mod (it is loaded, nothing is passed to it).
6. Each mod still reads its own settings. edworld reads `edworld.ini` from `edloader\config` and writes its log to
   `edloader\logs`. For other mods, see where they look: most read their files beside their dll, so keep those in
   `edloader\plugins`.

## Each mod's "original d3d11"

Most mods pass the game's calls on to "the original d3d11.dll". For them to pass it on to the next mod in your
list, that setting must say `d3d11.dll` (the name, not a path to the system's copy):

- EDHM (3Dmigoto) does so by itself.
- EDVR: `advanced.real_dll = d3d11.dll` in its settings.
- edworld: `next = d3d11.dll` in `edworld.ini`.

A mod pointed at the system's copy ends the list there: the mods after it are not used, and `edloader.log` says
`did not call on` with the mod's name.

## Check that it works

Open `edloader\logs\edloader.log` after starting the game. It lists the mods it loaded (`chain[0] = ...`) and
`chain: game -> N proxy(ies) -> system d3d11.dll`. A mod it could not load is named with `cannot load`; a dll that
is not a d3d11 mod with `not a d3d11 proxy`. Without `edloader.txt` the log says `no ...edloader.txt` and the game
runs without any mod.

## After a verification of the game's files

Steam's and the launcher's verification removes from the game's folder everything that is not the game's, so
edloader's `d3d11.dll` with it. Your mods, their settings and your list stay in `edloader`, outside the game's
folder: copy edloader's `d3d11.dll` into the game's folder again and everything is back.

## Remove

Delete edloader's `d3d11.dll` from the game's folder. To go back to one mod, copy that mod's dll from
`edloader\plugins` into the game's folder as `d3d11.dll`.
