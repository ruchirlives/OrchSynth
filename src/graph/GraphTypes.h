#pragma once

#include <string>
#include <vector>
#include <map>

namespace OrchFaust {

struct Node {
    std::string id;
    std::string type;
    std::map<std::string, float> params;
};

struct Connection {
    std::string source;
    std::string target;
    std::string sourceHandle;
    std::string targetHandle;
};

struct Graph {
    int version = 1;
    std::string name;
    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::string outputNodeId;
};

} // namespace OrchFaust
