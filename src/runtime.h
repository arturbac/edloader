// edloader - what edloader's parts share: the chain's elements, the paths, the log.
#pragma once

#include <windows.h>

#include <d3d11.h>

#include <cstdio>
#include <string>
#include <vector>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>
#include <algorithm>

namespace edloader
  {
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


  extern HMODULE self_module;
  extern HMODULE system_module;
  extern std::wstring module_dir;
  extern std::wstring root_dir;
  extern element_t system_element;
  extern std::vector<element_t> chain;
  extern std::FILE * log_file;

  ///\brief an SRW lock held exclusively for a scope: let go on every way out of it
  class exclusive_lock_t
    {
    SRWLOCK & lock_;

  public:
    explicit exclusive_lock_t(SRWLOCK & lock) noexcept : lock_{lock} { AcquireSRWLockExclusive(&lock_); }

    ~exclusive_lock_t() { ReleaseSRWLockExclusive(&lock_); }

    exclusive_lock_t(exclusive_lock_t const &) = delete;
    auto operator=(exclusive_lock_t const &) -> exclusive_lock_t & = delete;
    };

  ///\brief an SRW lock held shared for a scope
  class shared_lock_t
    {
    SRWLOCK & lock_;

  public:
    explicit shared_lock_t(SRWLOCK & lock) noexcept : lock_{lock} { AcquireSRWLockShared(&lock_); }

    ~shared_lock_t() { ReleaseSRWLockShared(&lock_); }

    shared_lock_t(shared_lock_t const &) = delete;
    auto operator=(shared_lock_t const &) -> shared_lock_t & = delete;
    };

  ///\brief a wide string in a log line, written as UTF-8 (std::format takes no wide string into a narrow line)
  struct wide_t
    {
    std::wstring_view text;
    };

  [[nodiscard]]
  inline auto wide(std::wstring_view text) noexcept -> wide_t
    {
    return {text};
    }

  [[nodiscard]]
  inline auto wide(wchar_t const * text) noexcept -> wide_t
    {
    return {text ? std::wstring_view{text} : std::wstring_view{L"(null)"}};
    }

  ///\brief a value for a hexadecimal field: a signed one (HRESULT, LONG) as its bits, as printf's %X showed it
  template<typename value_t>
  [[nodiscard]]
  constexpr auto as_unsigned(value_t value) noexcept
    {
    if constexpr(std::is_enum_v<value_t>)
      return as_unsigned(std::to_underlying(value));
    else if constexpr(std::is_integral_v<value_t> and std::is_signed_v<value_t>)
      return static_cast<std::make_unsigned_t<value_t>>(value);
    else
      return value;
    }

  ///\brief one line, time-stamped (UTC), cut to 2 KB, written at once under the log's lock; a line that cannot be made
  /// is logged as such (never thrown)
  auto log_vline(std::string_view fmt, std::format_args args) noexcept -> void;

  ///\brief log_vline with the format checked against its arguments when compiled
  template<typename... args_t>
  auto log_line(std::format_string<args_t...> fmt, args_t &&... args) noexcept -> void
    {
    log_vline(fmt.get(), std::make_format_args(args...));
    }
  auto make_dirs(std::wstring const & root) -> void;
  ///\brief the one place: %USERPROFILE%\edloader, inside the wine prefix, which a verification of the game's files
  /// never touches (it removes everything in the game's folder that is not the game's), so after one only
  /// d3d11.dll has to be put back. EDLOADER_DIR replaces it for the lab; then the default log says so in one line,
  /// so the place everyone looks first still shows where this run's files are.
  auto find_root_dir() -> std::wstring;
  }  // namespace edloader

///\brief a wide string (edloader::wide) in a log line as UTF-8, by pieces through a buffer on the stack
template<>
struct std::formatter<edloader::wide_t, char> : std::formatter<std::string_view, char>
  {
  auto format(edloader::wide_t const & w, std::format_context & ctx) const -> std::format_context::iterator
    {
    auto out{ctx.out()};
    std::wstring_view rest{w.text};
    char buf[512];
    while(not rest.empty())
      {
      std::size_t len{std::min<std::size_t>(rest.size(), 120)};
      if(len < rest.size() and rest[len - 1] >= 0xD800 and rest[len - 1] <= 0xDBFF)
        --len;  // a surrogate pair stays whole
      int const n{WideCharToMultiByte(CP_UTF8, 0, rest.data(), static_cast<int>(len), buf, static_cast<int>(sizeof buf), nullptr, nullptr)};
      for(int i{}; i < n; ++i)
        *out++ = buf[i];
      rest.remove_prefix(len);
      }
    return out;
    }
  };
