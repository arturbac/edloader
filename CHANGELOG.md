# Changelog

## Unreleased

- A `+` with no dll after it is a mistake in the list (`MISCONFIGURATION: line N '+': a mark with no dll after it`),
  as `^` or `!` alone are; before, the line was taken for a dll named by the empty name, could not be loaded and was
  left out.
- edloader reads other modules' images through edplugin_common's `pe_image`: a module with headers or imports out of
  shape (or unloaded while it is read) is passed over with a line, never read past its end; an import slot is written
  only within the module's import table and edloader's own exports' bytes only within its code, each write under a
  fault handler with the page's rights put back. A step that fails logs the fault's kind and, for a hardware fault, its place
  (`failed (<what> at <module>+0x...)`, before `failed (exception 0x...)`).
- `USERPROFILE`, `EDLOADER_DIR` and the path of edloader's dll are taken whole over 259 characters (edplugin_common's
  environment and paths; before, a longer value was taken for none and the root fell back). A module whose name
  cannot be told is `?` in the log also when its path cannot be read (before, an empty name). The variables
  `EDLOADER_CONFIG_DIR` and `EDLOADER_LOG_DIR` are named by edplugin_common, which the plugins read them by.
- The log is edplugin_common's and holds one game session: at the start the last session's `edloader.log` is moved to
  `logs\edloader\edloader.<UTC>.log` (before, every session was appended to one file). It is written with Windows'
  own calls instead of a CRT stream: a thread killed at the game's exit in the middle of a write leaves no stream half
  done for the CRT to close (what ended edworld with 0xC0000409 before 2026-11-17).
- Built with edplugin_common 1.0.0, the library the plugins share: a clone in the plugin's directory on the commit
  `edplugin_common.commit` pins. Without the clone the build stops with the commands that make it; another commit or
  changes in it are a warning, and an error in a release build. The log says the library's commit in a line of its
  own after the version (`edplugin_common <hash>`).
- The d3d11 exports are made at configure time by edplugin_common (`edplugin_common_proxy_exports`) instead of the
  copy of EDVR's generator: the build no longer needs Python 3. The dll's exports are unchanged.
- The `kdev-*` presets build with clang's GNU-style driver (edplugin_common's `clang-msvc-sdk.cmake`) instead of
  clang-cl, so an IDE whose code model is libclang (KDevelop) reads the compile commands; releases stay with
  clang-cl. Warnings through `edplugin_common_warnings`, linker options with CMake's `LINKER:` prefix.

## 1.3.1 and earlier

The notes of each release are with its tag on GitHub.
