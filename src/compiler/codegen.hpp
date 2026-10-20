#pragma once
#include <string>

#include "ir.hpp"

namespace mcc {
// Register allocation (linear scan) and R32 assembly emission.
std::string emitAssembly(const ir::Module& m, const std::string& sourceName);
}  // namespace mcc
