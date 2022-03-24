#include <elf.h>
#include <unistd.h>
 
#include <cassert>
#include <cstdio>
#include <cstring>
 
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#include <queue>
#include <map>
#include <set>
#include <stdlib.h>
#include <string>

class LddWorker {
public :
    LddWorker();
    void Execute(const std::string& file, bool verbose = false);
private:
    enum Error {
        OK,
        EMPTY_FILE,
        WRONG_FILE
    };

    struct DynInfo {
        size_t load_off = 0;
        size_t dynstr_off = 0;
        size_t dynsym_off = 0;
        size_t dynsym_sz = 0;
        size_t dynsect_off = 0;
        size_t dynsect_sz = 0;
    };

    int ReadFile(const std::string& file, std::vector<char>& data, bool verbose = false);

    int GetDirectDependencies(const std::string& file, std::vector<std::string>& dependencies, bool verbose = false);

    int GetAllNames(const std::string& file, std::vector<std::pair<std::string, std::string>>& names, bool verbose = false);
    int GetImportNames(const std::string& file, std::vector<std::string>& names, bool verbose = false);
    int GetExportNames(const std::string& file, std::vector<std::string>& names, bool verbose = false);

    void GetLoadPtr(const char* bytes, DynInfo& info);
    int GetDynInfo(const std::string file, const char* bytes, DynInfo& info, bool verbose = false);

    void ParseLDPath(std::vector<std::string>& prefixes);
    std::string FindLibrary(const std::string& name);
};