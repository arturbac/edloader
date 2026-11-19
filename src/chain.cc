// edloader - the chain: the system copy in DllMain, the list (edloader.txt) loaded on the first export call.
#include "runtime.h"
#include "chain.h"
#include "pe_read.h"
#include "takeovers.h"

#include <string>

extern "C"
  {
  extern void * edvr_realProcs_d3d11[];
  void edvr_unresolved_d3d11();
  }


#include <string_view>

#include <stralgo/stralgo.h>

namespace edloader
  {
  using namespace std::string_view_literals;

  char const * const export_names[]{
#include "edvr_exports_d3d11.inc"
  };
  constexpr std::size_t export_count{sizeof(export_names) / sizeof(export_names[0])};

  auto loader_paths() -> void;

  // in DllMain, where nothing may be thrown out (out of memory for a path): what is not made stays empty, the exports
  // unresolved, and edloader passes nothing on (2026-11-18, the second audit)
  auto loader_phase() noexcept -> void
    {
    try
      {
      loader_paths();
      }
    catch(...)
      {
      system_module = nullptr;
      }
    for(std::size_t i{}; i != export_count; ++i)
      if(not edvr_realProcs_d3d11[i])
        edvr_realProcs_d3d11[i] = reinterpret_cast<void *>(&edvr_unresolved_d3d11);
    }

  auto loader_paths() -> void
    {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self_module, path, MAX_PATH);
    module_dir = path;
    if(auto const slash{module_dir.find_last_of(L"\\/")}; slash != std::wstring::npos)
      module_dir.resize(slash);
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    system_element.name = stralgo::stl::merge(std::wstring_view{sys}, L"\\d3d11.dll"sv);
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
      path = stralgo::stl::merge(root_dir, L"\\plugins\\"sv, path);
    return path;
    }


  auto load_list() -> void
    {
    std::wstring const list_path{stralgo::stl::merge(root_dir, L"\\edloader.txt"sv)};
    std::FILE * f{_wfopen(list_path.c_str(), L"rb")};
    if(not f)
      {
      log_line("no %S: everything goes to the system d3d11.dll", list_path.c_str());
      return;
      }
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(self_module, self, MAX_PATH);
    char line[1024];
    // the settings first, wherever they stand in the list: they decide what a takeover while loading does
    while(std::fgets(line, sizeof line, f))
      {
      std::string_view text{trim(line)};
      if(auto const hash{text.find_first_of("#;")}; hash != std::string_view::npos)
        text = trim(text.substr(0, hash));
      auto const eq{text.find('=')};
      if(eq == std::string_view::npos)
        continue;
      std::string_view const key{trim(text.substr(0, eq))}, value{trim(text.substr(eq + 1))};
      if(key == "accept_takeover")
        {
        accept_takeover = value == "true" or value == "1" or value == "yes";
        log_line("accept_takeover = %s", accept_takeover ? "true: a takeover is logged and the list still passed on" : "false");
        }
      else
        log_line("unknown setting '%.*s' ignored", static_cast<int>(key.size()), key.data());
      }
    std::rewind(f);
    while(std::fgets(line, sizeof line, f))
      {
      std::string_view text{trim(line)};
      if(auto const hash{text.find_first_of("#;")}; hash != std::string_view::npos)
        text = trim(text.substr(0, hash));
      if(text.empty())
        continue;
      if(text.find('=') != std::string_view::npos)
        continue;  // a setting, read above
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
      list_files.push_back(module_of(reinterpret_cast<void const *>(module)));
      {
      std::string const label{stralgo::stl::merge("after loading "sv, text)};
      guard_own_exports(label.c_str(), std::string{text}.c_str());
      check_takers(label.c_str());
      check_loader(label.c_str());
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


  // ---- edloader's own work in the game's process: a fault or an exception there is logged and that step skipped ----
  // Reading other modules' headers, import tables and memory (a module unloaded meanwhile, a page gone) and the
  // strings of the checks (out of memory) must never end the game. The steps below are not noexcept, so a C++
  // exception reaches the handler; it catches hardware faults and C++ exceptions alike.
  auto guarded(char const * what, void (*step)(char const *), char const * when) noexcept -> bool
    {
    DWORD code{};
    __try
      {
      step(when);
      return true;
      }
    __except(code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
      {
      log_line("%s: failed (exception 0x%08lX%s); skipped", what, code, code == 0xE06D7363u ? ", a C++ exception" : "");
      return false;
      }
    }

  // the checks at a device change edloader's records of the last state: one thread at a time
  SRWLOCK checks_lock = SRWLOCK_INIT;

  auto device_checks(char const * when) noexcept -> void
    {
    AcquireSRWLockExclusive(&checks_lock);
    guarded("own exports", [](char const * w) { guard_own_exports(w, "a dll"); }, when);
    guarded("takers", [](char const * w) { check_takers(w); }, when);
    guarded("loader", [](char const * w) { check_loader(w); }, when);
    guarded("loader imports", [](char const * w) { sweep_loader_imports(w); }, when);
    ReleaseSRWLockExclusive(&checks_lock);
    }

  auto init_steps(char const *) -> void
    {
    root_dir = find_root_dir();
    make_dirs(root_dir);
    log_file = _wfopen(stralgo::stl::merge(root_dir, L"\\logs\\edloader.log"sv).c_str(), L"ab");
    log_line("edloader %s", EDLOADER_VERSION);
    log_line("root: %S", root_dir.c_str());
    // the plugins' own files: plugins made for edloader read these, without edloader they fall back to beside their dll
    SetEnvironmentVariableW(L"EDLOADER_CONFIG_DIR", stralgo::stl::merge(root_dir, L"\\config"sv).c_str());
    SetEnvironmentVariableW(L"EDLOADER_LOG_DIR", stralgo::stl::merge(root_dir, L"\\logs"sv).c_str());
    guarded("start", [](char const *) { start_takers(); }, nullptr);
    guarded("own exports kept", [](char const *) { keep_own_exports(); }, nullptr);
    guarded("the list", [](char const *) { load_list(); }, nullptr);
    guarded("loader imports", [](char const * w) { sweep_loader_imports(w); }, "after the list");
    if(takeover and accept_takeover and stopped)
      {
      stopped = false;  // found before the list's accept_takeover was read
      log_line("TAKEOVER accepted (accept_takeover = true): the list is passed on, at your own risk");
      }
    }

  INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

  BOOL CALLBACK init_callback(PINIT_ONCE, PVOID, PVOID *)
    {
    guarded("edloader's start", &init_steps, nullptr);
    return TRUE;
    }

  auto ensure_initialised() noexcept -> void { InitOnceExecuteOnce(&init_once, init_callback, nullptr, nullptr); }
  }  // namespace edloader
