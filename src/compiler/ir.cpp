#include "ir.hpp"

#include <climits>
#include <sstream>
#include <unordered_map>

namespace mcc {
namespace ir {

bool Func::hasCall() const {
    for (auto& i : code)
        if (i.op == Op::Call) return true;
    return false;
}

bool evalBop(Bop op, int32_t a, int32_t b, int32_t& out) {
    uint32_t ua = static_cast<uint32_t>(a), ub = static_cast<uint32_t>(b);
    switch (op) {
        case Bop::Add: out = static_cast<int32_t>(ua + ub); return true;
        case Bop::Sub: out = static_cast<int32_t>(ua - ub); return true;
        case Bop::Mul: out = static_cast<int32_t>(ua * ub); return true;
        case Bop::Div:
            if (b == 0) return false;
            out = (a == INT_MIN && b == -1) ? INT_MIN : a / b;
            return true;
        case Bop::Rem:
            if (b == 0) return false;
            out = (a == INT_MIN && b == -1) ? 0 : a % b;
            return true;
        case Bop::And: out = a & b; return true;
        case Bop::Or: out = a | b; return true;
        case Bop::Xor: out = a ^ b; return true;
        case Bop::Shl: out = static_cast<int32_t>(ua << (ub & 31)); return true;
        case Bop::Shr: out = a >> (ub & 31); return true;
        case Bop::Lt: out = a < b; return true;
        case Bop::Le: out = a <= b; return true;
        case Bop::Gt: out = a > b; return true;
        case Bop::Ge: out = a >= b; return true;
        case Bop::Eq: out = a == b; return true;
        case Bop::Ne: out = a != b; return true;
    }
    return false;
}

bool isCommutative(Bop op) {
    return op == Bop::Add || op == Bop::Mul || op == Bop::And || op == Bop::Or || op == Bop::Xor ||
           op == Bop::Eq || op == Bop::Ne;
}

bool isCompare(Bop op) { return op >= Bop::Lt; }

Bop invertCompare(Bop op) {
    switch (op) {
        case Bop::Lt: return Bop::Ge;
        case Bop::Ge: return Bop::Lt;
        case Bop::Gt: return Bop::Le;
        case Bop::Le: return Bop::Gt;
        case Bop::Eq: return Bop::Ne;
        case Bop::Ne: return Bop::Eq;
        default: return op;
    }
}

int defOf(const Inst& in) {
    switch (in.op) {
        case Op::Mov: case Op::Bin: case Op::Neg: case Op::BNot: case Op::Load: case Op::AddrG: case Op::AddrL:
            return in.dst;
        case Op::Call:
            return in.dst;
        default:
            return -1;
    }
}

void usesOf(const Inst& in, std::vector<int>& out) {
    out.clear();
    auto add = [&](const Operand& o) { if (o.isReg()) out.push_back(o.v); };
    switch (in.op) {
        case Op::Mov: case Op::Neg: case Op::BNot: case Op::Load: case Op::Ret: add(in.a); break;
        case Op::Bin: case Op::CmpBr: case Op::Store: add(in.a); add(in.b); break;
        case Op::Call: for (auto& a : in.args) add(a); break;
        default: break;
    }
}

bool isPure(const Inst& in) {
    switch (in.op) {
        case Op::Mov: case Op::Bin: case Op::Neg: case Op::BNot: case Op::Load: case Op::AddrG: case Op::AddrL:
            return true;
        default:
            return false;
    }
}

std::string labelName(const Func& f, int id) { return ".L" + f.name + "_" + std::to_string(id); }

static std::string opnd(const Operand& o) {
    if (o.isReg()) return "v" + std::to_string(o.v);
    if (o.isImm()) return std::to_string(o.v);
    return "_";
}

static const char* bopName(Bop b) {
    static const char* n[] = {"add", "sub", "mul", "div", "rem", "and", "or", "xor", "shl", "shr", "lt", "le", "gt", "ge", "eq", "ne"};
    return n[static_cast<int>(b)];
}

std::string toString(const Func& f) {
    std::ostringstream s;
    s << "func " << f.name << "(" << f.nparams << " params, " << f.nvregs << " vregs)\n";
    for (auto& i : f.code) {
        switch (i.op) {
            case Op::Nop: s << "    nop\n"; break;
            case Op::Mov: s << "    v" << i.dst << " = " << opnd(i.a) << "\n"; break;
            case Op::Bin: s << "    v" << i.dst << " = " << bopName(i.bop) << " " << opnd(i.a) << ", " << opnd(i.b) << "\n"; break;
            case Op::Neg: s << "    v" << i.dst << " = neg " << opnd(i.a) << "\n"; break;
            case Op::BNot: s << "    v" << i.dst << " = not " << opnd(i.a) << "\n"; break;
            case Op::Load: s << "    v" << i.dst << " = load [" << opnd(i.a) << "]\n"; break;
            case Op::Store: s << "    store [" << opnd(i.a) << "], " << opnd(i.b) << "\n"; break;
            case Op::AddrG: s << "    v" << i.dst << " = &" << i.sym << "\n"; break;
            case Op::AddrL: s << "    v" << i.dst << " = &frame" << i.label << "\n"; break;
            case Op::Label: s << labelName(f, i.label) << ":\n"; break;
            case Op::Jmp: s << "    jmp " << labelName(f, i.label) << "\n"; break;
            case Op::CmpBr: s << "    if " << opnd(i.a) << " " << bopName(i.bop) << " " << opnd(i.b) << " goto " << labelName(f, i.label) << "\n"; break;
            case Op::Call:
                s << "    ";
                if (i.dst >= 0) s << "v" << i.dst << " = ";
                s << "call " << i.sym << "(";
                for (size_t k = 0; k < i.args.size(); k++) s << (k ? ", " : "") << opnd(i.args[k]);
                s << ")\n";
                break;
            case Op::Ret: s << "    ret" << (i.a.k == Operand::None ? "" : " " + opnd(i.a)) << "\n"; break;
        }
    }
    return s.str();
}

CFG buildCFG(const Func& f) {
    CFG g;
    const int n = static_cast<int>(f.code.size());
    std::vector<bool> leader(n + 1, false);
    leader[0] = true;
    for (int i = 0; i < n; i++) {
        Op op = f.code[i].op;
        if (op == Op::Label) leader[i] = true;
        if (op == Op::Jmp || op == Op::CmpBr || op == Op::Ret) leader[i + 1] = true;
    }
    std::vector<int> blockOf(n, -1);
    for (int i = 0; i < n;) {
        int j = i + 1;
        while (j < n && !leader[j]) j++;
        Block b;
        b.start = i; b.end = j;
        for (int k = i; k < j; k++) blockOf[k] = static_cast<int>(g.blocks.size());
        g.blocks.push_back(std::move(b));
        i = j;
    }
    std::unordered_map<int, int> labelBlock;
    for (int i = 0; i < n; i++)
        if (f.code[i].op == Op::Label) labelBlock[f.code[i].label] = blockOf[i];
    const int nb = static_cast<int>(g.blocks.size());
    for (int bi = 0; bi < nb; bi++) {
        Block& b = g.blocks[bi];
        const Inst& last = f.code[b.end - 1];
        bool fall = true;
        if (last.op == Op::Jmp) {
            b.succ.push_back(labelBlock.at(last.label));
            fall = false;
        } else if (last.op == Op::CmpBr) {
            b.succ.push_back(labelBlock.at(last.label));
        } else if (last.op == Op::Ret) {
            fall = false;
        }
        if (fall && bi + 1 < nb) b.succ.push_back(bi + 1);
    }
    // Liveness (backward dataflow).
    const int nv = f.nvregs;
    std::vector<int> u;
    for (auto& b : g.blocks) {
        b.use.assign(nv, false); b.def.assign(nv, false); b.in.assign(nv, false); b.out.assign(nv, false);
        for (int i = b.start; i < b.end; i++) {
            usesOf(f.code[i], u);
            for (int r : u) if (!b.def[r]) b.use[r] = true;
            int d = defOf(f.code[i]);
            if (d >= 0) b.def[d] = true;
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int bi = nb - 1; bi >= 0; bi--) {
            Block& b = g.blocks[bi];
            for (int v = 0; v < nv; v++) {
                bool o = false;
                for (int s : b.succ) if (g.blocks[s].in[v]) { o = true; break; }
                bool in = b.use[v] || (o && !b.def[v]);
                if (o != b.out[v] || in != b.in[v]) { b.out[v] = o; b.in[v] = in; changed = true; }
            }
        }
    }
    return g;
}

}  // namespace ir
}  // namespace mcc
