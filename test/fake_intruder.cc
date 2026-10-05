// A stand-in for a dll that takes the game's d3d11 calls past edloader's list: in its DllMain it rewrites the host
// exe's import of D3D11CreateDevice from d3d11.dll to its own function, which asks for d3d11.dll by name and calls on.
#include <windows.h>

#include <d3d11.h>

#include <cstring>

using create_device_fn = HRESULT(WINAPI *)(
  IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT, ID3D11Device **,
  D3D_FEATURE_LEVEL *, ID3D11DeviceContext **
);

static HRESULT WINAPI intruder_create(
  IDXGIAdapter * a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, D3D_FEATURE_LEVEL const * l, UINT n, UINT v,
  ID3D11Device ** d, D3D_FEATURE_LEVEL * o, ID3D11DeviceContext ** c)
  {
  auto const create{reinterpret_cast<create_device_fn>(GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "D3D11CreateDevice"))};
  return create ? create(a, t, s, f, l, n, v, d, o, c) : E_FAIL;
  }

static void take_the_import()
  {
  auto const base{reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr))};
  auto const nt{reinterpret_cast<IMAGE_NT_HEADERS const *>(base + reinterpret_cast<IMAGE_DOS_HEADER const *>(base)->e_lfanew)};
  IMAGE_DATA_DIRECTORY const & dir{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]};
  for(auto desc{reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR const *>(base + dir.VirtualAddress)}; desc->Name; ++desc)
    {
    if(_stricmp(reinterpret_cast<char const *>(base + desc->Name), "d3d11.dll") != 0)
      continue;
    auto names{reinterpret_cast<IMAGE_THUNK_DATA const *>(base + desc->OriginalFirstThunk)};
    auto slots{reinterpret_cast<IMAGE_THUNK_DATA *>(base + desc->FirstThunk)};
    for(; names->u1.AddressOfData; ++names, ++slots)
      if(not IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
         and std::strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME const *>(base + names->u1.AddressOfData)->Name, "D3D11CreateDevice") == 0)
        {
        DWORD old{};
        VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
        slots->u1.Function = reinterpret_cast<ULONG_PTR>(&intruder_create);
        VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
        }
    }
  }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
  {
  if(reason == DLL_PROCESS_ATTACH)
    take_the_import();
  return TRUE;
  }
