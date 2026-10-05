// A stand-in for 3Dmigoto loaded under another name: in its DllMain it hooks D3D11CreateDevice inline in the module
// named d3d11.dll (edloader), as 3Dmigoto's HookD3D11 does. Its hook only notes that it ran and fails the call:
// edloader must put its own bytes back, so the hook never runs.
#include <windows.h>

#include <d3d11.h>

#include <cstdio>
#include <cstring>

static HRESULT WINAPI hooked_create(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const *, UINT, UINT,
                                    ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **)
  {
  if(std::FILE * order{std::fopen("order.txt", "ab")})
    {
    std::fputs("hooker\n", order);
    std::fclose(order);
    }
  return E_FAIL;
  }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
  {
  if(reason != DLL_PROCESS_ATTACH)
    return TRUE;
  auto const at{reinterpret_cast<unsigned char *>(GetProcAddress(GetModuleHandleA("d3d11.dll"), "D3D11CreateDevice"))};
  if(not at)
    return TRUE;
  // jmp qword ptr [rip+0]; dq hooked_create
  unsigned char jump[14]{0xFF, 0x25, 0, 0, 0, 0};
  void * const target{reinterpret_cast<void *>(&hooked_create)};
  std::memcpy(jump + 6, &target, sizeof target);
  DWORD old{};
  if(std::FILE * note{std::fopen("hooker.txt", "ab")})
    {
    char path[MAX_PATH]{};
    GetModuleFileNameA(GetModuleHandleA("d3d11.dll"), path, MAX_PATH);
    std::fprintf(note, "hooking %s at %p\n", path, static_cast<void *>(at));
    std::fclose(note);
    }
  if(VirtualProtect(at, sizeof jump, PAGE_EXECUTE_READWRITE, &old))
    {
    std::memcpy(at, jump, sizeof jump);
    VirtualProtect(at, sizeof jump, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof jump);
    }
  return TRUE;
  }
