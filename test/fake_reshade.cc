// A stand-in for ReShade: it exports ReShadeRegisterAddon, and in its DllMain hooks the system d3d11's
// D3D11CreateDevice inline, the jump leading through a stub in memory of no module, as ReShade 6.8.0 does (2026-11-17).
// Its hook passes each call on to the real function: the bytes put back for the call, the hook again after it.
#include <windows.h>

#include <d3d11.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
  {
  unsigned char * at{};
  unsigned char saved[14]{};
  unsigned char jump[14]{0xFF, 0x25, 0, 0, 0, 0};  // jmp qword ptr [rip+0]; dq stub

  auto write(unsigned char const * bytes) -> void
    {
    DWORD old{};
    if(VirtualProtect(at, sizeof jump, PAGE_EXECUTE_READWRITE, &old))
      {
      std::memcpy(at, bytes, sizeof jump);
      VirtualProtect(at, sizeof jump, old, &old);
      FlushInstructionCache(GetCurrentProcess(), at, sizeof jump);
      }
    }

  HRESULT WINAPI hooked_create(IDXGIAdapter * adapter, D3D_DRIVER_TYPE driver_type, HMODULE software, UINT flags,
                               D3D_FEATURE_LEVEL const * levels, UINT level_count, UINT sdk, ID3D11Device ** device,
                               D3D_FEATURE_LEVEL * level, ID3D11DeviceContext ** context)
    {
    if(std::FILE * order{std::fopen("order.txt", "ab")})
      {
      std::fputs("reshade\n", order);
      std::fclose(order);
      }
    write(saved);
    HRESULT const hr{reinterpret_cast<decltype(&D3D11CreateDevice)>(at)(adapter, driver_type, software, flags, levels,
                                                                        level_count, sdk, device, level, context)};
    write(jump);
    return hr;
    }
  }  // namespace

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE, std::uint32_t) { return false; }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
  {
  if(reason != DLL_PROCESS_ATTACH)
    return TRUE;
  char sys[MAX_PATH]{};
  GetSystemDirectoryA(sys, MAX_PATH);
  std::strcat(sys, "\\d3d11.dll");
  at = reinterpret_cast<unsigned char *>(GetProcAddress(GetModuleHandleA(sys), "D3D11CreateDevice"));
  // the stub: jmp qword ptr [rip+0]; dq hooked_create, in memory of no module
  auto * const stub{static_cast<unsigned char *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))};
  if(not at or not stub)
    return TRUE;
  void * const target{reinterpret_cast<void *>(&hooked_create)};
  std::memcpy(stub, jump, 6);
  std::memcpy(stub + 6, &target, sizeof target);
  void * const to_stub{stub};
  std::memcpy(jump + 6, &to_stub, sizeof to_stub);
  std::memcpy(saved, at, sizeof saved);
  write(jump);
  return TRUE;
  }
