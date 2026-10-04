// edloader — a d3d11.dll that chains several d3d11 proxies (EDVR, EDHM, edworld, ReShade...) in the order
// listed in edloader.txt beside it, the way ASI loaders take a list of plugins.
//
// Every proxy in the list was written to BE d3d11.dll and to reach "the original" somehow. Those that ask for
// "d3d11.dll" by name (3Dmigoto/EDHM, EDVR with advanced.real_dll = d3d11.dll, edworld with next = d3d11.dll)
// get edloader back; edloader looks at who called (the return address's module) and passes the call to the
// element after it in the list, the last one to the system copy. The game itself (not in the list) goes to the
// first element.
//
// Discipline from EDVR (MIT): only the system copy is loaded in DllMain (already mapped, no foreign DllMain
// under the loader lock); the list is loaded on the first export call.
#include <windows.h>

#include <d3d11.h>
#include <intrin.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#pragma intrinsic(_ReturnAddress)

extern "C"
  {
  extern void * edvr_realProcs_d3d11[];
  void edvr_unresolved_d3d11();
  }

namespace
  {
  char const * const export_names[]{
#include "edvr_exports_d3d11.inc"
  };
  constexpr std::size_t export_count{sizeof(export_names) / sizeof(export_names[0])};

  using create_device_fn = HRESULT(WINAPI *)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, ID3D11Device **,
    D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
  );
  using create_device_swap_fn = HRESULT(WINAPI *)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT,
    DXGI_SWAP_CHAIN_DESC const *, IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
  );

  struct element_t
    {
    std::wstring name;
    HMODULE module{};
    create_device_fn create{};
    create_device_swap_fn create_swap{};
    };

  HMODULE self_module{};
  HMODULE system_module{};
  std::wstring module_dir;
  element_t system_element;
  std::vector<element_t> chain;
  std::FILE * log_file{};
  std::mutex log_mutex;

  auto log_line(char const * fmt, ...) noexcept -> void
    {
    if(not log_file)
      return;
    SYSTEMTIME t;
    GetSystemTime(&t);
    char buf[2048];
    int n{std::snprintf(
      buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
      t.wSecond, t.wMilliseconds
    )};
    va_list args;
    va_start(args, fmt);
    int const m{std::vsnprintf(buf + n, sizeof buf - static_cast<std::size_t>(n) - 2, fmt, args)};
    va_end(args);
    n = m < 0 ? n : std::min<int>(n + m, static_cast<int>(sizeof buf) - 2);
    buf[n++] = '\n';
    std::lock_guard const lock{log_mutex};
    std::fwrite(buf, 1, static_cast<std::size_t>(n), log_file);
    std::fflush(log_file);
    }

  auto loader_phase() noexcept -> void
    {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self_module, path, MAX_PATH);
    module_dir = path;
    if(auto const slash{module_dir.find_last_of(L"\\/")}; slash != std::wstring::npos)
      module_dir.resize(slash);
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    system_element.name = std::wstring{sys} + L"\\d3d11.dll";
    system_module = LoadLibraryW(system_element.name.c_str());
    system_element.module = system_module;
    for(std::size_t i{}; i != export_count; ++i)
      {
      void * p{system_module ? reinterpret_cast<void *>(GetProcAddress(system_module, export_names[i])) : nullptr};
      edvr_realProcs_d3d11[i] = p ? p : reinterpret_cast<void *>(&edvr_unresolved_d3d11);
      }
    if(system_module)
      {
      system_element.create = reinterpret_cast<create_device_fn>(GetProcAddress(system_module, "D3D11CreateDevice"));
      system_element.create_swap
        = reinterpret_cast<create_device_swap_fn>(GetProcAddress(system_module, "D3D11CreateDeviceAndSwapChain"));
      }
    }

  auto trim(std::string_view s) -> std::string_view
    {
    while(not s.empty() and (s.front() == ' ' or s.front() == '\t'))
      s.remove_prefix(1);
    while(not s.empty() and (s.back() == ' ' or s.back() == '\t' or s.back() == '\r' or s.back() == '\n'))
      s.remove_suffix(1);
    return s;
    }

  auto widen(std::string_view s) -> std::wstring
    {
    if(s.empty())
      return {};
    int const n{MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0)};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
    }

  auto full_path(std::wstring const & name) -> std::wstring
    {
    std::wstring path{name};
    if(path.find(L':') == std::wstring::npos and path.find(L'\\') == std::wstring::npos)
      path = module_dir + L"\\" + path;
    return path;
    }

  ///\brief edloader.txt: one dll per line, in call order from the game; `+name` = load only (not a d3d11 proxy);
  /// `#` or `;` starts a comment
  auto load_list() -> void
    {
    std::wstring const list_path{module_dir + L"\\edloader.txt"};
    std::FILE * f{_wfopen(list_path.c_str(), L"rb")};
    if(not f)
      {
      log_line("no %S: everything goes to the system d3d11.dll", list_path.c_str());
      return;
      }
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(self_module, self, MAX_PATH);
    char line[1024];
    while(std::fgets(line, sizeof line, f))
      {
      std::string_view text{trim(line)};
      if(auto const hash{text.find_first_of("#;")}; hash != std::string_view::npos)
        text = trim(text.substr(0, hash));
      if(text.empty())
        continue;
      bool const plain{text.front() == '+'};
      if(plain)
        text = trim(text.substr(1));
      std::wstring const name{widen(text)};
      std::wstring const path{full_path(name)};
      wchar_t want[MAX_PATH]{};
      if(GetFullPathNameW(path.c_str(), MAX_PATH, want, nullptr) and _wcsicmp(self, want) == 0)
        {
        log_line("%S is edloader itself; skipped", name.c_str());
        continue;
        }
      HMODULE const module{LoadLibraryW(path.c_str())};
      if(not module)
        {
        log_line("%S: cannot load (error %lu); skipped", path.c_str(), GetLastError());
        continue;
        }
      if(plain)
        {
        log_line("%S: loaded (plugin, not chained)", name.c_str());
        continue;
        }
      element_t e{name, module, reinterpret_cast<create_device_fn>(GetProcAddress(module, "D3D11CreateDevice")),
                  reinterpret_cast<create_device_swap_fn>(GetProcAddress(module, "D3D11CreateDeviceAndSwapChain"))};
      if(not e.create)
        {
        log_line("%S: exports no D3D11CreateDevice, so not a d3d11 proxy; kept loaded, not chained", name.c_str());
        continue;
        }
      log_line("chain[%zu] = %S%s", chain.size(), name.c_str(), e.create_swap ? "" : " (no D3D11CreateDeviceAndSwapChain)");
      chain.push_back(e);
      }
    std::fclose(f);
    log_line("chain: game -> %zu proxy(ies) -> system d3d11.dll", chain.size());
    }

  INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

  BOOL CALLBACK init_callback(PINIT_ONCE, PVOID, PVOID *)
    {
    log_file = _wfopen((module_dir + L"\\edloader.log").c_str(), L"ab");
    log_line("edloader %s", EDLOADER_VERSION);
    load_list();
    return TRUE;
    }

  auto ensure_initialised() noexcept -> void { InitOnceExecuteOnce(&init_once, init_callback, nullptr, nullptr); }

  thread_local int depth{};
  thread_local std::uint32_t entered{};  // bit i: chain[i] called back into us during the outermost call

  struct depth_guard_t
    {
    depth_guard_t() noexcept { ++depth; }

    ~depth_guard_t() { --depth; }
    };

  ///\brief the element the call goes to, from the module that made it
  auto route(void * return_address, char const * what) noexcept -> element_t const &
    {
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
    if(reported)
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
  }  // namespace

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
    entered = 0;
  element_t const & target{route(_ReturnAddress(), "D3D11CreateDevice")};
  if(not target.create)
    return E_FAIL;
  HRESULT const hr{target.create(adapter, driver_type, software, flags, levels, level_count, sdk, device, level, context)};
  if(depth == 1)
    {
    log_line("D3D11CreateDevice: hr 0x%08lX through %zu element(s)", static_cast<unsigned long>(hr), chain.size());
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
    entered = 0;
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
    report_shortcuts();
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
