// A stand-in d3d11 proxy for the chain test: notes its name in order.txt, then asks for "d3d11.dll" BY NAME
// (as 3Dmigoto does) and calls its D3D11CreateDevice - which is edloader, which must pass it on. Built with
// FAKE_SWAP it exports D3D11CreateDeviceAndSwapChain too, passed on the same way. Its DllMain notes its name in
// loaded.txt: a list edloader refuses loads none of its dlls.
#include <windows.h>

#include <d3d11.h>

#include <cstdio>

static auto note() -> void
  {
  if(std::FILE * order{std::fopen("order.txt", "ab")})
    {
    std::fputs(FAKE_NAME "\n", order);
    std::fclose(order);
    }
  }

using create_device_fn = HRESULT(WINAPI *)(
  IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, ID3D11Device **,
  D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
);

extern "C" HRESULT WINAPI fake_D3D11CreateDevice(
  IDXGIAdapter * a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, D3D_FEATURE_LEVEL const * l, UINT n, UINT v,
  ID3D11Device ** d, D3D_FEATURE_LEVEL * o, ID3D11DeviceContext ** c)
  {
  note();
  HMODULE const by_name{LoadLibraryW(L"d3d11.dll")};
  auto const create{reinterpret_cast<create_device_fn>(GetProcAddress(by_name, "D3D11CreateDevice"))};
  return create ? create(a, t, s, f, l, n, v, d, o, c) : E_FAIL;
  }

#ifdef FAKE_SWAP
using create_swap_fn = HRESULT(WINAPI *)(
  IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, DXGI_SWAP_CHAIN_DESC const *,
  IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
);

extern "C" HRESULT WINAPI fake_D3D11CreateDeviceAndSwapChain(
  IDXGIAdapter * a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, D3D_FEATURE_LEVEL const * l, UINT n, UINT v,
  DXGI_SWAP_CHAIN_DESC const * sd, IDXGISwapChain ** sc, ID3D11Device ** d, D3D_FEATURE_LEVEL * o, ID3D11DeviceContext ** c)
  {
  note();
  HMODULE const by_name{LoadLibraryW(L"d3d11.dll")};
  auto const create{reinterpret_cast<create_swap_fn>(GetProcAddress(by_name, "D3D11CreateDeviceAndSwapChain"))};
  return create ? create(a, t, s, f, l, n, v, sd, sc, d, o, c) : E_FAIL;
  }
#endif

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
  {
  if(reason == DLL_PROCESS_ATTACH)
    if(std::FILE * loaded{std::fopen("loaded.txt", "ab")})
      {
      std::fputs(FAKE_NAME "\n", loaded);
      std::fclose(loaded);
      }
  return TRUE;
  }
