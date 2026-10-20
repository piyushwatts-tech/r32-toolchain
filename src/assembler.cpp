#include "r32/assembler.hpp"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include "r32/isa.hpp"

namespace r32 {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$'; }

bool parseNumber(const std::string& s, long long& out) {
    if (s.empty()) return false;
    const char* p = s.c_str();
    char* end = nullptr;
    long long v = std::strtoll(p, &end, 0);
    if (*end != '\0' || end == p) return false;
    out = v;
    return true;
}

struct Fixup {
    enum Kind { Branch, Jal, Hi, Lo } kind;
    uint32_t off;
    std::string sym;
    int line;
};

class Assembler {
public:
    explicit Assembler(std::string file) : file_(std::move(file)) {}

    Object run(const std::string& src) {
        size_t pos = 0;
        while (pos <= src.size()) {
            size_t nl = src.find('\n', pos);
            if (nl == std::string::npos) nl = src.size();
            line_++;
            handleLine(src.substr(pos, nl - pos));
            pos = nl + 1;
        }
        resolve();
        for (auto& s : obj_.syms)
            if (s.sec == Sec::Undef) s.global = true;
        return std::move(obj_);
    }

private:
    std::string file_;
    int line_ = 0;
    Sec cur_ = Sec::Text;
    Object obj_;
    std::unordered_map<std::string, uint32_t> symIdx_;
    std::vector<Fixup> fixups_;

    [[noreturn]] void fail(const std::string& msg, int line = -1) const {
        throw Error(file_ + ":" + std::to_string(line < 0 ? line_ : line) + ": " + msg);
    }

    std::vector<uint8_t>& bytes() { return cur_ == Sec::Text ? obj_.text : obj_.data; }

    uint32_t symbol(const std::string& name) {
        auto it = symIdx_.find(name);
        if (it != symIdx_.end()) return it->second;
        Symbol s;
        s.name = name;
        obj_.syms.push_back(s);
        symIdx_[name] = static_cast<uint32_t>(obj_.syms.size() - 1);
        return static_cast<uint32_t>(obj_.syms.size() - 1);
    }

    void define(const std::string& name) {
        Symbol& s = obj_.syms[symbol(name)];
        if (s.sec != Sec::Undef) fail("duplicate label '" + name + "'");
        s.sec = cur_;
        s.value = static_cast<uint32_t>(bytes().size());
    }

    void handleLine(std::string text) {
        for (size_t i = 0; i < text.size(); i++)
            if (text[i] == '#' || text[i] == ';') { text.resize(i); break; }
        text = trim(text);
        // Leading labels ("name:"), possibly several on one line.
        while (!text.empty() && isIdentStart(text[0])) {
            size_t i = 1;
            while (i < text.size() && isIdentChar(text[i])) i++;
            if (i < text.size() && text[i] == ':') {
                define(text.substr(0, i));
                text = trim(text.substr(i + 1));
            } else {
                break;
            }
        }
        if (text.empty()) return;
        size_t sp = 0;
        while (sp < text.size() && !std::isspace(static_cast<unsigned char>(text[sp]))) sp++;
        std::string head = text.substr(0, sp);
        std::string rest = trim(text.substr(sp));
        std::vector<std::string> ops;
        if (!rest.empty()) {
            size_t a = 0;
            while (true) {
                size_t c = rest.find(',', a);
                ops.push_back(trim(rest.substr(a, c == std::string::npos ? std::string::npos : c - a)));
                if (c == std::string::npos) break;
                a = c + 1;
            }
        }
        if (head[0] == '.') directive(head, ops);
        else instruction(head, ops);
    }

    void directive(const std::string& d, const std::vector<std::string>& ops) {
        if (d == ".text") { cur_ = Sec::Text; return; }
        if (d == ".data") { cur_ = Sec::Data; return; }
        if (d == ".globl" || d == ".global") {
            if (ops.size() != 1 || ops[0].empty()) fail(d + " expects a symbol");
            obj_.syms[symbol(ops[0])].global = true;
            return;
        }
        if (d == ".align") {
            while (bytes().size() % 4) bytes().push_back(0);
            return;
        }
        if (d == ".space") {
            long long n;
            if (ops.size() != 1 || !parseNumber(ops[0], n) || n < 0 || n > (1 << 24)) fail(".space expects a size");
            bytes().insert(bytes().end(), static_cast<size_t>(n), 0);
            return;
        }
        if (d == ".word") {
            if (ops.empty()) fail(".word expects values");
            for (auto& o : ops) emitWord(o);
            return;
        }
        fail("unknown directive '" + d + "'");
    }

    void put32(uint32_t v) {
        for (int i = 0; i < 4; i++) bytes().push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    void emitWord(const std::string& o) {
        long long v;
        if (parseNumber(o, v)) {
            put32(static_cast<uint32_t>(v));
            return;
        }
        // symbol or symbol+N / symbol-N
        std::string name = o;
        long long addend = 0;
        size_t k = o.find_first_of("+-", 1);
        if (k != std::string::npos) {
            name = trim(o.substr(0, k));
            if (!parseNumber(trim(o.substr(k)), addend)) fail("bad expression '" + o + "'");
        }
        if (name.empty() || !isIdentStart(name[0])) fail("bad .word operand '" + o + "'");
        Reloc r;
        r.sec = cur_;
        r.offset = static_cast<uint32_t>(bytes().size());
        r.sym = symbol(name);
        r.type = RelType::Abs32;
        r.addend = static_cast<int32_t>(addend);
        obj_.relocs.push_back(r);
        put32(0);
    }

    int reg(const std::string& s) {
        int r = parseReg(s);
        if (r < 0) fail("expected a register, got '" + s + "'");
        return r;
    }

    long long imm(const std::string& s) {
        long long v;
        if (!parseNumber(s, v)) fail("expected a number, got '" + s + "'");
        return v;
    }

    void need(const std::vector<std::string>& ops, size_t n, const std::string& m) {
        if (ops.size() != n) fail("'" + m + "' expects " + std::to_string(n) + " operand(s)");
    }

    void checkImm(Op op, long long v) {
        switch (op) {
            case ADDI: case SLTI: case SLTIU: case LW: case SW: case JALR:
                if (v < -32768 || v > 32767) fail("immediate " + std::to_string(v) + " out of range for '" + mnemonic(op) + "' (-32768..32767)");
                break;
            case ANDI: case ORI: case XORI: case LUI:
                if (v < 0 || v > 65535) fail("immediate " + std::to_string(v) + " out of range for '" + mnemonic(op) + "' (0..65535)");
                break;
            case SLLI: case SRLI: case SRAI:
                if (v < 0 || v > 31) fail("shift amount " + std::to_string(v) + " out of range (0..31)");
                break;
            case ECALL:
                if (v < 0 || v > 65535) fail("ecall number out of range");
                break;
            default: break;
        }
    }

    void emit(Instr in) {
        if (cur_ != Sec::Text) fail("instructions must be in .text");
        put32(encode(in));
    }

    void emitRI(Op op, int rd, int rs1, long long v) {
        checkImm(op, v);
        Instr in;
        in.op = op; in.rd = rd; in.rs1 = rs1; in.imm = static_cast<int32_t>(v);
        emit(in);
    }

    void addFixup(Fixup::Kind k, const std::string& sym) {
        if (sym.empty() || !isIdentStart(sym[0])) fail("expected a label, got '" + sym + "'");
        symbol(sym);
        fixups_.push_back({k, static_cast<uint32_t>(obj_.text.size()), sym, line_});
    }

    void parseMem(const std::string& s, int& base, long long& off) {
        size_t lp = s.find('(');
        size_t rp = s.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp < lp) fail("expected offset(reg), got '" + s + "'");
        std::string o = trim(s.substr(0, lp));
        off = o.empty() ? 0 : imm(o);
        base = reg(trim(s.substr(lp + 1, rp - lp - 1)));
    }

    void instruction(const std::string& m, const std::vector<std::string>& ops) {
        // ---- pseudo instructions ----
        if (m == "nop") { need(ops, 0, m); emitRI(ADDI, 0, 0, 0); return; }
        if (m == "mv") { need(ops, 2, m); emitRI(ADDI, reg(ops[0]), reg(ops[1]), 0); return; }
        if (m == "neg") {
            need(ops, 2, m);
            Instr in; in.op = SUB; in.rd = reg(ops[0]); in.rs1 = 0; in.rs2 = reg(ops[1]);
            emit(in);
            return;
        }
        if (m == "li") {
            need(ops, 2, m);
            int rd = reg(ops[0]);
            long long v = imm(ops[1]);
            if (v < -2147483648LL || v > 4294967295LL) fail("li immediate out of 32-bit range");
            int32_t s = static_cast<int32_t>(static_cast<uint32_t>(v));
            if (s >= -32768 && s <= 32767) {
                emitRI(ADDI, rd, 0, s);
            } else {
                uint32_t u = static_cast<uint32_t>(s);
                emitRI(LUI, rd, 0, (u >> 16) & 0xFFFF);
                if (u & 0xFFFF) emitRI(ORI, rd, rd, u & 0xFFFF);
            }
            return;
        }
        if (m == "la") {
            need(ops, 2, m);
            int rd = reg(ops[0]);
            if (cur_ != Sec::Text) fail("instructions must be in .text");
            addFixup(Fixup::Hi, ops[1]);
            emitRI(LUI, rd, 0, 0);
            addFixup(Fixup::Lo, ops[1]);
            emitRI(ORI, rd, rd, 0);
            return;
        }
        if (m == "j" || m == "call") {
            need(ops, 1, m);
            if (cur_ != Sec::Text) fail("instructions must be in .text");
            addFixup(Fixup::Jal, ops[0]);
            Instr in; in.op = JAL; in.rd = (m == "call") ? REG_RA : REG_ZERO;
            emit(in);
            return;
        }
        if (m == "ret") {
            need(ops, 0, m);
            Instr in; in.op = JALR; in.rd = 0; in.rs1 = REG_RA; in.imm = 0;
            emit(in);
            return;
        }
        if (m == "beqz" || m == "bnez") {
            need(ops, 2, m);
            if (cur_ != Sec::Text) fail("instructions must be in .text");
            int r = reg(ops[0]);
            addFixup(Fixup::Branch, ops[1]);
            Instr in; in.op = (m == "beqz") ? BEQ : BNE; in.rs1 = r; in.rs2 = 0;
            emit(in);
            return;
        }
        if (m == "bgt" || m == "ble") {
            need(ops, 3, m);
            if (cur_ != Sec::Text) fail("instructions must be in .text");
            int a = reg(ops[0]), b = reg(ops[1]);
            addFixup(Fixup::Branch, ops[2]);
            Instr in; in.op = (m == "bgt") ? BLT : BGE; in.rs1 = b; in.rs2 = a;
            emit(in);
            return;
        }

        // ---- real instructions ----
        Op op;
        if (!lookupOp(m, op)) fail("unknown instruction '" + m + "'");
        switch (format(op)) {
            case Fmt::R: {
                need(ops, 3, m);
                Instr in; in.op = op; in.rd = reg(ops[0]); in.rs1 = reg(ops[1]); in.rs2 = reg(ops[2]);
                emit(in);
                break;
            }
            case Fmt::I:
                if (op == LW) {
                    need(ops, 2, m);
                    int base; long long off;
                    parseMem(ops[1], base, off);
                    emitRI(op, reg(ops[0]), base, off);
                } else if (op == LUI) {
                    need(ops, 2, m);
                    emitRI(op, reg(ops[0]), 0, imm(ops[1]));
                } else {
                    need(ops, 3, m);
                    emitRI(op, reg(ops[0]), reg(ops[1]), imm(ops[2]));
                }
                break;
            case Fmt::S: {
                need(ops, 2, m);
                int base; long long off;
                parseMem(ops[1], base, off);
                checkImm(op, off);
                Instr in; in.op = op; in.rs2 = reg(ops[0]); in.rs1 = base; in.imm = static_cast<int32_t>(off);
                emit(in);
                break;
            }
            case Fmt::B: {
                need(ops, 3, m);
                if (cur_ != Sec::Text) fail("instructions must be in .text");
                int a = reg(ops[0]), b = reg(ops[1]);
                addFixup(Fixup::Branch, ops[2]);
                Instr in; in.op = op; in.rs1 = a; in.rs2 = b;
                emit(in);
                break;
            }
            case Fmt::J: {
                need(ops, 2, m);
                if (cur_ != Sec::Text) fail("instructions must be in .text");
                int rd = reg(ops[0]);
                addFixup(Fixup::Jal, ops[1]);
                Instr in; in.op = op; in.rd = rd;
                emit(in);
                break;
            }
            case Fmt::Sys:
                need(ops, 1, m);
                checkImm(op, imm(ops[0]));
                emitRI(op, 0, 0, imm(ops[0]));
                break;
        }
        if (op == JALR && ops.size() != 3) fail("'jalr' expects rd, rs1, imm");
    }

    void patch(uint32_t off, uint32_t mask, uint32_t value) {
        uint32_t w = 0;
        for (int i = 0; i < 4; i++) w |= static_cast<uint32_t>(obj_.text[off + i]) << (8 * i);
        w = (w & ~mask) | (value & mask);
        for (int i = 0; i < 4; i++) obj_.text[off + i] = static_cast<uint8_t>(w >> (8 * i));
    }

    void resolve() {
        for (auto& f : fixups_) {
            uint32_t si = symIdx_.at(f.sym);
            const Symbol& s = obj_.syms[si];
            bool localText = (s.sec == Sec::Text);
            switch (f.kind) {
                case Fixup::Branch: {
                    if (!localText) fail("branch target '" + f.sym + "' must be a label defined in this file's .text", f.line);
                    long long d = (static_cast<long long>(s.value) - f.off) / 4;
                    if (d < -32768 || d > 32767) fail("branch to '" + f.sym + "' is out of range", f.line);
                    patch(f.off, 0xFFFF, static_cast<uint32_t>(d));
                    break;
                }
                case Fixup::Jal: {
                    if (localText) {
                        long long d = (static_cast<long long>(s.value) - f.off) / 4;
                        patch(f.off, 0x1FFFFF, static_cast<uint32_t>(d));
                    } else {
                        obj_.relocs.push_back({Sec::Text, f.off, si, RelType::Jal21, 0});
                    }
                    break;
                }
                case Fixup::Hi:
                    obj_.relocs.push_back({Sec::Text, f.off, si, RelType::Hi16, 0});
                    break;
                case Fixup::Lo:
                    obj_.relocs.push_back({Sec::Text, f.off, si, RelType::Lo16, 0});
                    break;
            }
        }
    }
};

}  // namespace

Object assemble(const std::string& source, const std::string& filename) {
    Assembler a(filename);
    return a.run(source);
}

}  // namespace r32
