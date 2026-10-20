#include "r32/object.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "r32/isa.hpp"

namespace r32 {
namespace {

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void str(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    void bytes(const std::vector<uint8_t>& v) {
        u32(static_cast<uint32_t>(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
};

struct Reader {
    const std::vector<uint8_t>& b;
    size_t p = 0;
    explicit Reader(const std::vector<uint8_t>& v) : b(v) {}
    void need(size_t n) {
        if (p + n > b.size()) throw Error("truncated or corrupt file");
    }
    uint8_t u8() { need(1); return b[p++]; }
    uint32_t u32() {
        need(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(b[p + i]) << (8 * i);
        p += 4;
        return v;
    }
    std::string str() {
        uint32_t n = u32();
        need(n);
        std::string s(b.begin() + p, b.begin() + p + n);
        p += n;
        return s;
    }
    std::vector<uint8_t> bytes() {
        uint32_t n = u32();
        need(n);
        std::vector<uint8_t> v(b.begin() + p, b.begin() + p + n);
        p += n;
        return v;
    }
};

void magic(Writer& w, const char* m) {
    for (int i = 0; i < 4; i++) w.u8(static_cast<uint8_t>(m[i]));
}

void expectMagic(Reader& r, const char* m, const char* what) {
    r.need(4);
    if (std::memcmp(&r.b[0], m, 4) != 0) throw Error(std::string("not an R32 ") + what + " file");
    r.p = 4;
}

}  // namespace

std::vector<uint8_t> serialize(const Object& o) {
    Writer w;
    magic(w, "R32O");
    w.bytes(o.text);
    w.bytes(o.data);
    w.u32(static_cast<uint32_t>(o.syms.size()));
    for (auto& s : o.syms) {
        w.str(s.name);
        w.u8(static_cast<uint8_t>(s.sec));
        w.u32(s.value);
        w.u8(s.global ? 1 : 0);
    }
    w.u32(static_cast<uint32_t>(o.relocs.size()));
    for (auto& r : o.relocs) {
        w.u8(static_cast<uint8_t>(r.sec));
        w.u32(r.offset);
        w.u32(r.sym);
        w.u8(static_cast<uint8_t>(r.type));
        w.u32(static_cast<uint32_t>(r.addend));
    }
    return w.b;
}

Object deserializeObject(const std::vector<uint8_t>& bytes) {
    Reader r(bytes);
    expectMagic(r, "R32O", "object");
    Object o;
    o.text = r.bytes();
    o.data = r.bytes();
    uint32_t ns = r.u32();
    for (uint32_t i = 0; i < ns; i++) {
        Symbol s;
        s.name = r.str();
        uint8_t sec = r.u8();
        if (sec > 2) throw Error("corrupt symbol table");
        s.sec = static_cast<Sec>(sec);
        s.value = r.u32();
        s.global = r.u8() != 0;
        o.syms.push_back(std::move(s));
    }
    uint32_t nr = r.u32();
    for (uint32_t i = 0; i < nr; i++) {
        Reloc rl;
        uint8_t sec = r.u8();
        if (sec > 1) throw Error("corrupt relocation table");
        rl.sec = static_cast<Sec>(sec);
        rl.offset = r.u32();
        rl.sym = r.u32();
        uint8_t t = r.u8();
        if (t > 3) throw Error("corrupt relocation type");
        rl.type = static_cast<RelType>(t);
        rl.addend = static_cast<int32_t>(r.u32());
        if (rl.sym >= o.syms.size()) throw Error("relocation refers to a missing symbol");
        o.relocs.push_back(rl);
    }
    return o;
}

std::vector<uint8_t> serialize(const Executable& x) {
    Writer w;
    magic(w, "R32X");
    w.u32(x.entry);
    w.u32(x.textSize);
    w.u32(x.dataSize);
    w.bytes(x.image);
    w.u32(static_cast<uint32_t>(x.symbols.size()));
    for (auto& s : x.symbols) {
        w.str(s.first);
        w.u32(s.second);
    }
    return w.b;
}

Executable deserializeExecutable(const std::vector<uint8_t>& bytes) {
    Reader r(bytes);
    expectMagic(r, "R32X", "executable");
    Executable x;
    x.entry = r.u32();
    x.textSize = r.u32();
    x.dataSize = r.u32();
    x.image = r.bytes();
    if (x.image.size() != static_cast<size_t>(x.textSize) + x.dataSize)
        throw Error("corrupt executable: image size mismatch");
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; i++) {
        std::string name = r.str();
        uint32_t a = r.u32();
        x.symbols.emplace_back(std::move(name), a);
    }
    return x;
}

char fileKind(const std::vector<uint8_t>& b) {
    if (b.size() < 4) return 0;
    if (std::memcmp(b.data(), "R32O", 4) == 0) return 'O';
    if (std::memcmp(b.data(), "R32X", 4) == 0) return 'X';
    return 0;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("cannot open '" + path + "'");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw Error("cannot write '" + path + "'");
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string readTextFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw Error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace r32
