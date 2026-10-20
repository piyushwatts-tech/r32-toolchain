#pragma once
#include <stdexcept>
#include <string>

namespace mcc {
struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[noreturn]] inline void error(const std::string& file, int line, const std::string& msg) {
    throw CompileError(file + ":" + std::to_string(line) + ": error: " + msg);
}
}  // namespace mcc
