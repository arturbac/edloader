// edloader - what edloader's parts share: the chain's elements, the paths, the log.
#pragma once

#include <windows.h>

#include <d3d11.h>

#include <cstdio>
#include <string>
#include <vector>

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

  auto log_line(char const * fmt, ...) noexcept -> void;
  auto make_dirs(std::wstring const & root) -> void;
  ///\brief the one place: %USERPROFILE%\edloader, inside the wine prefix, which a verification of the game's files
  /// never touches (it removes everything in the game's folder that is not the game's), so after one only
  /// d3d11.dll has to be put back. EDLOADER_DIR replaces it for the lab; then the default log says so in one line,
  /// so the place everyone looks first still shows where this run's files are.
  auto find_root_dir() -> std::wstring;
  }  // namespace edloader
