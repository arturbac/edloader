// edloader - what edloader's parts share: the chain's elements, the paths, the log.
#include "runtime.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <string_view>

#include <stralgo/stralgo.h>

namespace edloader
  {
  using namespace std::string_view_literals;

  HMODULE self_module{};
  HMODULE system_module{};
  std::wstring module_dir;
  std::wstring root_dir;  // holds edloader.txt and the folders plugins (the list's relative names), config, logs
  element_t system_element;
  std::vector<element_t> chain;

  namespace
    {
    // the lines of what stopped edloader, for the window; static, so keeping one never allocates
    stop_lines_t stop_lines{};
    SRWLOCK stop_lock = SRWLOCK_INIT;  ///< no std::mutex: its lock may throw, and a line is kept where nothing may
    }  // namespace

  auto keep_stop_line(std::string_view text) noexcept -> void
    {
    exclusive_lock_t const hold{stop_lock};
    // under the lock only copying: a fault there would leave the lock taken (no unwinding of a hardware fault)
    if(stop_lines.count != stop_line_count)
      {
      std::size_t const n{std::min(text.size(), stop_line_size - 1)};
      std::memcpy(stop_lines.text[stop_lines.count], text.data(), n);
      stop_lines.text[stop_lines.count][n] = '\0';
      ++stop_lines.count;
      }
    else
      stop_lines.more = true;
    }

  auto copy_stop_lines(stop_lines_t & out) noexcept -> void
    {
    shared_lock_t const hold{stop_lock};
    std::memcpy(&out, &stop_lines, sizeof out);
    }

  auto forget_stop_lines() noexcept -> void
    {
    exclusive_lock_t const hold{stop_lock};
    stop_lines.count = 0;
    stop_lines.more = false;
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
    CreateDirectoryW(stralgo::stl::merge(root, L"\\config"sv).c_str(), nullptr);
    CreateDirectoryW(stralgo::stl::merge(root, L"\\logs"sv).c_str(), nullptr);
    CreateDirectoryW(stralgo::stl::merge(root, L"\\plugins"sv).c_str(), nullptr);
    }

  ///\brief the one place: %USERPROFILE%\edloader, inside the wine prefix, which a verification of the game's files
  /// never touches (it removes everything in the game's folder that is not the game's), so after one only
  /// d3d11.dll has to be put back. EDLOADER_DIR replaces it for the lab; then the default log says so in one line,
  /// so the place everyone looks first still shows where this run's files are.
  auto find_root_dir() -> std::wstring
    {
    std::wstring const profile{environment(L"USERPROFILE")};
    std::wstring const standard{profile.empty() ? module_dir : stralgo::stl::merge(profile, L"\\edloader"sv)};
    std::wstring const lab{environment(L"EDLOADER_DIR")};
    if(lab.empty())
      return standard;
    make_dirs(standard);
    log_line_to(stralgo::stl::merge(standard, L"\\logs\\edloader.log"sv), "EDLOADER_DIR={}: this run's list, config and logs are there",
                wide(lab));
    return lab;
    }
  }  // namespace edloader
