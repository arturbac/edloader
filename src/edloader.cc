// edloader — a d3d11.dll that chains several d3d11 proxies (EDVR, EDHM, edworld, ReShade...) in the order
// listed in %USERPROFILE%\edloader\edloader.txt (one place outside the game's folder: see root_dir), the way ASI
// loaders take a list of plugins.
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

#include <tlhelp32.h>

#include <d3d11.h>
#include <intrin.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
  std::wstring root_dir;  // holds edloader.txt and the folders plugins (the list's relative names), config, logs
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
      path = root_dir + L"\\plugins\\" + path;
    return path;
    }

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

  ///\brief a dll took the game's d3d11 calls past the list (or something had before edloader started): edloader
  /// stops passing calls down the list unless the list accepts it (`accept_takeover = true` in edloader.txt)
  bool takeover{};
  bool accept_takeover{};
  bool stopped{};

  ///\brief what made edloader stop, said once
  auto take_over(std::string const & what) noexcept -> void
    {
    takeover = true;
    if(accept_takeover)
      {
      log_line("TAKEOVER: %s; accepted (accept_takeover = true): the list is passed on, at your own risk", what.c_str());
      return;
      }
    if(not stopped)
      {
      stopped = true;
      log_line("TAKEOVER: %s. edloader stops: every call goes to the system d3d11.dll and no dll of the list is called. "
               "Remove that dll from the list, or put it first in the list and add the line 'accept_takeover = true' to "
               "edloader.txt: then edloader still passes the calls on, at your own risk", what.c_str());
      }
    else
      log_line("TAKEOVER: %s", what.c_str());
    }

  watched_import_t watched_imports[2]{{"D3D11CreateDevice", nullptr, nullptr, {}, nullptr},
                                      {"D3D11CreateDeviceAndSwapChain", nullptr, nullptr, {}, nullptr}};

  auto narrow(std::wstring const & w) -> std::string
    {
    std::string out;
    for(wchar_t const c: w)
      out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
    }

  ///\brief the module an address lies in, by name; "?" when none
  auto module_of(void const * address) -> std::wstring
    {
    HMODULE m{};
    if(not address or not GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                             static_cast<LPCWSTR>(address), &m))
      return L"?";
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring name{path};
    if(auto const slash{name.find_last_of(L"\\/")}; slash != std::wstring::npos)
      name.erase(0, slash + 1);
    return name;
    }

  ///\brief where a jump at the start of a function leads (E9 rel32, FF 25 [rip+rel32], 48 B8 imm64 + FF E0); null if none
  auto jump_target(std::uint8_t const * p) -> void const *
    {
    if(p[0] == 0xE9)
      {
      std::int32_t rel;
      std::memcpy(&rel, p + 1, 4);
      return p + 5 + rel;
      }
    if(p[0] == 0xFF and p[1] == 0x25)
      {
      std::int32_t rel;
      std::memcpy(&rel, p + 2, 4);
      void const * target;
      std::memcpy(&target, p + 6 + rel, sizeof target);
      return target;
      }
    if(p[0] == 0x48 and p[1] == 0xB8 and p[10] == 0xFF and p[11] == 0xE0)
      {
      void const * target;
      std::memcpy(&target, p + 2, sizeof target);
      return target;
      }
    return nullptr;
    }

  ///\brief the game exe's import slots of the two device functions from d3d11.dll, once
  auto find_game_imports() -> void
    {
    auto const base{reinterpret_cast<std::uint8_t *>(GetModuleHandleW(nullptr))};
    auto const dos{reinterpret_cast<IMAGE_DOS_HEADER const *>(base)};
    auto const nt{reinterpret_cast<IMAGE_NT_HEADERS const *>(base + dos->e_lfanew)};
    IMAGE_DATA_DIRECTORY const & dir{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]};
    if(dir.VirtualAddress == 0)
      return;
    for(auto desc{reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR const *>(base + dir.VirtualAddress)}; desc->Name; ++desc)
      {
      if(_stricmp(reinterpret_cast<char const *>(base + desc->Name), "d3d11.dll") != 0)
        continue;
      auto names{reinterpret_cast<IMAGE_THUNK_DATA const *>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk))};
      auto slots{reinterpret_cast<IMAGE_THUNK_DATA *>(base + desc->FirstThunk)};
      for(; names->u1.AddressOfData; ++names, ++slots)
        {
        if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
          continue;
        auto const by_name{reinterpret_cast<IMAGE_IMPORT_BY_NAME const *>(base + names->u1.AddressOfData)};
        for(watched_import_t & w: watched_imports)
          if(std::strcmp(reinterpret_cast<char const *>(by_name->Name), w.name) == 0)
            w.slot = reinterpret_cast<void **>(&slots->u1.Function);
        }
      }
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
  auto check_loader(char const * when) noexcept -> void
    {
    for(watched_loader_t & w: watched_loader)
      {
      if(not w.function)
        continue;
      std::uint8_t now[16];
      std::memcpy(now, w.function, sizeof now);
      if(std::memcmp(now, w.prologue, sizeof now) != 0)
        {
        void const * const to{jump_target(now)};
        log_line("%s: kernelbase's %s starts differently now%s%S", when, w.name, to ? ", a jump to " : " (no jump read)",
                 to ? module_of(to).c_str() : L"");
        take_over(std::string{when} + ": kernelbase's " + w.name + " was hooked inline" +
                  (to ? " (a jump to " + narrow(module_of(to)) + ")" : std::string{}));
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
      log_line("%s: LoadLibrary(\"d3d11.dll\") from edloader gives %S%s; its D3D11CreateDevice is in %S", when, got,
               by_name == self_module ? " (edloader)" : "", module_of(create).c_str());
      }
    if(by_name)
      FreeLibrary(by_name);
    }

  ///\brief compares the game's import slots and the system copy's first bytes with the last check; logs changes
  auto check_takers(char const * when) noexcept -> void
    {
    for(watched_import_t & w: watched_imports)
      {
      if(w.slot)
        {
        void * const now{*w.slot};
        if(now != w.last)
          {
          log_line("%s: the game's import of %s now leads to %S (%p)", when, w.name, module_of(now).c_str(), now);
          if(module_of(now) != module_of(w.last))
            take_over(std::string{when} + ": " + narrow(module_of(now)) + " took the game's import of " + w.name);
          w.last = now;
          }
        }
      if(w.system)
        {
        std::uint8_t now[16];
        std::memcpy(now, w.system, sizeof now);
        if(std::memcmp(now, w.prologue, sizeof now) != 0)
          {
          void const * const to{jump_target(now)};
          log_line("%s: the system d3d11's %s starts differently now%s%S", when, w.name, to ? ", a jump to " : " (no jump read)",
                   to ? module_of(to).c_str() : L"");
          take_over(std::string{when} + ": the system d3d11's " + w.name + " was hooked inline" +
                    (to ? " (a jump to " + narrow(module_of(to)) + ")" : std::string{}));
          std::memcpy(w.prologue, now, sizeof now);
          }
        }
      }
    }

  ///\brief the first bytes of an exported function as the dll's file on disk has them; false when they cannot be
  /// compared (not found, forwarded, or a relocation inside them)
  auto bytes_on_disk(std::wstring const & path, char const * function, std::uint8_t (&out)[16], std::wstring & why) -> bool
    {
    std::FILE * f{_wfopen(path.c_str(), L"rb")};
    if(not f)
      {
      why = L"the file cannot be read";
      return false;
      }
    std::vector<std::uint8_t> file;
    std::uint8_t buf[65536];
    for(std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
      file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    auto const at = [&](std::size_t off, std::size_t len) { return off + len <= file.size(); };
    if(not at(0, sizeof(IMAGE_DOS_HEADER)))
      return why = L"not a dll", false;
    auto const dos{reinterpret_cast<IMAGE_DOS_HEADER const *>(file.data())};
    if(not at(static_cast<std::size_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64)))
      return why = L"not a dll", false;
    auto const nt{reinterpret_cast<IMAGE_NT_HEADERS64 const *>(file.data() + dos->e_lfanew)};
    auto const sections{IMAGE_FIRST_SECTION(nt)};
    auto const to_offset = [&](DWORD rva) -> std::size_t
      {
      for(WORD i{}; i != nt->FileHeader.NumberOfSections; ++i)
        if(rva >= sections[i].VirtualAddress and rva < sections[i].VirtualAddress + sections[i].SizeOfRawData)
          return rva - sections[i].VirtualAddress + sections[i].PointerToRawData;
      return SIZE_MAX;
      };
    IMAGE_DATA_DIRECTORY const & ed{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]};
    std::size_t const eo{to_offset(ed.VirtualAddress)};
    if(eo == SIZE_MAX or not at(eo, sizeof(IMAGE_EXPORT_DIRECTORY)))
      return why = L"no export table", false;
    auto const exp{reinterpret_cast<IMAGE_EXPORT_DIRECTORY const *>(file.data() + eo)};
    std::size_t const names{to_offset(exp->AddressOfNames)}, ords{to_offset(exp->AddressOfNameOrdinals)}, funcs{to_offset(exp->AddressOfFunctions)};
    DWORD rva{};
    for(DWORD i{}; i != exp->NumberOfNames and names != SIZE_MAX and ords != SIZE_MAX and funcs != SIZE_MAX; ++i)
      {
      DWORD name_rva;
      std::memcpy(&name_rva, file.data() + names + i * 4, 4);
      std::size_t const no{to_offset(name_rva)};
      if(no == SIZE_MAX or std::strcmp(reinterpret_cast<char const *>(file.data() + no), function) != 0)
        continue;
      WORD ord;
      std::memcpy(&ord, file.data() + ords + i * 2, 2);
      std::memcpy(&rva, file.data() + funcs + ord * 4, 4);
      break;
      }
    if(rva == 0)
      return why = L"not exported by name", false;
    if(rva >= ed.VirtualAddress and rva < ed.VirtualAddress + ed.Size)
      return why = L"forwarded", false;
    // a relocation inside the first bytes would differ in memory without anyone's hook
    IMAGE_DATA_DIRECTORY const & rd{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]};
    for(std::size_t ro{to_offset(rd.VirtualAddress)}, end{ro + rd.Size}; ro != SIZE_MAX and ro + 8 <= end and at(ro, 8);)
      {
      auto const block{reinterpret_cast<IMAGE_BASE_RELOCATION const *>(file.data() + ro)};
      if(block->SizeOfBlock < 8)
        break;
      for(std::size_t e{8}; e + 2 <= block->SizeOfBlock and at(ro + e, 2); e += 2)
        {
        WORD entry;
        std::memcpy(&entry, file.data() + ro + e, 2);
        DWORD const where{block->VirtualAddress + (entry & 0xFFFu)};
        if((entry >> 12) != IMAGE_REL_BASED_ABSOLUTE and where + 8 > rva and where < rva + 16)
          return why = L"a relocation in its first bytes", false;
        }
      ro += block->SizeOfBlock;
      }
    std::size_t const fo{to_offset(rva)};
    if(fo == SIZE_MAX or not at(fo, sizeof out))
      return why = L"outside the file", false;
    std::memcpy(out, file.data() + fo, sizeof out);
    return true;
    }

  ///\brief the modules in the process when edloader starts: whoever was there may have changed d3d11 already
  auto log_modules() noexcept -> void
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
    log_line("modules before the list: %S", all.c_str());
    }

  ///\brief the starting point of the checks: where the game's imports lead and the system copy's first bytes,
  /// against the system copy's file on disk (a hook made before edloader started shows there)
  auto start_takers() noexcept -> void
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
          log_line("kernelbase's %s cannot be compared with its file (%S)", w.name, why.c_str());
        else if(std::memcmp(disk, w.prologue, sizeof disk) != 0)
          {
          void const * const to{jump_target(w.prologue)};
          take_over(std::string{"before edloader started: kernelbase's "} + w.name + " differs from its file" +
                    (to ? " (a jump to " + narrow(module_of(to)) + ")" : std::string{}));
          }
        else
          log_line("kernelbase's %s is as its file has it", w.name);
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
          log_line("the system d3d11's %s cannot be compared with its file (%S)", w.name, why.c_str());
        else if(std::memcmp(disk, w.prologue, sizeof disk) != 0)
          {
          void const * const to{jump_target(w.prologue)};
          take_over(std::string{"before edloader started: the system d3d11's "} + w.name + " differs from its file" +
                    (to ? " (a jump to " + narrow(module_of(to)) + ")" : std::string{}));
          }
        else
          log_line("the system d3d11's %s is as its file has it", w.name);
        }
      if(w.slot)
        {
        w.last = *w.slot;
        log_line("the game imports %s from d3d11.dll; it leads to %S", w.name, module_of(w.last).c_str());
        }
      else
        log_line("the game does not import %s by name from d3d11.dll", w.name);
      }
    }

  ///\brief edloader.txt: one dll per line, in call order from the game; `+name` = load only (not a d3d11 proxy);
  /// `#` or `;` starts a comment
  auto load_list() -> void
    {
    std::wstring const list_path{root_dir + L"\\edloader.txt"};
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
      {
      std::string const label{"after loading " + std::string{text}};
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

  auto environment(wchar_t const * name) -> std::wstring
    {
    wchar_t value[MAX_PATH]{};
    DWORD const n{GetEnvironmentVariableW(name, value, MAX_PATH)};
    return n != 0 and n < MAX_PATH ? std::wstring{value, n} : std::wstring{};
    }

  auto make_dirs(std::wstring const & root) -> void
    {
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW((root + L"\\config").c_str(), nullptr);
    CreateDirectoryW((root + L"\\logs").c_str(), nullptr);
    CreateDirectoryW((root + L"\\plugins").c_str(), nullptr);
    }

  ///\brief the one place: %USERPROFILE%\edloader, inside the wine prefix, which a verification of the game's files
  /// never touches (it removes everything in the game's folder that is not the game's), so after one only
  /// d3d11.dll has to be put back. EDLOADER_DIR replaces it for the lab; then the default log says so in one line,
  /// so the place everyone looks first still shows where this run's files are.
  auto find_root_dir() -> std::wstring
    {
    std::wstring const profile{environment(L"USERPROFILE")};
    std::wstring const standard{profile.empty() ? module_dir : profile + L"\\edloader"};
    std::wstring const lab{environment(L"EDLOADER_DIR")};
    if(lab.empty())
      return standard;
    make_dirs(standard);
    if(std::FILE * f{_wfopen((standard + L"\\logs\\edloader.log").c_str(), L"ab")})
      {
      SYSTEMTIME t;
      GetSystemTime(&t);
      std::fprintf(
        f, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ EDLOADER_DIR=%S: this run's list, config and logs are there\n",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, lab.c_str()
      );
      std::fclose(f);
      }
    return lab;
    }

  INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

  BOOL CALLBACK init_callback(PINIT_ONCE, PVOID, PVOID *)
    {
    root_dir = find_root_dir();
    make_dirs(root_dir);
    log_file = _wfopen((root_dir + L"\\logs\\edloader.log").c_str(), L"ab");
    log_line("edloader %s", EDLOADER_VERSION);
    log_line("root: %S", root_dir.c_str());
    // the plugins' own files: our plugins read these, without edloader they fall back to beside their dll
    SetEnvironmentVariableW(L"EDLOADER_CONFIG_DIR", (root_dir + L"\\config").c_str());
    SetEnvironmentVariableW(L"EDLOADER_LOG_DIR", (root_dir + L"\\logs").c_str());
    start_takers();
    load_list();
    if(takeover and accept_takeover and stopped)
      {
      stopped = false;  // found before the list's accept_takeover was read
      log_line("TAKEOVER accepted (accept_takeover = true): the list is passed on, at your own risk");
      }
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
    {
    entered = 0;
    check_takers("at D3D11CreateDevice");
    check_loader("at D3D11CreateDevice");
    }
  element_t const & target{route(_ReturnAddress(), "D3D11CreateDevice")};
  if(not target.create)
    return E_FAIL;
  HRESULT const hr{target.create(adapter, driver_type, software, flags, levels, level_count, sdk, device, level, context)};
  if(depth == 1)
    {
    log_line("D3D11CreateDevice: hr 0x%08lX through %zu element(s), device %s", static_cast<unsigned long>(hr), chain.size(),
             device == nullptr ? "not asked" : SUCCEEDED(hr) and *device ? "made" : "asked, none made");
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
    check_takers("at D3D11CreateDeviceAndSwapChain");
    check_loader("at D3D11CreateDeviceAndSwapChain");
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
