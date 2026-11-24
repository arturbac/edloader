// edloader - the chain: the system copy in DllMain, the list (edloader.txt) loaded on the first export call.
#include "runtime.h"
#include "chain.h"
#include "pe_read.h"
#include "takeovers.h"
#include "edplugin_common_commit.h"
#include "d3d11_proxy.h"

#include <optional>
#include <string>
#include <string_view>

#include <stralgo/stralgo.h>

namespace edloader
  {
  using namespace std::string_view_literals;

  bool misconfigured{};

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
    for(std::size_t i{}; i != d3d11_proxy_count; ++i)
      if(not d3d11_proxy_real_procs[i])
        d3d11_proxy_real_procs[i] = reinterpret_cast<void *>(&d3d11_proxy_unresolved);
    }

  auto loader_paths() -> void
    {
    module_dir = edplugin_common::directory_of(edplugin_common::module_path(self_module));  // a long path too
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    system_element.name = stralgo::stl::merge(std::wstring_view{sys}, L"\\d3d11.dll"sv);
    system_module = LoadLibraryW(system_element.name.c_str());
    system_element.module = system_module;
    for(std::size_t i{}; i != d3d11_proxy_count; ++i)
      {
      void * p{system_module ? reinterpret_cast<void *>(GetProcAddress(system_module, d3d11_proxy_names[i])) : nullptr};
      d3d11_proxy_real_procs[i] = p ? p : reinterpret_cast<void *>(&d3d11_proxy_unresolved);
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


  ///\brief one line of the list that names a dll: its marks and where its name leads
  struct list_line_t
    {
    std::size_t number{};
    std::string text;   // the name as written, without its marks
    std::wstring path;  // as LoadLibrary gets it
    std::wstring full;  // GetFullPathName of it: one dll on two lines of the same spelling
    bool plain{};       // +: loaded, not chained
    bool first{};       // ^: first in the chain
    bool last{};        // !: last in the chain, before the system copy
    bool self{};        // edloader itself
    };

  ///\brief the line without its comment and the spaces around
  auto content(std::string_view line) -> std::string_view
    {
    std::string_view text{trim(line)};
    if(auto const hash{text.find_first_of("#;")}; hash != std::string_view::npos)
      text = trim(text.substr(0, hash));
    return text;
    }

  ///\brief the marks before a name (+, ^, !, ^!, written together; spaces after them are let be) into l, the name after
  /// them; null when they are fine, otherwise what is wrong with them
  auto read_marks(std::string_view text, list_line_t & l) -> char const *
    {
    std::size_t const end{std::min(text.find_first_not_of("+^!"), text.size())};
    std::string_view const marks{text.substr(0, end)};
    text = trim(text.substr(end));
    l.text = text;
    if(not text.empty() and "+^!"sv.find(text.front()) != std::string_view::npos)
      return "marks other than ^ (first), ! (last) or ^! (both), written together before the name";
    if(marks == "+")
      l.plain = true;
    else if(marks == "^")
      l.first = true;
    else if(marks == "!")
      l.last = true;
    else if(marks == "^!")
      l.first = l.last = true;
    else if(marks.find('+') != std::string_view::npos and marks.find_first_of("^!") != std::string_view::npos)
      return "'+' (loaded, not chained) together with '^' or '!' (a place in the chain)";
    else if(not marks.empty())
      return "marks other than ^ (first), ! (last) or ^! (both)";
    if(text.empty() and (l.first or l.last))
      return "a mark with no dll after it";
    return nullptr;
    }

  auto mark_text(list_line_t const & l) -> std::string_view
    {
    return l.first and l.last ? "^!"sv : l.first ? "^"sv : l.last ? "!"sv : ""sv;
    }

  ///\brief the list's own errors, found before any dll of it is loaded: a line each; true when there was one
  auto misconfigured_list(std::vector<list_line_t> const & lines, std::size_t bad_marks) -> bool
    {
    std::size_t errors{bad_marks};
    for(bool const at_first: {true, false})
      {
      std::string who;
      std::size_t count{};
      for(list_line_t const & l: lines)
        if(not l.self and (at_first ? l.first : l.last))
          who += std::format("{}line {} {}", count++ == 0 ? "" : ", ", l.number, l.text);
      if(count > 1)
        {
        ++errors;
        log_stop_line("MISCONFIGURATION: {} dlls ask for the {} place ({}), which one dll can have: {}", count,
                 at_first ? "first" : "last", at_first ? "^" : "!", who);
        }
      }
    for(std::size_t i{}; i != lines.size(); ++i)
      for(std::size_t j{i + 1}; j != lines.size(); ++j)
        {
        list_line_t const & a{lines[i]};
        list_line_t const & b{lines[j]};
        if(a.self or b.self or _wcsicmp(a.full.c_str(), b.full.c_str()) != 0)
          continue;
        if(a.plain or b.plain)
          log_line("lines {} and {} load the same dll ({}): loaded once, as before", a.number, b.number, b.text);
        else
          {
          ++errors;
          log_stop_line("MISCONFIGURATION: lines {} and {} name the same dll ({}): a dll stands in the chain once "
                   "(^! puts one dll at both ends)", a.number, b.number, b.text);
          }
        }
    return errors != 0;
    }

  ///\brief edloader stops for its list's own errors: the takeover's stop, which accept_takeover does not lift
  auto stop_misconfigured(char const * loaded) -> void
    {
    misconfigured = true;
    stopped = true;
    chain.clear();
    log_stop_line("MISCONFIGURATION: edloader stops: every call goes to the system d3d11.dll and no dll of the list is {}. "
             "Fix the lines above in edloader.txt; a mod's own documentation says if it needs ^ (first), ! (last) or "
             "^! (both)", loaded);
    log_line("chain: game -> 0 proxy(ies) -> system d3d11.dll");
    }

  auto load_list() -> void
    {
    std::wstring const list_path{stralgo::stl::merge(root_dir, L"\\edloader.txt"sv)};
    std::FILE * f{_wfopen(list_path.c_str(), L"rb")};
    if(not f)
      {
      log_line("no {}: everything goes to the system d3d11.dll", wide(list_path));
      return;
      }
    std::vector<std::string> text_lines;
    char line[1024];
    bool whole{true};  // the last piece ended its line: a line longer than the buffer comes in pieces
    while(std::fgets(line, sizeof line, f))
      {
      if(whole)
        text_lines.emplace_back(line);
      else
        text_lines.back() += line;
      whole = text_lines.back().ends_with('\n');
      }
    std::fclose(f);
    if(not text_lines.empty() and text_lines.front().starts_with("\xEF\xBB\xBF"))
      text_lines.front().erase(0, 3);  // a byte order mark, as Notepad writes one
    // the settings first, wherever they stand in the list: they decide what a takeover while loading does
    for(std::string const & l: text_lines)
      {
      std::string_view const text{content(l)};
      auto const eq{text.find('=')};
      if(eq == std::string_view::npos)
        continue;
      std::string_view const key{trim(text.substr(0, eq))}, value{trim(text.substr(eq + 1))};
      if(key == "accept_takeover")
        {
        accept_takeover = value == "true" or value == "1" or value == "yes";
        log_line("accept_takeover = {}", accept_takeover ? "true: a takeover is logged and the list still passed on" : "false");
        }
      else
        log_line("unknown setting '{}' ignored", key);
      }
    // a takeover before the list told to add accept_takeover, which the list has: no longer the window's to say
    if(accept_takeover)
      forget_stop_lines();
    // the dlls' lines and their marks, all checked before any dll is loaded: a dll runs its own code when loaded
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(self_module, self, MAX_PATH);
    std::vector<list_line_t> lines;
    std::size_t bad_marks{};
    for(std::size_t n{}; n != text_lines.size(); ++n)
      {
      std::string_view const text{content(text_lines[n])};
      if(text.empty() or text.find('=') != std::string_view::npos)
        continue;  // a setting, read above
      list_line_t l{};
      l.number = n + 1;
      if(char const * const wrong{read_marks(text, l)})
        {
        ++bad_marks;
        log_stop_line("MISCONFIGURATION: line {} '{}': {}", l.number, text, wrong);
        continue;
        }
      l.path = full_path(widen(l.text));
      wchar_t want[MAX_PATH]{};
      DWORD const got{GetFullPathNameW(l.path.c_str(), MAX_PATH, want, nullptr)};
      l.full = got != 0 and got < MAX_PATH ? std::wstring{want} : l.path;
      l.self = _wcsicmp(self, l.full.c_str()) == 0;
      lines.push_back(std::move(l));
      }
    if(misconfigured_list(lines, bad_marks))
      {
      stop_misconfigured("loaded or called");
      return;
      }
    std::vector<element_t> middle;
    std::optional<element_t> first, last;
    std::vector<HMODULE> chained;
    for(list_line_t const & l: lines)
      {
      std::string_view const mark{mark_text(l)};
      if(l.self)
        {
        log_line("{} is edloader itself; skipped{}", l.text, mark.empty() ? "" : ", its mark not counted");
        continue;
        }
      // a mark whose dll gives no element: that place stays as the rest of the list makes it
      auto const place_empty{[&](char const * why)
                             {
                               if(not mark.empty())
                                 log_line("line {} {}{}: {}; the place it asks for ({}) is left to the rest of the list",
                                          l.number, mark, l.text, why, mark);
                             }};
      HMODULE const module{LoadLibraryW(l.path.c_str())};
      if(not module)
        {
        log_line("{}: cannot load (error {}); skipped", wide(l.path), GetLastError());
        place_empty("cannot load");
        continue;
        }
      if(not l.plain and std::ranges::find(chained, module) != chained.end())
        {
        // one dll by two spellings of its path (another drive, a link): the same module twice in the chain
        log_stop_line("MISCONFIGURATION: line {} {} is a dll already in the chain, by another path", l.number, l.text);
        stop_misconfigured(stralgo::stl::merge("called (the dlls before line "sv,
                                               std::to_string(l.number), " are loaded)"sv).c_str());
        return;
        }
      list_files.push_back(module_name_of(reinterpret_cast<void const *>(module)));
      {
      std::string const label{stralgo::stl::merge("after loading "sv, std::string_view{l.text})};
      guard_own_exports(label.c_str(), l.text.c_str());
      check_takers(label.c_str());
      check_loader(label.c_str());
      }
      if(l.plain)
        {
        log_line("{}: loaded (plugin, not chained)", l.text);
        continue;
        }
      element_t e{widen(l.text), module, reinterpret_cast<create_device_fn>(GetProcAddress(module, "D3D11CreateDevice")),
                  reinterpret_cast<create_device_swap_fn>(GetProcAddress(module, "D3D11CreateDeviceAndSwapChain"))};
      if(not e.create)
        {
        log_line("{}: exports no D3D11CreateDevice, so not a d3d11 proxy; kept loaded, not chained", l.text);
        place_empty("not a d3d11 proxy");
        continue;
        }
      chained.push_back(module);
      if(l.first)
        {
        first = e;
        first->place = " (first, ^)";
        }
      if(l.last)
        {
        last = e;
        last->place = " (last, !)";
        }
      if(not l.first and not l.last)
        middle.push_back(std::move(e));
      }
    if(first)
      chain.push_back(*first);
    chain.insert(chain.end(), middle.begin(), middle.end());
    if(last)
      chain.push_back(*last);
    for(std::size_t i{}; i != chain.size(); ++i)
      log_line("chain[{}] = {}{}{}", i, wide(chain[i].name), chain[i].place,
               chain[i].create_swap ? "" : " (no D3D11CreateDeviceAndSwapChain)");
    log_line("chain: game -> {} proxy(ies) -> system d3d11.dll", chain.size());
    }


  // ---- edloader's own work in the game's process: a fault or an exception there is logged and that step skipped ----
  // Reading other modules' headers, import tables and memory (a module unloaded meanwhile, a page gone) and the
  // strings of the checks (out of memory) must never end the game. Each step runs under edplugin_common's guard
  // (run_guarded): hardware faults and C++ exceptions alike, logged with their kind (a fault with its place); no step
  // is turned off, each runs again at its next time. "the list" loads the list's dlls (their DllMain) under it too: the
  // one place a module not edloader's runs under its guard, so a fault that reaches it from a dll's DllMain skips the
  // rest of the list's loading instead of ending the game; whether the system's loader lock LoadLibrary held is let go
  // then is not checked (held, it would hang the game at its next load of a module or start of a thread).
  auto guarded(char const * what, void (*step)(char const *), char const * when) noexcept -> bool
    {
    edplugin_common::fault_t fault;
    if(edplugin_common::run_guarded([&] { step(when); }, fault))
      return true;
    char text[320];
    log_line("{}: failed ({}); skipped", what, edplugin_common::fault_line(fault, text));
    return false;
    }

  // the checks at a device change edloader's records of the last state: one thread at a time
  SRWLOCK checks_lock = SRWLOCK_INIT;

  auto device_checks(char const * when) noexcept -> void
    {
    exclusive_lock_t const hold{checks_lock};
    guarded("own exports", [](char const * w) { guard_own_exports(w, "a dll"); }, when);
    guarded("takers", [](char const * w) { check_takers(w); }, when);
    guarded("loader", [](char const * w) { check_loader(w); }, when);
    guarded("loader imports", [](char const * w) { sweep_loader_imports(w); }, when);
    }

  auto init_steps(char const *) -> void
    {
    root_dir = find_root_dir();
    make_dirs(root_dir);
    log_open(stralgo::stl::merge(root_dir, L"\\logs"sv), L"edloader");  // the last session's log set aside in logs\edloader
    log_line("edloader {}", EDLOADER_VERSION);
    log_line("edplugin_common {}", EDPLUGIN_COMMON_COMMIT);
    log_line("root: {}", wide(root_dir));
    // the plugins' own files: plugins made for edloader read these, without edloader they fall back to beside their dll
    SetEnvironmentVariableW(edplugin_common::edloader_config_dir_variable, stralgo::stl::merge(root_dir, L"\\config"sv).c_str());
    SetEnvironmentVariableW(edplugin_common::edloader_log_dir_variable, stralgo::stl::merge(root_dir, L"\\logs"sv).c_str());
    guarded("start", [](char const *) { start_takers(); }, nullptr);
    guarded("own exports kept", [](char const *) { keep_own_exports(); }, nullptr);
    guarded("the list", [](char const *) { load_list(); }, nullptr);
    guarded("loader imports", [](char const * w) { sweep_loader_imports(w); }, "after the list");
    if(takeover and accept_takeover and stopped and not misconfigured)
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
