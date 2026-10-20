#pragma once
#include <string>

#include "ast.hpp"
#include "ir.hpp"

namespace mcc {
// Semantic analysis + lowering of the AST to IR. Throws CompileError.
ir::Module generate(const Program& prog, const std::string& file);
}  // namespace mcc
