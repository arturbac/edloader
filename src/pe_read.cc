// edloader - reading modules: their names, jumps at a function's start, a function's bytes on disk.
#include "runtime.h"
#include "pe_read.h"

#include <tlhelp32.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace edloader
  {
  auto narrow(std::wstring const & w) -> std::string
    {
    std::string out;
    for(wchar_t const c: w)
      out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
    }

  ///\brief the module an address lies in, by name; "?" when none
  auto module_name_of(void const * address) -> std::wstring
    {
    HMODULE const m{edplugin_common::module_of(address)};
    std::wstring name{m ? edplugin_common::file_name_of(edplugin_common::module_path(m)) : std::wstring{}};
    return name.empty() ? std::wstring{L"?"} : name;
    }

  ///\brief where a jump at the start of a function leads (E9 rel32, FF 25 [rip+rel32], 48 B8 imm64 + FF E0); null if
  /// none. `p` holds the function's first bytes as read (a copy), `origin` is where the function itself starts: the
  /// relative forms count from the function, not from the copy. The pointer of FF 25 is read only when readable
  auto jump_target(std::uint8_t const * p, void const * origin) -> void const *
    {
    auto const at{static_cast<std::uint8_t const *>(origin)};
    if(p[0] == 0xE9)
      {
      std::int32_t rel;
      std::memcpy(&rel, p + 1, 4);
      return at + 5 + rel;
      }
    if(p[0] == 0xFF and p[1] == 0x25)
      {
      std::int32_t rel;
      std::memcpy(&rel, p + 2, 4);
      void const * const slot{at + 6 + rel};
      void const * target{};
      if(rel >= 0 and rel <= 2)
        std::memcpy(&target, p + 6 + rel, sizeof target);  // within the 16 bytes read: the copy holds it
      else if(edplugin_common::readable(slot, sizeof target))
        std::memcpy(&target, slot, sizeof target);
      return target;
      }
    if(p[0] == 0x48 and p[1] == 0xB8 and p[10] == 0xFF and p[11] == 0xE0)
      {
      void const * target;
      std::memcpy(&target, p + 2, sizeof target);
      return target;
      }
    return nullptr;
    }

  ///\brief the game exe's import slots of the two device functions from d3d11.dll, once

  auto bytes_on_disk(std::wstring const & path, char const * function, std::uint8_t (&out)[16], std::wstring & why) -> bool
    {
    std::FILE * f{_wfopen(path.c_str(), L"rb")};
    if(not f)
      {
      why = L"the file cannot be read";
      return false;
      }
    std::vector<std::uint8_t> file;
    std::uint8_t buf[65536];
    for(std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
      file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    // every offset read from the file is checked against it: a cut or damaged dll is "not comparable", never a read
    // past the buffer
    auto const at = [&](std::size_t off, std::size_t len) { return off <= file.size() and len <= file.size() - off; };
    if(not at(0, sizeof(IMAGE_DOS_HEADER)))
      return why = L"not a dll", false;
    auto const dos{reinterpret_cast<IMAGE_DOS_HEADER const *>(file.data())};
    if(dos->e_lfanew < 0 or not at(static_cast<std::size_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64)))
      return why = L"not a dll", false;
    auto const nt{reinterpret_cast<IMAGE_NT_HEADERS64 const *>(file.data() + dos->e_lfanew)};
    auto const sections{IMAGE_FIRST_SECTION(nt)};
    if(not at(static_cast<std::size_t>(reinterpret_cast<std::uint8_t const *>(sections) - file.data()),
              std::size_t{nt->FileHeader.NumberOfSections} * sizeof(IMAGE_SECTION_HEADER)))
      return why = L"not a dll", false;
    auto const to_offset = [&](DWORD rva) -> std::size_t
      {
      for(WORD i{}; i != nt->FileHeader.NumberOfSections; ++i)
        if(rva >= sections[i].VirtualAddress and rva < sections[i].VirtualAddress + sections[i].SizeOfRawData)
          return rva - sections[i].VirtualAddress + sections[i].PointerToRawData;
      return SIZE_MAX;
      };
    IMAGE_DATA_DIRECTORY const & ed{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]};
    std::size_t const eo{to_offset(ed.VirtualAddress)};
    if(eo == SIZE_MAX or not at(eo, sizeof(IMAGE_EXPORT_DIRECTORY)))
      return why = L"no export table", false;
    auto const exp{reinterpret_cast<IMAGE_EXPORT_DIRECTORY const *>(file.data() + eo)};
    std::size_t const names{to_offset(exp->AddressOfNames)}, ords{to_offset(exp->AddressOfNameOrdinals)}, funcs{to_offset(exp->AddressOfFunctions)};
    DWORD rva{};
    std::size_t const function_size{std::strlen(function) + 1};
    for(DWORD i{}; i != exp->NumberOfNames and names != SIZE_MAX and ords != SIZE_MAX and funcs != SIZE_MAX; ++i)
      {
      if(not at(names + std::size_t{i} * 4, 4) or not at(ords + std::size_t{i} * 2, 2))
        break;
      DWORD name_rva;
      std::memcpy(&name_rva, file.data() + names + std::size_t{i} * 4, 4);
      std::size_t const no{to_offset(name_rva)};
      if(no == SIZE_MAX or not at(no, function_size) or std::memcmp(file.data() + no, function, function_size) != 0)
        continue;
      WORD ord;
      std::memcpy(&ord, file.data() + ords + std::size_t{i} * 2, 2);
      if(not at(funcs + std::size_t{ord} * 4, 4))
        break;
      std::memcpy(&rva, file.data() + funcs + std::size_t{ord} * 4, 4);
      break;
      }
    if(rva == 0)
      return why = L"not exported by name", false;
    if(rva >= ed.VirtualAddress and rva < ed.VirtualAddress + ed.Size)
      return why = L"forwarded", false;
    // a relocation inside the first bytes would differ in memory without anyone's hook
    IMAGE_DATA_DIRECTORY const & rd{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]};
    std::size_t const reloc{to_offset(rd.VirtualAddress)};
    for(std::size_t ro{reloc}, end{reloc == SIZE_MAX ? 0 : reloc + rd.Size}; ro != SIZE_MAX and ro + 8 <= end and at(ro, 8);)
      {
      auto const block{reinterpret_cast<IMAGE_BASE_RELOCATION const *>(file.data() + ro)};
      if(block->SizeOfBlock < 8)
        break;
      for(std::size_t e{8}; e + 2 <= block->SizeOfBlock and at(ro + e, 2); e += 2)
        {
        WORD entry;
        std::memcpy(&entry, file.data() + ro + e, 2);
        DWORD const where{block->VirtualAddress + (entry & 0xFFFu)};
        if((entry >> 12) != IMAGE_REL_BASED_ABSOLUTE and where + 8 > rva and where < rva + 16)
          return why = L"a relocation in its first bytes", false;
        }
      ro += block->SizeOfBlock;
      }
    std::size_t const fo{to_offset(rva)};
    if(fo == SIZE_MAX or not at(fo, sizeof out))
      return why = L"outside the file", false;
    std::memcpy(out, file.data() + fo, sizeof out);
    return true;
    }

  ///\brief the modules in the process when edloader starts: whoever was there may have changed d3d11 already
  }  // namespace edloader
