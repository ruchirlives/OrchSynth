#include "GraphParser.h"
#include "util/Logging.h"
#include <nlohmann/json.hpp>
#include <set>
#include <queue>

using json = nlohmann::json;

namespace OrchFaust {

namespace {

Connection parseConnectionJson(const json& jConn) {
    Connection conn;
    bool hasCurrentKeys = jConn.contains("source") && jConn.contains("target");
    conn.source = hasCurrentKeys ? jConn["source"].get<std::string>() : jConn["fromNode"].get<std::string>();
    conn.target = hasCurrentKeys ? jConn["target"].get<std::string>() : jConn["toNode"].get<std::string>();
    if (jConn.contains("sourceHandle") && jConn["sourceHandle"].is_string()) {
        conn.sourceHandle = jConn["sourceHandle"].get<std::string>();
    }
    if (jConn.contains("targetHandle") && jConn["targetHandle"].is_string()) {
        conn.targetHandle = jConn["targetHandle"].get<std::string>();
    } else if (jConn.contains("toInput") && jConn["toInput"].is_number_integer()) {
        conn.targetHandle = "input-" + std::to_string(jConn["toInput"].get<int>());
    }
    if (jConn.contains("operation") && jConn["operation"].is_string()) {
        conn.operation = jConn["operation"].get<std::string>();
    }
    return conn;
}

Node parseNodeJson(const json& jNode);

std::shared_ptr<GroupDefinition> parseGroupJson(const json& jGroup) {
    auto group = std::make_shared<GroupDefinition>();
    if (jGroup.contains("nodes") && jGroup["nodes"].is_array()) {
        for (const auto& child : jGroup["nodes"]) {
            group->nodes.push_back(parseNodeJson(child));
        }
    }
    if (jGroup.contains("connections") && jGroup["connections"].is_array()) {
        for (const auto& childConn : jGroup["connections"]) {
            group->connections.push_back(parseConnectionJson(childConn));
        }
    }
    if (jGroup.contains("inputs") && jGroup["inputs"].is_array()) {
        for (const auto& input : jGroup["inputs"]) {
            GroupInput groupInput;
            groupInput.id = input.value("id", "");
            groupInput.label = input.value("label", groupInput.id);
            groupInput.targetNode = input.value("targetNode", "");
            groupInput.targetHandle = input.value("targetHandle", "input-0");
            group->inputs.push_back(groupInput);
        }
    }
    if (jGroup.contains("outputs") && jGroup["outputs"].is_array()) {
        for (const auto& output : jGroup["outputs"]) {
            GroupOutput groupOutput;
            groupOutput.id = output.value("id", "");
            groupOutput.label = output.value("label", groupOutput.id);
            groupOutput.sourceNode = output.value("sourceNode", "");
            groupOutput.sourceHandle = output.value("sourceHandle", "");
            group->outputs.push_back(groupOutput);
        }
    }
    if (jGroup.contains("promotedParams") && jGroup["promotedParams"].is_array()) {
        for (const auto& param : jGroup["promotedParams"]) {
            PromotedParam promoted;
            promoted.id = param.value("id", "");
            promoted.label = param.value("label", promoted.id);
            promoted.targetNode = param.value("targetNode", "");
            promoted.targetParam = param.value("targetParam", "");
            promoted.defaultValue = param.value("default", 0.0f);
            promoted.minValue = param.value("min", 0.0f);
            promoted.maxValue = param.value("max", 1.0f);
            promoted.step = param.value("step", 0.01f);
            group->promotedParams.push_back(promoted);
        }
    }
    return group;
}

Node parseNodeJson(const json& jNode) {
    Node node;
    node.id = jNode["id"].get<std::string>();
    node.type = jNode["type"].get<std::string>();
    if (jNode.contains("x") && jNode["x"].is_number() &&
        jNode.contains("y") && jNode["y"].is_number()) {
        node.x = jNode["x"].get<float>();
        node.y = jNode["y"].get<float>();
        node.hasPosition = true;
    }

    if (jNode.contains("params") && jNode["params"].is_object()) {
        for (auto& el : jNode["params"].items()) {
            if (el.value().is_number()) {
                node.params[el.key()] = el.value().get<float>();
            }
        }
    }
    if (jNode.contains("group") && jNode["group"].is_object()) {
        node.group = parseGroupJson(jNode["group"]);
    }
    return node;
}

}

std::optional<Graph> GraphParser::parse(const std::string& jsonString, std::string& errorMsg) {
    try {
        json j = json::parse(jsonString);
        
        Graph graph;
        if (j.contains("version")) {
            graph.version = j["version"].get<int>();
        }
        if (j.contains("name")) {
            graph.name = j["name"].get<std::string>();
        }
        
        // Parse Nodes
        if (!j.contains("nodes") || !j["nodes"].is_array()) {
            errorMsg = "Missing or invalid 'nodes' array";
            return std::nullopt;
        }
        
        for (const auto& jNode : j["nodes"]) {
            if (!jNode.contains("id") || !jNode.contains("type")) {
                errorMsg = "Node missing 'id' or 'type'";
                return std::nullopt;
            }
            
            graph.nodes.push_back(parseNodeJson(jNode));
        }
        
        // Parse Connections
        if (!j.contains("connections") || !j["connections"].is_array()) {
            errorMsg = "Missing or invalid 'connections' array";
            return std::nullopt;
        }
        
        for (const auto& jConn : j["connections"]) {
            bool hasCurrentKeys = jConn.contains("source") && jConn.contains("target");
            bool hasLegacyKeys = jConn.contains("fromNode") && jConn.contains("toNode");
            if (!hasCurrentKeys && !hasLegacyKeys) {
                errorMsg = "Connection missing 'source' or 'target'";
                return std::nullopt;
            }
            
            graph.connections.push_back(parseConnectionJson(jConn));
        }
        
        // Parse Output Node
        if (j.contains("output") && j["output"].is_string()) {
            graph.outputNodeId = j["output"].get<std::string>();
        } else {
            for (const auto& node : graph.nodes) {
                if (node.type == "output") {
                    graph.outputNodeId = node.id;
                    break;
                }
            }
        }
        if (graph.outputNodeId.empty()) {
            errorMsg = "Missing or invalid 'output' node specification";
            return std::nullopt;
        }
        
        // Validate Graph
        if (!validate(graph, errorMsg)) {
            return std::nullopt;
        }
        
        return graph;
    } catch (const std::exception& e) {
        errorMsg = std::string("JSON Parse error: ") + e.what();
        return std::nullopt;
    }
}

bool GraphParser::validate(const Graph& graph, std::string& errorMsg) {
    std::set<std::string> nodeIds;
    for (const auto& node : graph.nodes) {
        if (node.id.empty()) {
            errorMsg = "Empty node ID is invalid";
            return false;
        }
        if (nodeIds.count(node.id) > 0) {
            errorMsg = "Duplicate node ID: " + node.id;
            return false;
        }
        nodeIds.insert(node.id);
    }
    
    // Check connections point to valid nodes
    for (const auto& conn : graph.connections) {
        if (nodeIds.count(conn.source) == 0) {
            errorMsg = "Connection source node not found: " + conn.source;
            return false;
        }
        if (nodeIds.count(conn.target) == 0) {
            errorMsg = "Connection target node not found: " + conn.target;
            return false;
        }
    }
    
    // Check output node exists
    if (nodeIds.count(graph.outputNodeId) == 0) {
        errorMsg = "Output node not found: " + graph.outputNodeId;
        return false;
    }
    
    // Check for cycles (DAG validation) using Kahn's algorithm
    std::map<std::string, int> inDegree;
    std::map<std::string, std::vector<std::string>> adjList;
    
    for (const auto& id : nodeIds) {
        inDegree[id] = 0;
    }
    
    for (const auto& conn : graph.connections) {
        adjList[conn.source].push_back(conn.target);
        inDegree[conn.target]++;
    }
    
    std::queue<std::string> q;
    for (const auto& id : nodeIds) {
        if (inDegree[id] == 0) {
            q.push(id);
        }
    }
    
    int visitedCount = 0;
    while (!q.empty()) {
        std::string u = q.front();
        q.pop();
        visitedCount++;
        
        for (const auto& v : adjList[u]) {
            inDegree[v]--;
            if (inDegree[v] == 0) {
                q.push(v);
            }
        }
    }
    
    if (visitedCount != graph.nodes.size()) {
        errorMsg = "Graph contains cycles (not a Directed Acyclic Graph)";
        return false;
    }
    
    return true;
}

} // namespace OrchFaust
