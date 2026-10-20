// r32objdump: disassembler and object inspector for .o and .exe files
#include <cstdio>
#include <string>

#include "r32/isa.hpp"
#include "r32/object.hpp"

using namespace r32;

static uint32_t word(const std::vector<uint8_t>& b, uint32_t off) {
    return b[off] | b[off + 1] << 8 | b[off + 2] << 16 | static_cast<uint32_t>(b[off + 3]) << 24;
}

static const char* relName(RelType t) {
    switch (t) {
        case RelType::Hi16: return "HI16";
        case RelType::Lo16: return "LO16";
        case RelType::Jal21: return "JAL21";
        case RelType::Abs32: return "ABS32";
    }
    return "?";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: r32objdump file.o|file.exe   (disassembly, symbols and relocations)\n");
        return 2;
    }
    try {
        auto bytes = readFile(argv[1]);
        char kind = fileKind(bytes);
        if (kind == 'O') {
            Object o = deserializeObject(bytes);
            std::printf("object file: .text %zu bytes, .data %zu bytes\n\nSymbols:\n", o.text.size(), o.data.size());
            for (auto& s : o.syms) {
                if (s.name.compare(0, 2, ".L") == 0) continue;
                const char* sec = s.sec == Sec::Text ? ".text" : s.sec == Sec::Data ? ".data" : "*UND*";
                std::printf("  %-6s %08x  %c  %s\n", sec, s.value, s.global ? 'G' : 'l', s.name.c_str());
            }
            std::printf("\nRelocations:\n");
            for (auto& r : o.relocs)
                std::printf("  %-5s %08x  %-6s %s%+d\n", r.sec == Sec::Text ? ".text" : ".data", r.offset, relName(r.type),
                            o.syms[r.sym].name.c_str(), r.addend);
            std::printf("\nDisassembly of .text:\n");
            for (uint32_t pc = 0; pc < o.text.size(); pc += 4) {
                for (auto& s : o.syms)
                    if (s.sec == Sec::Text && s.value == pc && s.name.compare(0, 2, ".L") != 0) std::printf("%s:\n", s.name.c_str());
                std::printf("  %08x:  %08x  %s\n", pc, word(o.text, pc), disasm(word(o.text, pc), pc).c_str());
            }
        } else if (kind == 'X') {
            Executable x = deserializeExecutable(bytes);
            std::printf("executable: entry 0x%08x, .text %u bytes, .data %u bytes\n\nDisassembly of .text:\n", x.entry, x.textSize, x.dataSize);
            for (uint32_t pc = 0; pc < x.textSize; pc += 4) {
                for (auto& s : x.symbols)
                    if (s.second == pc) std::printf("%s:\n", s.first.c_str());
                std::printf("  %08x:  %08x  %s\n", pc, word(x.image, pc), disasm(word(x.image, pc), pc).c_str());
            }
            std::printf("\n.data symbols:\n");
            for (auto& s : x.symbols)
                if (s.second >= x.textSize) std::printf("  %08x  %s\n", s.second, s.first.c_str());
        } else {
            throw Error("not an R32 object or executable");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "r32objdump: %s\n", e.what());
        return 1;
    }
    return 0;
}
