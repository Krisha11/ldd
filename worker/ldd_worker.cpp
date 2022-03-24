#include <ldd_worker.h>

LddWorker::LddWorker() {
  possiblePrefixes.push_back("/lib/");
  possiblePrefixes.push_back("/usr/lib/");
  ParseLDPath();
}

int LddWorker::ReadFile(const std::string& file, std::vector<char>& data, bool verbose) {
  data = [fname = file]{
      std::ifstream file(fname, std::ios::binary | std::ios::in);
      return std::vector<char> ( std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>{} );
  }();

  if (data.empty()) {
    if (verbose) {
      std::cerr << "In file: " << file << ". File is empty or does not exist.\n";
    }
    return Error::EMPTY_FILE;
  }

  // Simple ELF checks
  Elf64_Ehdr elf_hdr;
  const unsigned char expected_magic[] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};
  memmove(&elf_hdr, data.data(), sizeof(elf_hdr));
  if (memcmp(elf_hdr.e_ident, expected_magic, sizeof(expected_magic)) != 0) {
    if (verbose) {
      std::cerr << "In file: " << file << "Target is not an ELF executable.\n";
    }

    return Error::WRONG_FILE;
  }
  if (elf_hdr.e_ident[EI_CLASS] != ELFCLASS64) {
    if (verbose) {
      std::cerr << "In file: " << file << ". Sorry, only ELF-64 is supported.\n";
    }

    return Error::WRONG_FILE;
  }
  if (elf_hdr.e_machine != EM_X86_64) {
    if (verbose) {
      std::cerr << "In file: " << file << ". Sorry, only x86-64 is supported.\n";
    }

    return Error::WRONG_FILE;
  }


  return Error::OK;
}

void LddWorker::GetLoadPtr(const char* bytes, DynInfo& info) {
    Elf64_Ehdr elf_hdr;
    memmove(&elf_hdr, bytes, sizeof(elf_hdr));

    for (uint16_t i = 0; i < elf_hdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        memmove(&phdr, bytes + elf_hdr.e_phoff + i * sizeof(phdr), sizeof(phdr));

        if (phdr.p_type == PT_LOAD) {
            info.load_off = phdr.p_paddr - phdr.p_offset;
            break;
        }
    }
}

int LddWorker::GetDynInfo(const std::string file, const char* bytes, DynInfo& info, bool verbose) {
  GetLoadPtr(bytes, info);

  Elf64_Ehdr elf_hdr;
  memmove(&elf_hdr, bytes, sizeof(elf_hdr));


  for (uint16_t i = 0; i < elf_hdr.e_shnum; i++) {
    size_t offset = elf_hdr.e_shoff + i * elf_hdr.e_shentsize;
    Elf64_Shdr shdr;
    memmove(&shdr, bytes + offset, sizeof(shdr));

    switch (shdr.sh_type) {
      case SHT_STRTAB:
        if (!info.dynstr_off) {
          info.dynstr_off = shdr.sh_offset;
        }
        break;
      case SHT_DYNSYM:
        info.dynsym_off = shdr.sh_offset;
        info.dynsym_sz = shdr.sh_size;
        break;
      case SHT_DYNAMIC:
        info.dynsect_off = shdr.sh_offset;
        info.dynsect_sz = shdr.sh_size;
        break; 
    }
  }

  if (!info.dynstr_off || !info.dynsym_off) {
     if (verbose) {
      std::cerr << "In file: " << file << ". Some problems with SHT_STRTAB or SHT_DYNSYM.\n";
    }

    return Error::WRONG_FILE;
  }

  return Error::OK;
}
 
int LddWorker::GetAllNames(const std::string& file, std::vector<std::pair<std::string, std::string>>& names, bool verbose) {
  std::vector<char> data;
  if (int res = ReadFile(file, data, verbose) != Error::OK) {
    return res;
  }
  char* bytes = data.data();  

  DynInfo info;
  if (int res = GetDynInfo(file, bytes, info, verbose) != Error::OK) {
    return res;
  }

  
  names.clear();
  for (size_t j = 0; j * sizeof(Elf64_Sym) < info.dynsym_sz; j++) {
    size_t absoffset = info.dynsym_off + j * sizeof(Elf64_Sym);

    Elf64_Sym sym;
    memmove(&sym, bytes + absoffset, sizeof(sym));

    std::string name = bytes + info.dynstr_off + sym.st_name;
    if (sym.st_shndx == STN_UNDEF) {
        names.push_back({name, "undefined"});
    } 
    else if (ELF64_ST_BIND(sym.st_info) == STB_GLOBAL) {
        names.push_back({name, "global"});
    }
  }

  return Error::OK;
}

int LddWorker::GetImportNames(const std::string& file, std::vector<std::string>& names, bool verbose) {
  std::vector<std::pair<std::string, std::string>> allNames;
  if (int res = GetAllNames(file, allNames, verbose)) {
    return res;
  }

  names.clear();
  for (auto& elem : allNames) {
    if (elem.second == "undefined") {
      names.push_back(elem.first);
    }
  }

  return Error::OK;
}

int LddWorker::GetExportNames(const std::string& file, std::vector<std::string>& names, bool verbose) {
  std::vector<std::pair<std::string, std::string>> allNames;
  if (int res = GetAllNames(file, allNames, verbose)) {
    return res;
  }

  names.clear();
  for (auto& elem : allNames) {
    if (elem.second == "global") {
      names.push_back(elem.first);
    }
  }

  return Error::OK;
}

int LddWorker::GetDirectDependencies(const std::string& file, std::vector<std::string>& dependencies, bool verbose) {
  std::vector<char> data;
  if (int res = ReadFile(file, data, verbose) != Error::OK) {
    return res;
  }
  char* bytes = data.data();  

  DynInfo info;
  if (int res = GetDynInfo(file, bytes, info, verbose) != Error::OK) {
    return res;
  }

  if (!info.dynsect_off) {
      return Error::OK;
  }
  
  dependencies.clear();

  size_t dt_strtab_ofs = 0;
  for (size_t j = 0; j * sizeof(Elf64_Dyn) < info.dynsect_sz; j++) {
    Elf64_Dyn dyn;
    size_t absoffset = info.dynsect_off + j * sizeof(Elf64_Dyn);
    memmove(&dyn, bytes + absoffset, sizeof(dyn));
    if (dyn.d_tag == DT_STRTAB) {
      dt_strtab_ofs = dyn.d_un.d_ptr - info.load_off;
    }
  }

  for (size_t j = 0; j * sizeof(Elf64_Dyn) < info.dynsect_sz; j++) {
    Elf64_Dyn dyn;
    size_t absoffset = info.dynsect_off + j * sizeof(Elf64_Dyn);
    memmove(&dyn, bytes + absoffset, sizeof(dyn));
    if (dyn.d_tag == DT_NEEDED) {
      dependencies.push_back(bytes + dt_strtab_ofs + dyn.d_un.d_val);
    }
  }


  return Error::OK;
}

void LddWorker::ParseLDPath() {
  std::string LD_LIBRARY_PATH = getenv("LD_LIBRARY_PATH");
  if (LD_LIBRARY_PATH == "") {
    return;
  }

  size_t previous = 0;
  size_t index = LD_LIBRARY_PATH.find(";");
  while (index != std::string::npos) {
      possiblePrefixes.push_back(LD_LIBRARY_PATH.substr(previous, index - previous));
      previous = index + 1;
      index = LD_LIBRARY_PATH.find(";", previous);
  }
  possiblePrefixes.push_back(LD_LIBRARY_PATH.substr(previous));
}

std::string LddWorker::FindLibrary(const std::string& name) {
  for (const auto& prefix : possiblePrefixes) {
    std::string attempt = prefix + name;
    std::ifstream libF(attempt);
    if (libF) {
      return attempt;
    }
  }

  return "";
}

void LddWorker::Execute(const std::string& file, bool verbose) {
  std::map<std::string, std::string> dependencies;
  std::queue<std::string> files;

  files.push(file);
  while (files.size() != 0) {
    const auto curFile = files.front();
    files.pop();

    std::vector<std::string> names;
    if (int res = GetDirectDependencies(curFile, names, verbose) != Error::OK) {
      continue;
    }

    for (const auto& name : names) {
      if (dependencies.count(name) || name == "") {
        continue;
      }

      std::string fullName = FindLibrary(name);
      dependencies[name] = fullName;

      if (fullName != "") {
        files.push(fullName);
      }
    }
  }

  for (const auto& dependency : dependencies) {
    std::cout << dependency.first << " => " << (dependency.second == "" ? "?" : dependency.second) << "\n";
  }

  std::cout << "expected import scheme\n";

  // precalac all libraries export names
  std::vector<std::pair<std::string, std::set<std::string>>> exportNames;
  for (const auto& dependency : dependencies) {
    if (dependency.second == "") {
      continue;
    }

    std::set<std::string> s;
    std::vector<std::string> names;
    if (GetExportNames(dependency.second, names) != Error::OK) {
      continue;
    }

    for (const auto& name : names) {
      s.insert(name);
      std::cout << name << '\n';
    }
    exportNames.push_back({ dependency.first, s });
    std::cout << dependency.first << " kek\n";
  }  

  std::vector<std::string> importNames;

  GetImportNames(file, importNames);
  for (const auto& importName : importNames) {
    std::string from = "?";
    for (const auto& exportName : exportNames) {
      if (exportName.second.count(importName) != 0) {
        from = exportName.first;
        break;
      }
    }

    std::cout << importName << "\t => \t" << from << '\n';
  }

}
