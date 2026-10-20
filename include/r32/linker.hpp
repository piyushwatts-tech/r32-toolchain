#pragma once
#include <string>
#include <vector>

#include "r32/object.hpp"

namespace r32 {

struct LinkInput {
    std::string name;
    Object obj;
};

// Links objects into an executable: lays out .text then .data, resolves
// symbols and applies relocations. Throws r32::Error with ld-style messages.
// If `map` is non-null, a human-readable link map is written to it.
Executable link(const std::vector<LinkInput>& inputs, const std::string& entry, std::string* map);

}  // namespace r32
