// Relocatable object files (R32O) and linked executables (R32X).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace r32 {

enum class Sec : uint8_t { Text = 0, Data = 1, Undef = 2 };
enum class RelType : uint8_t {
    Hi16 = 0,   // imm16 of a LUI  <- (addr >> 16)
    Lo16 = 1,   // imm16 of an ORI <- (addr & 0xFFFF)
    Jal21 = 2,  // imm21 of a JAL  <- (addr - pc) / 4
    Abs32 = 3   // 32-bit data word <- addr
};

struct Symbol {
    std::string name;
    Sec sec = Sec::Undef;
    uint32_t value = 0;  // offset within its section
    bool global = false;
};

struct Reloc {
    Sec sec = Sec::Text;  // section that contains the place to patch
    uint32_t offset = 0;
    uint32_t sym = 0;     // index into Object::syms
    RelType type = RelType::Abs32;
    int32_t addend = 0;
};

struct Object {
    std::vector<uint8_t> text, data;
    std::vector<Symbol> syms;
    std::vector<Reloc> relocs;
};

struct Executable {
    uint32_t entry = 0;
    uint32_t textSize = 0;
    uint32_t dataSize = 0;
    std::vector<uint8_t> image;  // text followed by data, loaded at address 0
    std::vector<std::pair<std::string, uint32_t>> symbols;
};

std::vector<uint8_t> serialize(const Object& o);
Object deserializeObject(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize(const Executable& x);
Executable deserializeExecutable(const std::vector<uint8_t>& bytes);

std::vector<uint8_t> readFile(const std::string& path);
void writeFile(const std::string& path, const std::vector<uint8_t>& bytes);
std::string readTextFile(const std::string& path);

// Returns 'O' for an object file, 'X' for an executable, 0 otherwise.
char fileKind(const std::vector<uint8_t>& bytes);

}  // namespace r32
