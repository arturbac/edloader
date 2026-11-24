// edloader — a d3d11.dll that chains several d3d11 proxies (EDVR, EDHM, edworld, ReShade...) in the order
// listed in %USERPROFILE%\edloader\edloader.txt (one place outside the game's folder: see root_dir), the way ASI
// loaders take a list of plugins.
//
// Every proxy in the list was written to BE d3d11.dll and to reach "the original" somehow. Those that ask for
// "d3d11.dll" by name (3Dmigoto/EDHM, edworld with next = d3d11.dll) or by edloader's full path (EDVR's
// advanced.real_dll: EDVR takes a bare name from its own folder) get edloader back; edloader looks at who called
// (the return address's module) and passes the call to the element after it in the list, the last one to the
// system copy. The game itself (not in the list) goes to the first element. A dll at two places (^! in the list,
// first and last) is told apart by the element edloader is calling on that thread.
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
  constexpr std::size_t no_element{~std::size_t{}};
  // the element edloader is calling on this thread: a dll at two places of the chain (^!) calls on from either, and
  // its module alone cannot say which
  thread_local std::size_t calling{no_element};

  struct depth_guard_t
    {
    depth_guard_t() noexcept { ++depth; }

    ~depth_guard_t() { --depth; }
    };

  ///\brief the element being called for a scope, the one before it again after
  struct calling_guard_t
    {
    std::size_t was{calling};

    explicit calling_guard_t(std::size_t now) noexcept { calling = now; }

    ~calling_guard_t() { calling = was; }

    calling_guard_t(calling_guard_t const &) = delete;
    auto operator=(calling_guard_t const &) -> calling_guard_t & = delete;
    };

  auto note_entered(std::size_t i) noexcept -> void
    {
    if(i < 32)
      entered |= 1u << i;
    }

  ///\brief the element the call goes to (chain.size(): the system copy), from the element being called and the module
  /// that made it
  auto route(void * return_address, char const * what) noexcept -> std::size_t
    {
    if(stopped)
      return chain.size();
    HMODULE caller{};
    GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      static_cast<LPCWSTR>(return_address), &caller
    );
    // the element being called calls on: the one after it
    if(calling < chain.size() and chain[calling].module == caller)
      {
      note_entered(calling);
      return calling + 1;
      }
    // a dll of the list while another element is called (or none): its first place after the one being called
    std::size_t const from{calling < chain.size() ? calling + 1 : 0};
    bool listed{};
    for(std::size_t i{}; i != chain.size(); ++i)
      if(chain[i].module == caller)
        {
        listed = true;
        if(i >= from)
          {
          note_entered(i);
          return i + 1;
          }
        }
    if(depth == 1)
      return 0;  // the game: the first element (the system copy when the list is empty)
    // Nested, from a module not in the list, or from a dll of the list at no place after the one being called: never
    // back into the chain, which could only recurse.
    static bool reported{};
    if(not reported)
      {
      reported = true;
      if(listed)
        log_line("{}: nested call from a dll of the list ({}) at no place after chain[{}]; routed to the system copy", what,
                 static_cast<void const *>(caller), calling);
      else
        log_line("{}: nested call from a module not in the list ({}); routed to the system copy", what, static_cast<void const *>(caller));
      }
    return chain.size();
    }

  auto target_name(std::size_t at) -> std::wstring_view
    {
    return at < chain.size() ? std::wstring_view{chain[at].name} : std::wstring_view{system_element.name};
    }

  auto stop_reason() noexcept -> char const *
    {
    return misconfigured ? " (edloader stopped by a MISCONFIGURATION of edloader.txt: the list skipped)"
                         : " (edloader stopped by a TAKEOVER: the list skipped)";
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
          "chain[{}] {}{} did not call on: the elements after it were skipped (point its own 'original d3d11' "
          "setting at d3d11.dll)",
          i, wide(chain[i].name), chain[i].place);
    }
  }  // namespace edloader

using namespace edloader;
using namespace std::string_view_literals;

extern "C" HRESULT WINAPI d3d11_proxy_impl_D3D11CreateDevice(
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
  std::size_t const at{route(_ReturnAddress(), "D3D11CreateDevice")};
  create_device_fn const create{at < chain.size() ? chain[at].create : system_element.create};
  if(not create)
    return E_FAIL;
  HRESULT hr{};
  {
  calling_guard_t const now{at < chain.size() ? at : no_element};
  hr = create(adapter, driver_type, software, flags, levels, level_count, sdk, device, level, context);
  }
  if(depth == 1)
    {
    log_line("D3D11CreateDevice: hr 0x{:08X} through {} element(s){}, device {}; called from {}, passed to {}",
             static_cast<unsigned long>(hr), stopped ? std::size_t{} : chain.size(), stopped ? stop_reason() : "",
             device == nullptr ? "not asked" : SUCCEEDED(hr) and *device ? "made" : "asked, none made",
             wide(module_name_of(_ReturnAddress())), wide(target_name(at)));
    report_shortcuts();
    if(stopped)
      show_stop_window(nullptr);
    }
  return hr;
  }

extern "C" HRESULT WINAPI d3d11_proxy_impl_D3D11CreateDeviceAndSwapChain(
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
  std::size_t at{route(_ReturnAddress(), "D3D11CreateDeviceAndSwapChain")};
  // An element without the swap-chain export cannot take this call; the next one that has it does. The one passed
  // over did not end the chain, so it counts as called on.
  while(at < chain.size() and not chain[at].create_swap)
    note_entered(at++);
  create_device_swap_fn const create_swap{at < chain.size() ? chain[at].create_swap : system_element.create_swap};
  if(not create_swap)
    return E_FAIL;
  HRESULT hr{};
  {
  calling_guard_t const now{at < chain.size() ? at : no_element};
  hr = create_swap(adapter, driver_type, software, flags, levels, level_count, sdk, swap_desc, swap, device, level, context);
  }
  if(depth == 1)
    {
    log_line("D3D11CreateDeviceAndSwapChain: hr 0x{:08X}, first to {}{}", static_cast<unsigned long>(hr),
             wide(at < chain.size() ? target_name(at) : L"the system d3d11.dll"sv), stopped ? stop_reason() : "");
    report_shortcuts();
    if(stopped)
      show_stop_window(swap_desc ? swap_desc->OutputWindow : nullptr);
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
