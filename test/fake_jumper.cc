// A hook with a relative jump: in its DllMain it hooks the system d3d11's D3D11CreateDevice inline with
// `jmp qword ptr [rip+rel32]`, rel32 far from 0, through a pointer in memory of no module near the function, the
// pointer leading into this dll. edloader reads where the jump leads from the function's own address, so its log
// names fake_jumper.dll; counted from a copy of the bytes, the pointer would be read from elsewhere. Its hook passes
// each call on to the real function: the bytes put back for the call, the hook again after it.
#include <windows.h>

#include <d3d11.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
  {
  unsigned char * at{};
  unsigned char saved[6]{};
  unsigned char jump[6]{0xFF, 0x25, 0, 0, 0, 0};  // jmp qword ptr [rip+rel32]

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
      std::fputs("jumper\n", order);
      std::fclose(order);
      }
    write(saved);
    HRESULT const hr{reinterpret_cast<decltype(&D3D11CreateDevice)>(at)(adapter, driver_type, software, flags, levels,
                                                                        level_count, sdk, device, level, context)};
    write(jump);
    return hr;
    }

  ///\brief a page of no module within reach of a rel32 from `function`, tried 64 KiB apart on both sides
  auto alloc_near(unsigned char * function) -> unsigned char *
    {
    auto const base{reinterpret_cast<std::uintptr_t>(function) & ~std::uintptr_t{0xFFFF}};
    for(std::uintptr_t step{1}; step != 0x7000; ++step)
      {
      std::uintptr_t const tries[2]{base + step * 0x10000, base - step * 0x10000};
      for(std::uintptr_t const to: tries)
        if(void * const page{VirtualAlloc(reinterpret_cast<void *>(to), 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)})
          return static_cast<unsigned char *>(page);
      }
    return nullptr;
    }
  }  // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
  {
  if(reason != DLL_PROCESS_ATTACH)
    return TRUE;
  char sys[MAX_PATH]{};
  GetSystemDirectoryA(sys, MAX_PATH);
  std::strcat(sys, "\\d3d11.dll");
  at = reinterpret_cast<unsigned char *>(GetProcAddress(GetModuleHandleA(sys), "D3D11CreateDevice"));
  unsigned char * const slot{at ? alloc_near(at) : nullptr};
  if(not slot)
    return TRUE;
  void * const target{reinterpret_cast<void *>(&hooked_create)};
  std::memcpy(slot, &target, sizeof target);
  auto const rel{static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(slot) - reinterpret_cast<std::intptr_t>(at + 6))};
  std::memcpy(jump + 2, &rel, sizeof rel);
  std::memcpy(saved, at, sizeof saved);
  write(jump);
  return TRUE;
  }
