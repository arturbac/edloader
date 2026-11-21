// The chain test's game for D3D11CreateDeviceAndSwapChain (edworld's test_app asks for D3D11CreateDevice only): it
// asks the d3d11.dll beside it for a device without a swap chain, which the call allows, and prints the result.
#include <windows.h>

#include <d3d11.h>

#include <cstdio>

using create_swap_fn = HRESULT(WINAPI *)(
  IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, DXGI_SWAP_CHAIN_DESC const *,
  IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
);

auto main() -> int
  {
  HMODULE const d3d11{LoadLibraryW(L"d3d11.dll")};
  auto const create{d3d11 ? reinterpret_cast<create_swap_fn>(GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain")) : nullptr};
  if(not create)
    {
    std::puts("swap_app: no D3D11CreateDeviceAndSwapChain");
    return 1;
    }
  ID3D11Device * device{};
  ID3D11DeviceContext * context{};
  HRESULT const hr{create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, nullptr, nullptr,
                          &device, nullptr, &context)};
  std::printf("swap_app: hr 0x%08lX\n", static_cast<unsigned long>(hr));
  if(context)
    context->Release();
  if(device)
    device->Release();
  return 0;
  }
