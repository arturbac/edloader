// edloader - the chain: the system copy in DllMain, the list (edloader.txt) loaded on the first export call.
#pragma once

#include "runtime.h"

namespace edloader
  {
  ///\brief the list's own errors stopped edloader (marks, one dll twice in the chain): accept_takeover does not lift it
  extern bool misconfigured;
  ///\brief the modules passed over when a nested call tells which element calls on (an antivirus's hooks standing in
  /// the chain's own calls): edloader.txt's `pass_modules = a.dll, b.dll`, edplugin_common's list by default
  extern std::wstring pass_modules;

  ///\brief one of edloader's own steps under a handler of hardware faults and C++ exceptions: logged and skipped
  auto guarded(char const * what, void (*step)(char const *), char const * when) noexcept -> bool;
  ///\brief once a process, when edloader is stopped: a window telling the player (owner: the game's window, if known)
  auto show_stop_window(HWND owner) noexcept -> void;

  auto loader_phase() noexcept -> void;
  auto device_checks(char const * when) noexcept -> void;
  auto ensure_initialised() noexcept -> void;
  }  // namespace edloader
