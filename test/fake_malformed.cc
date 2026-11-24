// A plain plugin (a + line, not chained) whose own image is out of shape while it is loaded, for edloader's scans of
// the process's modules: FAKE_MALFORMED=imports-out moves its import directory past the image's end,
// FAKE_MALFORMED=header-noaccess takes every right off its headers' page. edloader must pass it over with a line, never
// fault, and the chain must work. At DLL_PROCESS_DETACH the headers and their page's rights are put back.
#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

static HMODULE self{};
static IMAGE_DATA_DIRECTORY import_kept{};
static DWORD rights_kept{};
static bool moved{}, closed{};

static auto nt_headers() -> IMAGE_NT_HEADERS64 *
  {
  auto * const base{reinterpret_cast<unsigned char *>(self)};
  LONG lfanew;
  std::memcpy(&lfanew, base + offsetof(IMAGE_DOS_HEADER, e_lfanew), sizeof lfanew);
  return reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + lfanew);
  }

static auto note(char const * text) -> void
  {
  if(std::FILE * f{std::fopen("malformed.txt", "ab")})
    {
    std::fputs(text, f);
    std::fputc('\n', f);
    std::fclose(f);
    }
  }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
  {
  if(reason == DLL_PROCESS_ATTACH)
    {
    self = instance;
    char mode[32]{};
    GetEnvironmentVariableA("FAKE_MALFORMED", mode, sizeof mode);
    IMAGE_NT_HEADERS64 * const nt{nt_headers()};
    DWORD old{};
    if(std::strcmp(mode, "imports-out") == 0 and VirtualProtect(nt, sizeof *nt, PAGE_READWRITE, &old))
      {
      IMAGE_DATA_DIRECTORY & dir{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]};
      import_kept = dir;
      dir.VirtualAddress = nt->OptionalHeader.SizeOfImage + 0x1000;
      VirtualProtect(nt, sizeof *nt, old, &old);
      moved = true;
      note("imports-out");
      }
    else if(std::strcmp(mode, "header-noaccess") == 0 and VirtualProtect(self, 1, PAGE_NOACCESS, &rights_kept))
      {
      closed = true;
      note("header-noaccess");
      }
    }
  else if(reason == DLL_PROCESS_DETACH)
    {
    DWORD old{};
    if(closed)
      VirtualProtect(self, 1, rights_kept, &old);
    if(moved)
      {
      IMAGE_NT_HEADERS64 * const nt{nt_headers()};
      if(VirtualProtect(nt, sizeof *nt, PAGE_READWRITE, &old))
        {
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = import_kept;
        VirtualProtect(nt, sizeof *nt, old, &old);
        }
      }
    }
  return TRUE;
  }
