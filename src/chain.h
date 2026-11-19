// edloader - the chain: the system copy in DllMain, the list (edloader.txt) loaded on the first export call.
#pragma once

#include "runtime.h"

namespace edloader
  {
  auto loader_phase() noexcept -> void;
  auto device_checks(char const * when) noexcept -> void;
  auto ensure_initialised() noexcept -> void;
  }  // namespace edloader
