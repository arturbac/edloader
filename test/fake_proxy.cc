// A stand-in d3d11 proxy for the chain test: notes its name in order.txt, then asks for "d3d11.dll" BY NAME
// (as 3Dmigoto does) and calls its D3D11CreateDevice - which is edloader, which must pass it on.
#include <windows.h>

#include <d3d11.h>

#include <cstdio>

using create_device_fn = HRESULT(WINAPI *)(
  IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, ID3D11Device **,
  D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
);

extern "C" HRESULT WINAPI fake_D3D11CreateDevice(
  IDXGIAdapter * a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, D3D_FEATURE_LEVEL const * l, UINT n, UINT v,
  ID3D11Device ** d, D3D_FEATURE_LEVEL * o, ID3D11DeviceContext ** c)
  {
  if(std::FILE * order{std::fopen("order.txt", "ab")})
    {
    std::fputs(FAKE_NAME "\n", order);
    std::fclose(order);
    }
  HMODULE const by_name{LoadLibraryW(L"d3d11.dll")};
  auto const create{reinterpret_cast<create_device_fn>(GetProcAddress(by_name, "D3D11CreateDevice"))};
  return create ? create(a, t, s, f, l, n, v, d, o, c) : E_FAIL;
  }
