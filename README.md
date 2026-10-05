# edloader

A `d3d11.dll` for Elite Dangerous that lets the game use several d3d11 mods at once (EDHM, EDVR, edworld...),
each still made to be the game's `d3d11.dll`: edloader takes the name and calls them in the order listed in
`edloader.txt`. The ASI-loader idea, a loader and a text list of dlls, applied to d3d11 proxies.

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
