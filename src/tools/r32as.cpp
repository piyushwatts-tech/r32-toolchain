// r32as: assembler  (input.s -> output.o)
#include <cstdio>
#include <string>

#include "r32/assembler.hpp"
#include "r32/isa.hpp"

int main(int argc, char** argv) {
    std::string in, out;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else in = a;
    }
    if (in.empty()) {
        std::fprintf(stderr, "usage: r32as input.s [-o output.o]\n");
        return 2;
    }
    if (out.empty()) out = in.substr(0, in.rfind('.')) + ".o";
    try {
        r32::Object o = r32::assemble(r32::readTextFile(in), in);
        r32::writeFile(out, r32::serialize(o));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "r32as: %s\n", e.what());
        return 1;
    }
    return 0;
}
