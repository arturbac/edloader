// edloader - reading modules: their names, jumps at a function's start, a function's bytes on disk.
#pragma once

#include "runtime.h"

namespace edloader
  {
  auto narrow(std::wstring const & w) -> std::string;
  ///\brief the module an address lies in, by name; "?" when none
  auto module_name_of(void const * address) -> std::wstring;
  ///\brief where a jump at the start of a function leads (E9 rel32, FF 25 [rip+rel32], 48 B8 imm64 + FF E0, 49 BB imm64
  /// + 41 FF E3, 49 BA imm64 + 41 FF E2, 48 B8 imm64 + 50 C3, 68 imm32 + C3, 68 imm32 + C7 44 24 04 imm32 + C3); null if
  /// none. `p` holds the function's first bytes as read (a copy), `origin` is where the function itself starts: the
  /// relative forms count from the function, not from the copy. The pointer of FF 25 is read only when readable
  auto jump_target(std::uint8_t const * p, void const * origin) -> void const *;
  ///\brief a jump's target for the log: its module's name; in no module (a hook's own stub), its first 16 bytes read
  /// (when readable, under the guard): the module of the jump there, else those bytes in hex
  auto jump_place(void const * to) -> std::string;
  auto bytes_on_disk(std::wstring const & path, char const * function, std::uint8_t (&out)[16], std::wstring & why) -> bool;
  }  // namespace edloader
