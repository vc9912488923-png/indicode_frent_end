#pragma once

#include "indicode/ast.hpp"
#include "indicode/diagnostic.hpp"

#include <vector>

namespace indicode {

struct OwnershipCheckResult {
    std::vector<Diagnostic> diagnostics;
};

class OwnershipChecker {
public:
    OwnershipCheckResult check(const Program& program) const;
};

}