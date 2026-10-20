#include "r32/linker.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>

#include "r32/isa.hpp"

namespace r32 {
namespace {

uint32_t alignUp(uint32_t v) { return (v + 3u) & ~3u; }

uint32_t rd32(const std::vector<uint8_t>& b, uint32_t off) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(b[off + i]) << (8 * i);
    return v;
}

void wr32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + i] = static_cast<uint8_t>(v >> (8 * i));
}

bool isLocalLabel(const std::string& n) { return n.size() >= 2 && n[0] == '.' && n[1] == 'L'; }

}  // namespace

Executable link(const std::vector<LinkInput>& inputs, const std::string& entry, std::string* map) {
    const size_t n = inputs.size();
    std::vector<uint32_t> textBase(n), dataBase(n);

    // 1. Layout: all .text first, then all .data.
    uint32_t addr = 0;
    for (size_t i = 0; i < n; i++) {
        if (inputs[i].obj.text.size() % 4) throw Error(inputs[i].name + ": .text size is not a multiple of 4");
        textBase[i] = addr;
        addr += static_cast<uint32_t>(inputs[i].obj.text.size());
    }
    const uint32_t textSize = addr;
    for (size_t i = 0; i < n; i++) {
        addr = alignUp(addr);
        dataBase[i] = addr;
        addr += static_cast<uint32_t>(inputs[i].obj.data.size());
    }
    const uint32_t total = alignUp(addr);

    // 2. Global symbol table (with duplicate detection).
    struct Def { uint32_t addr; size_t obj; };
    std::unordered_map<std::string, Def> globals;
    auto symAddr = [&](size_t oi, const Symbol& s) -> uint32_t {
        return (s.sec == Sec::Text ? textBase[oi] : dataBase[oi]) + s.value;
    };
    for (size_t i = 0; i < n; i++) {
        for (auto& s : inputs[i].obj.syms) {
            if (s.sec == Sec::Undef || !s.global) continue;
            auto it = globals.find(s.name);
            if (it != globals.end())
                throw Error("multiple definition of '" + s.name + "' (first defined in " +
                            inputs[it->second.obj].name + ", again in " + inputs[i].name + ")");
            globals[s.name] = {symAddr(i, s), i};
        }
    }

    // 3. Image + relocations.
    Executable exe;
    exe.textSize = textSize;
    exe.image.assign(total, 0);
    for (size_t i = 0; i < n; i++) {
        std::copy(inputs[i].obj.text.begin(), inputs[i].obj.text.end(), exe.image.begin() + textBase[i]);
        std::copy(inputs[i].obj.data.begin(), inputs[i].obj.data.end(), exe.image.begin() + dataBase[i]);
    }
    exe.dataSize = total - textSize;

    for (size_t i = 0; i < n; i++) {
        for (auto& r : inputs[i].obj.relocs) {
            const Symbol& s = inputs[i].obj.syms[r.sym];
            uint32_t target;
            if (s.sec == Sec::Undef) {
                auto it = globals.find(s.name);
                if (it == globals.end())
                    throw Error(inputs[i].name + ": undefined reference to '" + s.name + "'");
                target = it->second.addr;
            } else {
                target = symAddr(i, s);
            }
            target += static_cast<uint32_t>(r.addend);
            uint32_t place = (r.sec == Sec::Text ? textBase[i] : dataBase[i]) + r.offset;
            uint32_t w = rd32(exe.image, place);
            switch (r.type) {
                case RelType::Hi16:
                    w = (w & ~0xFFFFu) | ((target >> 16) & 0xFFFFu);
                    break;
                case RelType::Lo16:
                    w = (w & ~0xFFFFu) | (target & 0xFFFFu);
                    break;
                case RelType::Jal21: {
                    int64_t d = static_cast<int64_t>(target) - static_cast<int64_t>(place);
                    if (d % 4) throw Error(inputs[i].name + ": misaligned call target '" + s.name + "'");
                    d /= 4;
                    if (d < -(1 << 20) || d >= (1 << 20))
                        throw Error(inputs[i].name + ": call to '" + s.name + "' out of range");
                    w = (w & ~0x1FFFFFu) | (static_cast<uint32_t>(d) & 0x1FFFFFu);
                    break;
                }
                case RelType::Abs32:
                    w = target;
                    break;
            }
            wr32(exe.image, place, w);
        }
    }

    // 4. Entry point.
    auto e = globals.find(entry);
    if (e == globals.end()) throw Error("undefined entry symbol '" + entry + "'");
    exe.entry = e->second.addr;

    // 5. Symbols for the executable (used by the disassembler and the map).
    for (size_t i = 0; i < n; i++)
        for (auto& s : inputs[i].obj.syms)
            if (s.sec != Sec::Undef && !isLocalLabel(s.name)) exe.symbols.emplace_back(s.name, symAddr(i, s));
    std::stable_sort(exe.symbols.begin(), exe.symbols.end(),
                     [](auto& a, auto& b) { return a.second < b.second; });

    if (map) {
        char buf[160];
        std::string m = "R32 link map\n============\n";
        std::snprintf(buf, sizeof buf, "entry   : %s = 0x%08x\n", entry.c_str(), exe.entry);
        m += buf;
        std::snprintf(buf, sizeof buf, "text    : 0x%08x - 0x%08x (%u bytes)\n", 0u, textSize, textSize);
        m += buf;
        std::snprintf(buf, sizeof buf, "data    : 0x%08x - 0x%08x (%u bytes)\n\n", textSize, total, total - textSize);
        m += buf;
        m += "Input sections\n";
        for (size_t i = 0; i < n; i++) {
            std::snprintf(buf, sizeof buf, "  %-28s .text 0x%08x (%5zu B)   .data 0x%08x (%5zu B)\n",
                          inputs[i].name.c_str(), textBase[i], inputs[i].obj.text.size(), dataBase[i],
                          inputs[i].obj.data.size());
            m += buf;
        }
        m += "\nSymbols\n";
        for (auto& s : exe.symbols) {
            std::snprintf(buf, sizeof buf, "  0x%08x  %s\n", s.second, s.first.c_str());
            m += buf;
        }
        *map = m;
    }
    return exe;
}

}  // namespace r32
