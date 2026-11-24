// edloader - what edloader's parts share: the chain's elements, the paths, the log.
#pragma once

#include <windows.h>

#include <d3d11.h>

#include <edplugin_common/core/lock.h>
#include <edplugin_common/log/log.h>
#include <edplugin_common/text/format.h>

#include <cstdio>
#include <string>
#include <vector>
#include <format>
#include <string_view>

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
    char const * place{""};  // in the log: " (first, ^)" or " (last, !)" for a dll its mark put there
    };


  extern HMODULE self_module;
  extern HMODULE system_module;
  extern std::wstring module_dir;
  extern std::wstring root_dir;
  extern element_t system_element;
  extern std::vector<element_t> chain;

  using edplugin_common::as_unsigned;
  using edplugin_common::exclusive_lock_t;
  using edplugin_common::shared_lock_t;
  using edplugin_common::wide;
  using edplugin_common::wide_t;
  // the log is edplugin_common's: logs\edloader.log, the last session's set aside in the folder edloader beside it
  using edplugin_common::log_is_open;
  using edplugin_common::log_line;
  using edplugin_common::log_line_to;
  using edplugin_common::log_open;
  using edplugin_common::log_vline;

  constexpr std::size_t stop_line_count{32};
  constexpr std::size_t stop_line_size{2048};

  ///\brief the lines of what stopped edloader: copied under their own lock (nothing else done there); more: lines that
  /// did not fit
  struct stop_lines_t
    {
    char text[stop_line_count][stop_line_size];
    std::size_t count;
    bool more;
    };

  ///\brief the log's tap of a line of what stopped edloader: its text (without its time) kept for the window that
  /// tells the player, also when the log could not be opened
  auto keep_stop_line(std::string_view text) noexcept -> void;
  auto copy_stop_lines(stop_lines_t & out) noexcept -> void;
  ///\brief the lines kept so far forgotten: a takeover's advice before the list said accept_takeover = true
  auto forget_stop_lines() noexcept -> void;

  ///\brief log_vline of a line of what stopped edloader
  template<typename... args_t>
  auto log_stop_line(std::format_string<args_t...> fmt, args_t &&... args) noexcept -> void
    {
    log_vline(fmt.get(), std::make_format_args(args...), keep_stop_line);
    }
  auto make_dirs(std::wstring const & root) -> void;
  ///\brief the one place: %USERPROFILE%\edloader, inside the wine prefix, which a verification of the game's files
  /// never touches (it removes everything in the game's folder that is not the game's), so after one only
  /// d3d11.dll has to be put back. EDLOADER_DIR replaces it for the lab; then the default log says so in one line,
  /// so the place everyone looks first still shows where this run's files are.
  auto find_root_dir() -> std::wstring;
  }  // namespace edloader
