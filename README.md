# edloader

A `d3d11.dll` for Elite Dangerous that lets the game use several d3d11 mods at once (EDHM, EDVR, edworld...),
each still made to be the game's `d3d11.dll`: edloader takes the name and calls them in the order listed in
`edloader.txt`. The ASI-loader idea, a loader and a text list of dlls, applied to d3d11 proxies.

## Why it makes a broken setup easier to fix

Without a loader, each mod is chained by hand: one is renamed, another one's ini points at it, and a third wrapper
(for example ReShade as `dxgi.dll`) may sit in the game's folder too. Each writes its own log, in its own folder,
with its own clock, and some write none. A real case: inverted colours, black rectangles and EDHM vanishing in a
chain of EDVR, EDHM and ReShade. No log showed the whole chain; one line in EDVR's log named a third wrapper,
`dxgi.dll`, and confirming that it was ReShade took renaming files one at a time.

With edloader the chain is one text file, `edloader.txt`, and `edloader\logs\edloader.log` has one line per event,
each with the time in UTC to the millisecond:

- the modules already in the process when edloader starts (a wrapper loaded before it shows up here);
- each element of the chain in order (`chain[0] = ...`), and a dll that could not be loaded or is not a d3d11 mod;
- a mod that ends the chain early (`did not call on`), and a mod that takes the game's calls past the list
  (`TAKEOVER:`);
- the device creation: through how many elements, called from which module, passed to which.

Changing the order or taking a mod out is an edit of one line, and the next start's log says what came of it.

- Players: [doc/users.md](doc/users.md): install, the list, checking it works, after a verification of the
  game's files, removing it.
- Developers: [doc/developers.md](doc/developers.md): how the chain is wired, what a plugin gets, build and test.

Status: tested under wine without the game (`tools/test.sh`), and in the game under Proton (Steam) with
edworld_eht and EDHM (3Dmigoto) in the list.

MIT licence (LICENSE). edloader builds on the work of the
[EDVR unofficial patch](https://github.com/characterecho-sean/edvr-unofficial-patch) team (MIT): the proxy loading
discipline and the export thunk generator (`tools/gen_exports.py`, unchanged). What comes from where, file by file,
with the licence text: NOTICE. Elite Dangerous is a trademark of Frontier Developments plc; edloader is not
affiliated with or endorsed by Frontier Developments.
