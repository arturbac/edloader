// edloader - who else takes the game's calls: the game's imports, the loader's functions, edloader's own exports.
#include "runtime.h"
#include "takeovers.h"
#include "chain.h"
#include "pe_read.h"

#include <tlhelp32.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <string_view>

#include <stralgo/stralgo.h>

namespace edloader
  {
  using namespace std::string_view_literals;

  // ---- who else takes the game's calls: the game's imports of d3d11 and the system copy's first bytes ----
  // A dll can take the game's d3d11 calls past the list: by rewriting the game's import of them (the exe's import
  // address table) or by overwriting the first bytes of the system copy's functions (an inline hook), typically from
  // its DllMain while edloader loads it. Both are checked after each dll of the list and at each device the game
  // asks for, and every change is logged with the module it now leads to.
  struct watched_import_t
    {
    char const * name;
    void ** slot;                 ///< the game's import slot; null when the game does not import it by name
    void * last;                  ///< what the slot held at the last check
    std::uint8_t prologue[16];    ///< the system copy's first bytes at the last check
    void * system;                ///< the system copy's function
    };

  ///\brief dlls known to take the game's calls for a reason of their own, not a mod's: their takeover is accepted
  /// without accept_takeover and logged once in short. Epic's overlay rewires the game's D3D11CreateDevice and the
  /// loader's functions of nearly every dll a moment after the game's first device
  struct known_taker_t
    {
    wchar_t const * file;
    char const * what;
    std::uint32_t imports;  ///< imports of the loader's functions leading to it, at the last sweep
    bool told;
    };

  known_taker_t known_takers[]{{L"EOSOVH-Win64-Shipping.dll", "Epic's overlay", 0u, false}};

  auto known_taker(std::wstring const & module) noexcept -> known_taker_t *
    {
    for(known_taker_t & k: known_takers)
      if(_wcsicmp(module.c_str(), k.file) == 0)
        return &k;
    return nullptr;
    }

  ///\brief a dll took the game's d3d11 calls past the list (or something had before edloader started): edloader
  /// stops passing calls down the list unless the list accepts it (`accept_takeover = true` in edloader.txt)
  bool takeover{};
  bool accept_takeover{};
  bool stopped{};

  ///\brief the file names (no path, as module_name_of gives them) of the dlls edloader loaded from the list, chained or
  /// not, each added before the checks that follow its loading
  std::vector<std::wstring> list_files;

  ///\brief a takeover by a dll of the list, not by something loaded outside it (an antivirus, an overlay, a wrapper)
  auto listed(std::wstring const & file) noexcept -> bool
    {
    for(std::wstring const & f: list_files)
      if(_wcsicmp(f.c_str(), file.c_str()) == 0)
        return true;
    return false;
    }

  ///\brief a loaded dll exporting ReShadeRegisterAddon (ReShade, under whatever name: dxgi.dll from its installer), its
  /// file name; empty if none. ReShade hooks the system d3d11 inline, the jump leading into memory of no module, so it is
  /// known by its export, not by where its hook leads (ReShade 6.8.0, 2026-11-17)
  auto reshade_loaded() -> std::wstring
    {
    HANDLE const snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId())};
    if(snap == INVALID_HANDLE_VALUE)
      return {};
    std::wstring found;
    MODULEENTRY32W m{};
    m.dwSize = sizeof m;
    for(BOOL ok{Module32FirstW(snap, &m)}; ok and found.empty(); ok = Module32NextW(snap, &m))
      if(GetProcAddress(m.hModule, "ReShadeRegisterAddon"))
        found = m.szModule;
    CloseHandle(snap);
    return found;
    }

  bool reshade_told{};

  ///\brief what made edloader stop, said once; by: the dll that took the calls, when known
  auto take_over(std::string const & what, std::wstring const & by = {}) -> void
    {
    if(known_taker_t * const k{known_taker(by)})
      {
      if(not k->told)
        log_line("TAKEOVER: {}; {} is {}, a known taker: accepted, the list is passed on", what, wide(k->file), k->what);
      k->told = true;
      return;
      }
    // a hook into memory of no module (or of no module told), or into ReShade's own dll, with ReShade loaded is ReShade's
    if(by.empty() or by == L"?" or not listed(by))
      if(std::wstring const reshade{reshade_loaded()};
         not reshade.empty() and (by.empty() or by == L"?" or _wcsicmp(by.c_str(), reshade.c_str()) == 0))
        {
        if(not reshade_told)
          log_line("TAKEOVER: {}; ReShade is loaded ({}), a known taker: accepted, the list is passed on", what,
                   wide(reshade));
        reshade_told = true;
        return;
        }
    takeover = true;
    if(accept_takeover)
      {
      log_line("TAKEOVER: {}; accepted (accept_takeover = true): the list is passed on, at your own risk{}", what,
               misconfigured ? " (it stays stopped by a MISCONFIGURATION of edloader.txt)" : "");
      return;
      }
    if(not stopped)
      {
      stopped = true;
      if(listed(by))
        log_stop_line("TAKEOVER: {}. edloader stops: every call goes to the system d3d11.dll and no dll of the list is called. "
                 "Remove that dll from the list, or put it first in the list (or mark it ^) and add the line 'accept_takeover = true' to "
                 "edloader.txt: then edloader still passes the calls on, at your own risk", what);
      else
        log_stop_line("TAKEOVER: {}. edloader stops: every call goes to the system d3d11.dll and no dll of the list is called. "
                 "The hook is not from a dll of the list but from something loaded outside it (another wrapper in the "
                 "game's folder, an overlay, a security program). Add the line 'accept_takeover = true' to "
                 "edloader.txt: then edloader still passes the calls on, at your own risk", what);
      }
    else
      log_stop_line("TAKEOVER: {}", what);
    }

  watched_import_t watched_imports[2]{{"D3D11CreateDevice", nullptr, nullptr, {}, nullptr},
                                      {"D3D11CreateDeviceAndSwapChain", nullptr, nullptr, {}, nullptr}};


  ///\brief whether an import names `dll` (the case aside)
  [[nodiscard]]
  auto import_of(edplugin_common::import_t const & i, std::string_view dll) noexcept -> bool
    {
    return i.dll.size() == dll.size() and _strnicmp(i.dll.data(), dll.data(), dll.size()) == 0;
    }

  ///\brief the game exe's import slots of the two device functions, read through edplugin_common's pe_image (every
  /// offset within the image, a slot only in the image's IAT); an import without its names (no OriginalFirstThunk) is
  /// not followed
  auto find_game_imports() -> void
    {
    std::optional<edplugin_common::pe_image_t> const image{edplugin_common::pe_image(GetModuleHandleW(nullptr))};
    if(not image)
      {
      log_line("the game's imports: its headers are out of shape; its import of d3d11 not watched");
      return;
      }
    edplugin_common::import_scan_t const scan{edplugin_common::for_each_import(
      *image,
      [](edplugin_common::import_t const & i) noexcept
      {
        if(not import_of(i, "d3d11.dll"sv) or i.function.empty() or not i.slot)
          return;
        for(watched_import_t & w: watched_imports)
          if(i.function == std::string_view{w.name})
            w.slot = i.slot;
      })};
    if(scan.malformed or scan.without_names != 0 or scan.slots_refused != 0)
      log_line("the game's imports: {}{} descriptor(s) without names, {} slot(s) refused", scan.malformed ? "out of shape (read up to there), " : "",
               scan.without_names, scan.slots_refused);
    }

  // ---- the loader's own functions: a dll that hooks them hands out itself for d3d11.dll ----
  struct watched_loader_t
    {
    char const * name;
    void * function;           ///< in kernelbase
    std::uint8_t prologue[16];
    };

  watched_loader_t watched_loader[3]{{"LoadLibraryExW", nullptr, {}}, {"LoadLibraryW", nullptr, {}}, {"GetProcAddress", nullptr, {}}};
  void * own_create{};          ///< edloader's own D3D11CreateDevice export

  ///\brief what LoadLibrary("d3d11.dll") and GetProcAddress hand out now: edloader itself, unless someone redirects them
  auto check_loader(char const * when) -> void
    {
    for(watched_loader_t & w: watched_loader)
      {
      if(not w.function)
        continue;
      std::uint8_t now[16];
      std::memcpy(now, w.function, sizeof now);
      if(std::memcmp(now, w.prologue, sizeof now) != 0)
        {
        void const * const to{jump_target(now, w.function)};
        log_line("{}: kernelbase's {} starts differently now{}{}", when, w.name, to ? ", a jump to " : " (no jump read)",
                 wide(to ? module_name_of(to).c_str() : L""));
        // 3Dmigoto hooks LoadLibraryExW for its own load_library_redirect; edloader's chain does not go through the
        // loader after the list is loaded, so this is said, not stopped for
        std::memcpy(w.prologue, now, sizeof now);
        }
      }
    // What a bare "d3d11.dll" resolves to from here, logged when it changes but no takeover by itself: the loader's
    // search may hand out the system copy once a dll of the list loaded it by its full path (seen under wine)
    HMODULE const by_name{LoadLibraryW(L"d3d11.dll")};
    void * const create{by_name ? reinterpret_cast<void *>(GetProcAddress(by_name, "D3D11CreateDevice")) : nullptr};
    static HMODULE last_by_name{};
    static void * last_create{};
    if(by_name != last_by_name or create != last_create)
      {
      last_by_name = by_name;
      last_create = create;
      wchar_t got[MAX_PATH]{};
      if(by_name)
        GetModuleFileNameW(by_name, got, MAX_PATH);
      log_line("{}: LoadLibrary(\"d3d11.dll\") from edloader gives {}{}; its D3D11CreateDevice is in {}", when, wide(got),
               by_name == self_module ? " (edloader)" : "", wide(module_name_of(create)));
      }
    if(by_name)
      FreeLibrary(by_name);
    }

  ///\brief compares the game's import slots and the system copy's first bytes with the last check; logs changes
  auto check_takers(char const * when) -> void
    {
    for(watched_import_t & w: watched_imports)
      {
      if(w.slot)
        {
        void * const now{*w.slot};
        if(now != w.last)
          {
          log_line("{}: the game's import of {} now leads to {} ({})", when, w.name, wide(module_name_of(now)), static_cast<void const *>(now));
          if(module_name_of(now) != module_name_of(w.last))
            take_over(stralgo::stl::merge(std::string_view{when}, ": "sv, narrow(module_name_of(now)), " took the game's import of "sv, std::string_view{w.name}),
                      module_name_of(now));
          w.last = now;
          }
        }
      if(w.system)
        {
        std::uint8_t now[16];
        std::memcpy(now, w.system, sizeof now);
        if(std::memcmp(now, w.prologue, sizeof now) != 0)
          {
          void const * const to{jump_target(now, w.system)};
          log_line("{}: the system d3d11's {} starts differently now{}{}", when, w.name, to ? ", a jump to " : " (no jump read)",
                   wide(to ? module_name_of(to).c_str() : L""));
          take_over(stralgo::stl::merge(std::string_view{when}, ": the system d3d11's "sv, std::string_view{w.name}, " was hooked inline"sv,
                                        to ? stralgo::stl::merge(" (a jump to "sv, narrow(module_name_of(to)), ')') : std::string{}),
                    to ? module_name_of(to) : std::wstring{});
          std::memcpy(w.prologue, now, sizeof now);
          }
        }
      }
    }

  // ---- the loader's functions as other modules import them ----
  // A dll can hand out itself for d3d11.dll by rewriting other modules' imports of LoadLibrary* and GetProcAddress
  // (their import address tables), which leaves kernelbase's own bytes as they were. After the list is loaded and at
  // each device the game asks for, every module's imports of them are compared with the real functions; an import
  // leading elsewhere is logged and put back, unless the list accepts takeovers.
  constexpr char const * loader_functions[]{"LoadLibraryA", "LoadLibraryW", "LoadLibraryExA", "LoadLibraryExW", "GetProcAddress"};

  ///\brief an export's address read from the module's export table in memory (no GetProcAddress, which may be hooked),
  /// through edplugin_common's pe_image and export_address (forwarders followed twice at most); nullptr when none
  auto export_address(HMODULE module, char const * name) noexcept -> void *
    {
    std::optional<edplugin_common::pe_image_t> const image{edplugin_common::pe_image(module)};
    return image ? const_cast<void *>(edplugin_common::export_address(*image, name)) : nullptr;
    }

  // ---- edloader's own exports: what 3Dmigoto (EDHM) hooks ----
  // 3Dmigoto loaded under another name than d3d11.dll hooks D3D11CreateDevice(AndSwapChain) inline in the module named
  // d3d11.dll from its DllMain (DirectX11/DLLMainHook.cpp, HookD3D11) - here that is edloader. Every call into edloader
  // would then go to it first, and the calls it passes on through its trampoline would come from it, so the list
  // would be skipped past it. edloader keeps its exports' first bytes and puts them back after each dll of the list;
  // the dll stays a chain element like any other, and its trampoline still reaches edloader's code.
  struct own_export_t
    {
    std::string name;
    std::uint8_t * at;
    std::uint8_t bytes[32];
    };

  std::vector<own_export_t> own_exports;

  ///\brief whether `bytes` bytes at `rva` lie in one section of the image that holds code: the only place a write of
  /// edloader's own exports' bytes may go
  [[nodiscard]]
  auto in_code(edplugin_common::pe_image_t const & image, std::uint32_t rva, std::uint32_t bytes) noexcept -> bool
    {
    IMAGE_SECTION_HEADER const * const s{image.section_of(rva)};
    if(not s or (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
      return false;
    std::uint64_t const size{s->Misc.VirtualSize != 0 ? s->Misc.VirtualSize : s->SizeOfRawData};
    return edplugin_common::rva_in(rva - s->VirtualAddress, bytes, size) and image.span(rva, bytes).has_value();
    }

  ///\brief every export of edloader's own table (the wrapped D3D11CreateDevice(AndSwapChain) among them, which the
  /// generated thunk list lacks), its first 32 bytes, before any dll of the list is loaded
  auto keep_own_exports() -> void
    {
    std::optional<edplugin_common::pe_image_t> const image{edplugin_common::pe_image(self_module)};
    if(not image)
      {
      log_line("own exports kept: edloader's own headers out of shape; none kept");
      return;
      }
    int outside{};
    bool const whole{edplugin_common::for_each_export(*image,
                                                       [&](edplugin_common::export_t const & e)
                                                       {
                                                         if(e.forwarder)
                                                           return true;  // a forwarder: no code of ours
                                                         if(not in_code(*image, e.rva, sizeof(own_export_t::bytes)))
                                                           {
                                                           ++outside;
                                                           return true;
                                                           }
                                                         own_export_t x{std::string{e.name}, const_cast<std::uint8_t *>(image->base()) + e.rva, {}};
                                                         std::memcpy(x.bytes, x.at, sizeof x.bytes);
                                                         own_exports.push_back(x);
                                                         return true;
                                                       })};
    log_line("own exports kept: {}, their first {} bytes{}{}", own_exports.size(), sizeof(own_export_t::bytes),
             outside != 0 ? "; not kept (not wholly in a section of code): " : "", outside != 0 ? std::to_string(outside) : std::string{});
    if(not whole)
      log_line("own exports kept: edloader's export directory out of shape; read up to there");
    }

  ///\brief puts back the first bytes of edloader's exports a dll overwrote (3Dmigoto's HookD3D11 from its DllMain), naming
  /// that dll: the one just loaded (a hook library jumps through its own stub, so the jump's module would say nothing)
  auto guard_own_exports(char const * when, char const * who) -> void
    {
    for(own_export_t const & e: own_exports)
      {
      if(std::memcmp(e.at, e.bytes, sizeof e.bytes) == 0)
        continue;
      DWORD old{};
      if(VirtualProtect(e.at, sizeof e.bytes, PAGE_EXECUTE_READWRITE, &old))
        {
        // the write under the guard, the page's rights put back after it however it ends
        edplugin_common::fault_t fault;
        bool const written{edplugin_common::run_guarded([&] { std::memcpy(e.at, e.bytes, sizeof e.bytes); }, fault)};
        VirtualProtect(e.at, sizeof e.bytes, old, &old);
        FlushInstructionCache(GetCurrentProcess(), e.at, sizeof e.bytes);
        if(written)
          log_line("{}: {} hooked edloader's own {} inline; put back (it stays in the list as any other element)", when, who,
                   e.name);
        else
          {
          char text[320];
          log_line("{}: {} hooked edloader's own {} inline; could not be put back ({})", when, who, e.name,
                   edplugin_common::fault_line(fault, text));
          }
        }
      else
        log_line("{}: {} hooked edloader's own {} inline; could not be put back (error {})", when, who, e.name,
                 GetLastError());
      }
    }

  ///\brief a protection that adds writing to what the page has: an import table on a page with code (merged sections)
  /// keeps its execute right while it is written, so another thread running that page does not fault
  auto writable_like(void const * p) noexcept -> DWORD
    {
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(p, &info, sizeof info) != sizeof info)
      return PAGE_READWRITE;
    DWORD const execute{PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY};
    return (info.Protect & execute) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    }

  ///\brief a module passed over (wholly or in part) for its headers or imports, said once a module and reason a
  /// session; `detail` (a fault's line) is said with it, not part of the reason
  auto out_of_shape_once(char const * when, std::wstring const & module, char const * why, std::string_view detail = {})
    -> void
    {
    static std::vector<std::string> told;
    std::string const key{stralgo::stl::merge(narrow(module), "|"sv, std::string_view{why})};
    for(std::string const & t: told)
      if(_stricmp(t.c_str(), key.c_str()) == 0)
        return;
    told.push_back(key);
    log_line("{}: {} {}{}; passed over", when, narrow(module), why, detail);
    }

  ///\brief one module's imports of the loader's functions (sweep_loader_imports), read through edplugin_common's
  /// pe_image: every offset within the image, a slot written only when it is in the image's IAT, the write under the
  /// guard and the page's rights put back after it however it ends
  auto sweep_module(char const * when, MODULEENTRY32W const & m, void * const (&real)[std::size(loader_functions)],
                    void * const (&stub)[std::size(loader_functions)], std::uint32_t (&known_now)[std::size(known_takers)]) -> void
    {
    std::optional<edplugin_common::pe_image_t> const image{edplugin_common::pe_image(m.hModule, m.modBaseSize)};
    if(not image)
      {
      out_of_shape_once(when, m.szModule, "has headers out of shape");
      return;
      }
    std::vector<edplugin_common::import_t> loader_imports;
    edplugin_common::import_scan_t const scan{edplugin_common::for_each_import(*image,
                                                                              [&](edplugin_common::import_t const & i)
                                                                              {
                                                                                for(char const * const f: loader_functions)
                                                                                  if(i.function == std::string_view{f})
                                                                                    {
                                                                                    loader_imports.push_back(i);
                                                                                    return;
                                                                                    }
                                                                              })};
    if(scan.malformed)
      out_of_shape_once(when, m.szModule, "has its imports out of shape (read up to there)");
    if(scan.past_size)
      out_of_shape_once(when, m.szModule, "has import descriptors past its directory's size (read on)");
    if(scan.without_names != 0)
      out_of_shape_once(when, m.szModule, "has import descriptors without names (those not checked)");
    for(edplugin_common::import_t const & imp: loader_imports)
      {
      std::size_t i{};
      while(imp.function != std::string_view{loader_functions[i]})
        ++i;
      if(not real[i])
        continue;
      if(not imp.slot)
        {
        out_of_shape_once(when, m.szModule, "has an import slot of the loader's functions outside its IAT");
        continue;
        }
      void * const now{*imp.slot};
      if(now == real[i] or now == stub[i])
        continue;
      std::wstring const to{module_name_of(now)};
      if(_wcsicmp(to.c_str(), m.szModule) == 0)
        continue;  // the module's own wrapper of its own import: not a takeover of others
      if(known_taker_t const * const k{known_taker(to)})
        {
        ++known_now[k - known_takers];
        continue;  // a known taker's: left as it is, told in short below
        }
      std::string const what{stralgo::stl::merge(std::string_view{when}, ": "sv, narrow(m.szModule), "'s import of "sv, imp.function,
                                                 " leads to "sv, narrow(to))};
      if(accept_takeover)
        {
        log_line("TAKEOVER: {}; accepted, left as it is", what);
        continue;
        }
      DWORD old{};
      if(VirtualProtect(imp.slot, sizeof(void *), writable_like(imp.slot), &old))
        {
        edplugin_common::fault_t fault;
        bool const written{edplugin_common::run_guarded([&] { *imp.slot = real[i]; }, fault)};
        VirtualProtect(imp.slot, sizeof(void *), old, &old);
        if(written)
          log_line("TAKEOVER: {}; put back to the real {}", what, imp.function);
        else
          {
          char text[320];
          log_line("TAKEOVER: {}; could not be put back ({})", what, edplugin_common::fault_line(fault, text));
          }
        }
      else
        log_line("TAKEOVER: {}; could not be put back (error {})", what, GetLastError());
      }
    }

  ///\brief every module's imports of the loader's functions: logged when they lead past kernel32/kernelbase, put back
  /// unless accept_takeover; once a frame of device calls at most
  auto sweep_loader_imports(char const * when) -> void
    {
    HMODULE const kernel32{GetModuleHandleW(L"kernel32.dll")}, kernelbase{GetModuleHandleW(L"kernelbase.dll")};
    void * real[std::size(loader_functions)]{};
    void * stub[std::size(loader_functions)]{};
    for(std::size_t i{}; i != std::size(loader_functions); ++i)
      {
      real[i] = export_address(kernelbase, loader_functions[i]);
      stub[i] = export_address(kernel32, loader_functions[i]);
      if(not real[i])
        real[i] = stub[i];
      }
    HANDLE const snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId())};
    if(snap == INVALID_HANDLE_VALUE)
      return;
    std::uint32_t known_now[std::size(known_takers)]{};
    MODULEENTRY32W m{};
    m.dwSize = sizeof m;
    for(BOOL ok{Module32FirstW(snap, &m)}; ok; ok = Module32NextW(snap, &m))
      {
      // each module read under the guard: one unloaded meanwhile faults, and is passed over
      edplugin_common::fault_t fault;
      if(not edplugin_common::run_guarded([&] { sweep_module(when, m, real, stub, known_now); }, fault))
        {
        char text[320];
        out_of_shape_once(when, m.szModule, "faulted while its imports were read: ",
                          edplugin_common::fault_line(fault, text));
        }
      }
    CloseHandle(snap);
    for(std::size_t i{}; i != std::size(known_takers); ++i)
      if(known_now[i] != known_takers[i].imports)
        {
        log_line("TAKEOVER: {}: {} imports of the loader's functions lead to {}, {}, a known taker: left as they are", when,
                 known_now[i], wide(known_takers[i].file), known_takers[i].what);
        known_takers[i].imports = known_now[i];
        }
    }

  ///\brief the first bytes of an exported function as the dll's file on disk has them; false when they cannot be
  /// compared (not found, forwarded, or a relocation inside them)

  auto log_modules() -> void
    {
    HANDLE const snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId())};
    if(snap == INVALID_HANDLE_VALUE)
      return;
    MODULEENTRY32W m{};
    m.dwSize = sizeof m;
    std::wstring all;
    for(BOOL ok{Module32FirstW(snap, &m)}; ok; ok = Module32NextW(snap, &m))
      {
      if(not all.empty())
        all += L", ";
      all += m.szModule;
      }
    CloseHandle(snap);
    log_line("modules before the list: {}", wide(all));
    }

  ///\brief the starting point of the checks: where the game's imports lead and the system copy's first bytes,
  /// against the system copy's file on disk (a hook made before edloader started shows there)
  auto start_takers() -> void
    {
    log_modules();
    find_game_imports();
    own_create = reinterpret_cast<void *>(GetProcAddress(self_module, "D3D11CreateDevice"));
    if(HMODULE const kernelbase{GetModuleHandleW(L"kernelbase.dll")})
      {
      wchar_t path[MAX_PATH]{};
      GetModuleFileNameW(kernelbase, path, MAX_PATH);
      for(watched_loader_t & w: watched_loader)
        {
        w.function = reinterpret_cast<void *>(GetProcAddress(kernelbase, w.name));
        if(not w.function)
          continue;
        std::memcpy(w.prologue, w.function, sizeof w.prologue);
        std::uint8_t disk[16];
        std::wstring why;
        if(not bytes_on_disk(path, w.name, disk, why))
          log_line("kernelbase's {} cannot be compared with its file ({})", w.name, wide(why));
        else if(std::memcmp(disk, w.prologue, sizeof disk) != 0)
          {
          void const * const to{jump_target(w.prologue, w.function)};
          log_line("before edloader started: kernelbase's {} differs from its file{}{} (said, not stopped for)", w.name,
                   to ? " (a jump to " : "", wide(to ? (module_name_of(to) + L")").c_str() : L""));
          }
        else
          log_line("kernelbase's {} is as its file has it", w.name);
        }
      }
    check_loader("at start");
    watched_imports[0].system = reinterpret_cast<void *>(system_element.create);
    watched_imports[1].system = reinterpret_cast<void *>(system_element.create_swap);
    for(watched_import_t & w: watched_imports)
      {
      if(w.system)
        {
        std::memcpy(w.prologue, w.system, sizeof w.prologue);
        std::uint8_t disk[16];
        std::wstring why;
        if(not bytes_on_disk(system_element.name, w.name, disk, why))
          log_line("the system d3d11's {} cannot be compared with its file ({})", w.name, wide(why));
        else if(std::memcmp(disk, w.prologue, sizeof disk) != 0)
          {
          void const * const to{jump_target(w.prologue, w.system)};
          take_over(stralgo::stl::merge("before edloader started: the system d3d11's "sv, std::string_view{w.name}, " differs from its file"sv,
                                        to ? stralgo::stl::merge(" (a jump to "sv, narrow(module_name_of(to)), ')') : std::string{}),
                    to ? module_name_of(to) : std::wstring{});
          }
        else
          log_line("the system d3d11's {} is as its file has it", w.name);
        }
      if(w.slot)
        {
        w.last = *w.slot;
        log_line("the game imports {} from d3d11.dll; it leads to {}", w.name, wide(module_name_of(w.last)));
        }
      else
        log_line("the game does not import {} by name from d3d11.dll", w.name);
      }
    }

  ///\brief edloader.txt: one dll per line, in call order from the game; `+name` = load only (not a d3d11 proxy);
  /// `#` or `;` starts a comment
  }  // namespace edloader
