// Three-address intermediate representation used by the optimizer and back end.
// Local scalar variables live in virtual registers (not SSA: a vreg may be assigned
// more than once). Control flow is expressed with labels and (conditional) jumps.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mcc {
namespace ir {

struct Operand {
    enum Kind { None, Reg, Imm } k = None;
    int32_t v = 0;
    static Operand reg(int r) { Operand o; o.k = Reg; o.v = r; return o; }
    static Operand imm(int32_t i) { Operand o; o.k = Imm; o.v = i; return o; }
    bool isReg() const { return k == Reg; }
    bool isImm() const { return k == Imm; }
    bool operator==(const Operand& o) const { return k == o.k && v == o.v; }
};

enum class Op {
    Nop,
    Mov,     // dst = a
    Bin,     // dst = a bop b
    Neg,     // dst = -a
    BNot,    // dst = ~a
    Load,    // dst = *(int*)a
    Store,   // *(int*)a = b
    AddrG,   // dst = &global(sym)
    AddrL,   // dst = &frame_object(label index in `label` field)
    Label,   // label:
    Jmp,     // goto label
    CmpBr,   // if (a bop b) goto label
    Call,    // [dst =] sym(args...)
    Ret      // return [a]
};

enum class Bop { Add, Sub, Mul, Div, Rem, And, Or, Xor, Shl, Shr, Lt, Le, Gt, Ge, Eq, Ne };

struct Inst {
    Op op = Op::Nop;
    Bop bop = Bop::Add;
    int dst = -1;
    Operand a, b;
    int label = -1;                 // Label / Jmp / CmpBr target; AddrL frame object index
    std::string sym;                // AddrG / Call
    std::vector<Operand> args;      // Call
};

struct Func {
    std::string name;
    int nparams = 0;                // parameters occupy vregs 0 .. nparams-1
    int nvregs = 0;
    int nlabels = 0;
    std::vector<int> frameObjs;     // sizes (in words) of local arrays
    std::vector<Inst> code;
    bool hasCall() const;
};

struct Global {
    std::string name;
    int words = 1;
    std::vector<int32_t> init;
};

struct Module {
    std::vector<Func> funcs;
    std::vector<Global> globals;
};

// Helpers shared by the optimizer and the back end.
bool evalBop(Bop op, int32_t a, int32_t b, int32_t& out);   // false if it cannot be folded
bool isCommutative(Bop op);
bool isCompare(Bop op);
Bop invertCompare(Bop op);
int defOf(const Inst& in);                                  // defined vreg or -1
void usesOf(const Inst& in, std::vector<int>& out);         // vregs read
bool isPure(const Inst& in);                                // removable if the result is unused
std::string toString(const Func& f);
std::string labelName(const Func& f, int id);

// Control-flow graph + liveness.
struct Block {
    int start = 0, end = 0;               // [start, end) instruction indices
    std::vector<int> succ;
    std::vector<bool> use, def, in, out;  // indexed by vreg
};
struct CFG {
    std::vector<Block> blocks;
};
CFG buildCFG(const Func& f);   // includes liveness

}  // namespace ir
}  // namespace mcc
