#pragma once
#include <string>

#include "r32/object.hpp"

namespace r32 {

// Assembles R32 assembly text into a relocatable object. Throws r32::Error with
// "file:line: message" diagnostics.
Object assemble(const std::string& source, const std::string& filename);

}  // namespace r32
