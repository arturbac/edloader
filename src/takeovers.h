// edloader - who else takes the game's calls: the game's imports, the loader's functions, edloader's own exports.
#pragma once

#include "runtime.h"

namespace edloader
  {
  extern bool takeover;
  extern bool accept_takeover;
  extern bool stopped;
  extern std::vector<std::wstring> list_files;
  extern void * own_create;

  ///\brief what LoadLibrary("d3d11.dll") and GetProcAddress hand out now: edloader itself, unless someone redirects them
  auto check_loader(char const * when) -> void;
  ///\brief compares the game's import slots and the system copy's first bytes with the last check; logs changes
  auto check_takers(char const * when) -> void;
  ///\brief every export of edloader's own table (the wrapped D3D11CreateDevice(AndSwapChain) among them, which the
  /// generated thunk list lacks), its first 32 bytes, before any dll of the list is loaded
  auto keep_own_exports() -> void;
  ///\brief puts back the first bytes of edloader's exports a dll overwrote (3Dmigoto's HookD3D11 from its DllMain), naming
  /// that dll: the one just loaded (a hook library jumps through its own stub, so the jump's module would say nothing)
  auto guard_own_exports(char const * when, char const * who) -> void;
  ///\brief every module's imports of the loader's functions: logged when they lead past kernel32/kernelbase, put back
  /// unless accept_takeover; once a frame of device calls at most
  auto sweep_loader_imports(char const * when) -> void;
  ///\brief the starting point of the checks: where the game's imports lead and the system copy's first bytes,
  /// against the system copy's file on disk (a hook made before edloader started shows there)
  auto start_takers() -> void;
  }  // namespace edloader
