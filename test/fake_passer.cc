// A stand-in for an antivirus's hook in a mod's own call (Bitdefender's bdhkm64.dll, seen 2026-11-17 in a player's
// log): the test copies it beside the game as bdhkm64.dll; fake_p calls its pass_D3D11CreateDevice instead of
// d3d11.dll's, and it asks for "d3d11.dll" by name and calls on, so the call reaches edloader from this dll.
#include <windows.h>

#include <d3d11.h>

using create_device_fn = HRESULT(WINAPI *)(
  IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, ID3D11Device **,
  D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
);

extern "C" __declspec(dllexport) HRESULT WINAPI pass_D3D11CreateDevice(
  IDXGIAdapter * a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, D3D_FEATURE_LEVEL const * l, UINT n, UINT v,
  ID3D11Device ** d, D3D_FEATURE_LEVEL * o, ID3D11DeviceContext ** c)
  {
  HMODULE const by_name{LoadLibraryW(L"d3d11.dll")};
  auto const create{reinterpret_cast<create_device_fn>(GetProcAddress(by_name, "D3D11CreateDevice"))};
  return create ? create(a, t, s, f, l, n, v, d, o, c) : E_FAIL;
  }
