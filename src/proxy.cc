// edloader — a d3d11.dll that chains several d3d11 proxies (EDVR, EDHM, edworld, ReShade...) in the order
// listed in %USERPROFILE%\edloader\edloader.txt (one place outside the game's folder: see root_dir), the way ASI
// loaders take a list of plugins.
//
// Every proxy in the list was written to BE d3d11.dll and to reach "the original" somehow. Those that ask for
// "d3d11.dll" by name (3Dmigoto/EDHM, edworld with next = d3d11.dll) or by edloader's full path (EDVR's
// advanced.real_dll: EDVR takes a bare name from its own folder) get edloader back; edloader looks at who called
// (the return address's module) and passes the call to the element after it in the list, the last one to the
// system copy. The game itself (not in the list) goes to the first element.
//
// Discipline from EDVR (MIT): only the system copy is loaded in DllMain (already mapped, no foreign DllMain
// under the loader lock); the list is loaded on the first export call.
#include "runtime.h"
#include "chain.h"
#include "pe_read.h"
#include "takeovers.h"

#include <intrin.h>

#include <cstdint>

#pragma intrinsic(_ReturnAddress)

namespace edloader
  {
  thread_local int depth{};
  thread_local std::uint32_t entered{};  // bit i: chain[i] called back into edloader during the outermost call

  struct depth_guard_t
    {
    depth_guard_t() noexcept { ++depth; }

    ~depth_guard_t() { --depth; }
    };

  ///\brief the element the call goes to, from the module that made it
  auto route(void * return_address, char const * what) noexcept -> element_t const &
    {
    if(stopped)
      return system_element;
    HMODULE caller{};
    GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      static_cast<LPCWSTR>(return_address), &caller
    );
    for(std::size_t i{}; i != chain.size(); ++i)
      if(chain[i].module == caller)
        {
        if(i < 32)
          entered |= 1u << i;
        return i + 1 < chain.size() ? chain[i + 1] : system_element;
        }
    if(depth == 1)
      return chain.empty() ? system_element : chain.front();
    // Nested, from a module not in the list: never back into the chain, which could only recurse.
    static bool reported{};
    if(not reported)
      {
      reported = true;
      log_line("%s: nested call from a module not in the list (%p); routed to the system copy", what, caller);
      }
    return system_element;
    }

  ///\brief after the outermost call: an element that did not call on ends the chain early (its own config
  /// points it at the system copy) — said once
  auto report_shortcuts() noexcept -> void
    {
    static bool reported{};
    if(reported or stopped)  // stopped: no element was called, so none ended the chain
      return;
    reported = true;
    for(std::size_t i{}; i + 1 < chain.size() and i < 32; ++i)
      if(not(entered & (1u << i)))
        log_line(
          "chain[%zu] %S did not call on: the elements after it were skipped (point its own 'original d3d11' "
          "setting at d3d11.dll)",
          i, chain[i].name.c_str()
        );
    }
  }  // namespace edloader

using namespace edloader;

extern "C" HRESULT WINAPI edvr_impl_D3D11CreateDevice(
  IDXGIAdapter * adapter,
  D3D_DRIVER_TYPE driver_type,
  HMODULE software,
  UINT flags,
  D3D_FEATURE_LEVEL const * levels,
  UINT level_count,
  UINT sdk,
  ID3D11Device ** device,
  D3D_FEATURE_LEVEL * level,
  ID3D11DeviceContext ** context
)
  {
  ensure_initialised();
  depth_guard_t const guard;
  if(depth == 1)
    {
    entered = 0;
    device_checks("at D3D11CreateDevice");
    }
  element_t const & target{route(_ReturnAddress(), "D3D11CreateDevice")};
  if(not target.create)
    return E_FAIL;
  HRESULT const hr{target.create(adapter, driver_type, software, flags, levels, level_count, sdk, device, level, context)};
  if(depth == 1)
    {
    log_line("D3D11CreateDevice: hr 0x%08lX through %zu element(s)%s, device %s; called from %S, passed to %S",
             static_cast<unsigned long>(hr), stopped ? std::size_t{} : chain.size(),
             stopped ? " (edloader stopped by a TAKEOVER: the list skipped)" : "",
             device == nullptr ? "not asked" : SUCCEEDED(hr) and *device ? "made" : "asked, none made",
             module_of(_ReturnAddress()).c_str(), target.name.c_str());
    report_shortcuts();
    }
  return hr;
  }

extern "C" HRESULT WINAPI edvr_impl_D3D11CreateDeviceAndSwapChain(
  IDXGIAdapter * adapter,
  D3D_DRIVER_TYPE driver_type,
  HMODULE software,
  UINT flags,
  D3D_FEATURE_LEVEL const * levels,
  UINT level_count,
  UINT sdk,
  DXGI_SWAP_CHAIN_DESC const * swap_desc,
  IDXGISwapChain ** swap,
  ID3D11Device ** device,
  D3D_FEATURE_LEVEL * level,
  ID3D11DeviceContext ** context
)
  {
  ensure_initialised();
  depth_guard_t const guard;
  if(depth == 1)
    {
    entered = 0;
    device_checks("at D3D11CreateDeviceAndSwapChain");
    }
  element_t const * target{&route(_ReturnAddress(), "D3D11CreateDeviceAndSwapChain")};
  // An element without the swap-chain export cannot take this call; the next one that has it does.
  while(target != &system_element and not target->create_swap)
    {
    std::size_t const at{static_cast<std::size_t>(target - chain.data())};
    target = at + 1 < chain.size() ? &chain[at + 1] : &system_element;
    }
  if(not target->create_swap)
    return E_FAIL;
  HRESULT const hr{target->create_swap(
    adapter, driver_type, software, flags, levels, level_count, sdk, swap_desc, swap, device, level, context
  )};
  if(depth == 1)
    {
    log_line("D3D11CreateDeviceAndSwapChain: hr 0x%08lX, first to %S", static_cast<unsigned long>(hr),
             target == &system_element ? L"the system d3d11.dll" : target->name.c_str());
    report_shortcuts();
    }
  return hr;
  }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
  {
  if(reason == DLL_PROCESS_ATTACH)
    {
    self_module = instance;
    DisableThreadLibraryCalls(instance);
    loader_phase();
    }
  return TRUE;
  }
