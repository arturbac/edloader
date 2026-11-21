// edloader - what edloader's parts share: the chain's elements, the paths, the log.
#include "runtime.h"

#include <algorithm>
#include <cstdio>
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
  std::FILE * log_file{};
  SRWLOCK log_lock = SRWLOCK_INIT;  ///< no std::mutex: its lock may throw, and log_line runs where nothing may

  namespace
    {
    ///\brief the room a log line is made in, and an output iterator that stops at its end
    struct cut_buffer_t
      {
      char * at;
      char * end;
      };

    struct cut_out_t
      {
      using difference_type = std::ptrdiff_t;
      cut_buffer_t * b;

      auto operator*() noexcept -> cut_out_t & { return *this; }

      auto operator=(char c) noexcept -> cut_out_t &
        {
        if(b->at != b->end)
          *b->at++ = c;
        return *this;
        }

      auto operator++() noexcept -> cut_out_t & { return *this; }

      auto operator++(int) noexcept -> cut_out_t { return *this; }
      };
    }  // namespace

  namespace
    {
    // the lines of what stopped edloader, for the window; static, so keeping one never allocates
    stop_lines_t stop_lines{};
    }  // namespace

  auto log_vline(std::string_view fmt, std::format_args args, bool stop) noexcept -> void
    {
    if(not log_file and not stop)
      return;
    SYSTEMTIME t;
    GetSystemTime(&t);
    char buf[2048];
    cut_buffer_t room{buf, buf + sizeof buf - 1};  // one byte kept for the line's end
    char * text{buf};  // the line after its time
    try
      {
      std::format_to(cut_out_t{&room}, "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                     t.wMilliseconds);
      text = room.at;
      std::vformat_to(cut_out_t{&room}, fmt, args);
      }
    catch(...)
      {
      constexpr std::string_view failed{" (log: the rest of this line could not be formatted)"};
      for(char const c: failed)
        if(room.at != room.end)
          *room.at++ = c;
      }
    *room.at++ = '\n';
    exclusive_lock_t const hold{log_lock};
    // under the lock only copying: a fault there would leave the lock taken (no unwinding of a hardware fault)
    if(stop)
      {
      if(stop_lines.count != stop_line_count)
        {
        std::size_t const n{static_cast<std::size_t>(room.at - 1 - text)};  // without the line's end
        std::memcpy(stop_lines.text[stop_lines.count], text, n);
        stop_lines.text[stop_lines.count][n] = '\0';
        ++stop_lines.count;
        }
      else
        stop_lines.more = true;
      }
    if(not log_file)
      return;
    std::fwrite(buf, 1, static_cast<std::size_t>(room.at - buf), log_file);
    std::fflush(log_file);
    }

  auto copy_stop_lines(stop_lines_t & out) noexcept -> void
    {
    shared_lock_t const hold{log_lock};
    std::memcpy(&out, &stop_lines, sizeof out);
    }

  auto forget_stop_lines() noexcept -> void
    {
    exclusive_lock_t const hold{log_lock};
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
    if(std::FILE * f{_wfopen(stralgo::stl::merge(standard, L"\\logs\\edloader.log"sv).c_str(), L"ab")})
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
