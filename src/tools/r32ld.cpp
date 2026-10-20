// r32ld: linker  (a.o b.o ... -> program.exe)
#include <cstdio>
#include <string>
#include <vector>

#include "r32/isa.hpp"
#include "r32/linker.hpp"

int main(int argc, char** argv) {
    std::string out = "a.exe", entry = "_start", mapFile;
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "-e" && i + 1 < argc) entry = argv[++i];
        else if (a == "-m" && i + 1 < argc) mapFile = argv[++i];
        else files.push_back(a);
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: r32ld [-e entry] [-m map.txt] -o out.exe a.o b.o ...\n");
        return 2;
    }
    try {
        std::vector<r32::LinkInput> in;
        for (auto& f : files) {
            auto bytes = r32::readFile(f);
            if (r32::fileKind(bytes) != 'O') throw r32::Error(f + ": not an R32 object file");
            in.push_back({f, r32::deserializeObject(bytes)});
        }
        std::string map;
        r32::Executable x = r32::link(in, entry, mapFile.empty() ? nullptr : &map);
        r32::writeFile(out, r32::serialize(x));
        if (!mapFile.empty()) r32::writeFile(mapFile, std::vector<uint8_t>(map.begin(), map.end()));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "r32ld: %s\n", e.what());
        return 1;
    }
    return 0;
}
