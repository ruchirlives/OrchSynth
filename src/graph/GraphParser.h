#pragma once

#include "GraphTypes.h"
#include <string>
#include <optional>

namespace OrchFaust {

class GraphParser {
public:
    static std::optional<Graph> parse(const std::string& jsonString, std::string& errorMsg);
    static bool validate(const Graph& graph, std::string& errorMsg);
};

} // namespace OrchFaust
