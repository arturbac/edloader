// edloader - the chain: the system copy in DllMain, the list (edloader.txt) loaded on the first export call.
#pragma once

#include "runtime.h"

namespace edloader
  {
  ///\brief the list's own errors stopped edloader (marks, one dll twice in the chain): accept_takeover does not lift it
  extern bool misconfigured;

  auto loader_phase() noexcept -> void;
  auto device_checks(char const * when) noexcept -> void;
  auto ensure_initialised() noexcept -> void;
  }  // namespace edloader
