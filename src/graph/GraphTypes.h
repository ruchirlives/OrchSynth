#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace OrchFaust {

struct GroupDefinition;

struct Node {
    std::string id;
    std::string type;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    bool hasPosition = false;
    std::map<std::string, float> params;
    std::shared_ptr<GroupDefinition> group;
};

struct Connection {
    std::string source;
    std::string target;
    std::string sourceHandle;
    std::string targetHandle;
    std::string operation;
};

struct GroupInput {
    std::string id;
    std::string label;
    std::string targetNode;
    std::string targetHandle;
};

struct GroupOutput {
    std::string id;
    std::string label;
    std::string sourceNode;
    std::string sourceHandle;
};

struct PromotedParam {
    std::string id;
    std::string label;
    std::string targetNode;
    std::string targetParam;
    float defaultValue = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.01f;
};

struct GroupDefinition {
    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::vector<GroupInput> inputs;
    std::vector<GroupOutput> outputs;
    std::vector<PromotedParam> promotedParams;
};

struct Graph {
    int version = 1;
    std::string name;
    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::string outputNodeId;
};

} // namespace OrchFaust
