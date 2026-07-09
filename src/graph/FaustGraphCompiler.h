#pragma once

#include "GraphTypes.h"
#include <string>

namespace OrchFaust {

class FaustGraphCompiler {
public:
    static std::string compile(const Graph& graph, std::string& errorMsg);
};

} // namespace OrchFaust
