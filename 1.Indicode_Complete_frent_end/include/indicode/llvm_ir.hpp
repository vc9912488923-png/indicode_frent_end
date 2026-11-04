#pragma once

#include "indicode/hir.hpp"

#include <string>
#include <vector>

namespace indicode {

struct LlvmIrResult {
    std::string ir;
    std::vector<Diagnostic> diagnostics;
};

LlvmIrResult generate_llvm_ir(const HirProgram& program);

}