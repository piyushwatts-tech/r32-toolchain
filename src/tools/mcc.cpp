// mcc: compiler for MiniC (a C subset) targeting R32 assembly.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "compiler/ast.hpp"
#include "compiler/codegen.hpp"
#include "compiler/common.hpp"
#include "compiler/irgen.hpp"
#include "compiler/lexer.hpp"
#include "compiler/opt.hpp"

int main(int argc, char** argv) {
    std::string in, out;
    int optLevel = 1;
    bool emitIr = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "-O0") optLevel = 0;
        else if (a == "-O1" || a == "-O") optLevel = 1;
        else if (a == "--emit-ir") emitIr = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "mcc: unknown option %s\n", a.c_str()); return 2; }
        else in = a;
    }
    if (in.empty()) {
        std::fprintf(stderr, "usage: mcc [-O0|-O1] [--emit-ir] input.c [-o output.s]\n");
        return 2;
    }
    try {
        std::ifstream f(in);
        if (!f) throw mcc::CompileError("cannot open '" + in + "'");
        std::stringstream ss;
        ss << f.rdbuf();
        mcc::Program prog = mcc::parse(mcc::lex(ss.str(), in), in);
        mcc::ir::Module mod = mcc::generate(prog, in);
        if (optLevel >= 1)
            for (auto& fn : mod.funcs) mcc::optimize(fn);
        std::string text;
        if (emitIr) {
            for (auto& fn : mod.funcs) text += mcc::ir::toString(fn) + "\n";
        } else {
            text = mcc::emitAssembly(mod, in);
        }
        if (out.empty()) std::fputs(text.c_str(), stdout);
        else {
            std::ofstream o(out);
            if (!o) throw mcc::CompileError("cannot write '" + out + "'");
            o << text;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mcc: %s\n", e.what());
        return 1;
    }
    return 0;
}
