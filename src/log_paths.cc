// edloader - what edloader's parts share: the chain's elements, the paths, the log.
#include "runtime.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace edloader
  {
  HMODULE self_module{};
  HMODULE system_module{};
  std::wstring module_dir;
  std::wstring root_dir;  // holds edloader.txt and the folders plugins (the list's relative names), config, logs
  element_t system_element;
  std::vector<element_t> chain;
  std::FILE * log_file{};
  SRWLOCK log_lock = SRWLOCK_INIT;  ///< no std::mutex: its lock may throw, and log_line runs where nothing may

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
    int const room{static_cast<int>(sizeof buf) - n - 1};  // one byte kept for the line's end
    int const m{std::vsnprintf(buf + n, static_cast<std::size_t>(room), fmt, args)};
    va_end(args);
    // a cut line ends where vsnprintf put its terminator, which the line's end replaces: no NUL in the log
    n += m < 0 ? 0 : std::min(m, room - 1);
    buf[n++] = '\n';
    AcquireSRWLockExclusive(&log_lock);
    std::fwrite(buf, 1, static_cast<std::size_t>(n), log_file);
    std::fflush(log_file);
    ReleaseSRWLockExclusive(&log_lock);
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
  }  // namespace edloader
