#pragma once

#include "indicode/ast.hpp"
#include "indicode/diagnostic.hpp"

#include <vector>

namespace indicode {

struct EffectCheckResult {
    std::vector<Diagnostic> diagnostics;
};

class EffectChecker {
public:
    EffectCheckResult check(Program& program) const;
};

}