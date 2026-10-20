#pragma once
#include "ir.hpp"

namespace mcc {
// -O1 pipeline: constant folding and propagation, algebraic simplification,
// strength reduction, copy propagation, local common-subexpression elimination,
// dead-code elimination (global liveness), jump threading and branch cleanup.
// Returns the number of pipeline iterations that changed the code.
int optimize(ir::Func& f);
}  // namespace mcc
