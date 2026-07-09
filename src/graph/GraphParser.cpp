#include "GraphParser.h"
#include "util/Logging.h"
#include <nlohmann/json.hpp>
#include <set>
#include <queue>

using json = nlohmann::json;

namespace OrchFaust {

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
            
            Node node;
            node.id = jNode["id"].get<std::string>();
            node.type = jNode["type"].get<std::string>();
            
            if (jNode.contains("params") && jNode["params"].is_object()) {
                for (auto& el : jNode["params"].items()) {
                    if (el.value().is_number()) {
                        node.params[el.key()] = el.value().get<float>();
                    }
                }
            }
            graph.nodes.push_back(node);
        }
        
        // Parse Connections
        if (!j.contains("connections") || !j["connections"].is_array()) {
            errorMsg = "Missing or invalid 'connections' array";
            return std::nullopt;
        }
        
        for (const auto& jConn : j["connections"]) {
            if (!jConn.contains("source") || !jConn.contains("target")) {
                errorMsg = "Connection missing 'source' or 'target'";
                return std::nullopt;
            }
            
            Connection conn;
            conn.source = jConn["source"].get<std::string>();
            conn.target = jConn["target"].get<std::string>();
            if (jConn.contains("sourceHandle") && jConn["sourceHandle"].is_string()) {
                conn.sourceHandle = jConn["sourceHandle"].get<std::string>();
            }
            if (jConn.contains("targetHandle") && jConn["targetHandle"].is_string()) {
                conn.targetHandle = jConn["targetHandle"].get<std::string>();
            }
            graph.connections.push_back(conn);
        }
        
        // Parse Output Node
        if (!j.contains("output") || !j["output"].is_string()) {
            errorMsg = "Missing or invalid 'output' node specification";
            return std::nullopt;
        }
        graph.outputNodeId = j["output"].get<std::string>();
        
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
