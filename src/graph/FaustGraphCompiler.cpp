#include "FaustGraphCompiler.h"
#include "util/Logging.h"
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <queue>
#include <algorithm>

namespace OrchFaust {

struct ParamRange {
    float minVal;
    float maxVal;
    float step;
};

ParamRange getParamRange(const std::string& nodeType, const std::string& paramName) {
    if (paramName == "freq") {
        if (nodeType == "lfo") {
            return {0.01f, 100.0f, 0.01f};
        }
        return {20.0f, 10000.0f, 0.1f};
    }
    if (paramName == "keyboard_tracking") return {0.0f, 1.0f, 1.0f};
    if (paramName == "cutoff") return {20.0f, 20000.0f, 1.0f};
    if (paramName == "resonance") return {0.1f, 20.0f, 0.01f};
    if (paramName == "gain") return {0.0f, 20000.0f, 0.1f};
    if (paramName == "attack" || paramName == "decay" || paramName == "release") return {0.001f, 10.0f, 0.001f};
    if (paramName == "sustain") return {0.0f, 1.0f, 0.01f};
    if (paramName == "delay") return {0.0f, 5.0f, 0.001f};
    if (paramName == "damping") return {0.0f, 10.0f, 0.01f};
    if (paramName == "q") return {1.0f, 1000.0f, 1.0f};
    if (paramName == "size" || paramName == "brightness" || paramName == "damp") return {0.0f, 1.0f, 0.01f};
    return {0.0f, 10000.0f, 0.01f};
}

// Topological sort helper
std::vector<std::string> topologicalSort(const Graph& graph) {
    std::set<std::string> nodeIds;
    std::map<std::string, int> inDegree;
    std::map<std::string, std::vector<std::string>> adjList;
    
    for (const auto& node : graph.nodes) {
        nodeIds.insert(node.id);
        inDegree[node.id] = 0;
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
    
    std::vector<std::string> sorted;
    while (!q.empty()) {
        std::string u = q.front();
        q.pop();
        sorted.push_back(u);
        
        for (const auto& v : adjList[u]) {
            inDegree[v]--;
            if (inDegree[v] == 0) {
                q.push(v);
            }
        }
    }
    
    return sorted;
}

std::string FaustGraphCompiler::compile(const Graph& graph, std::string& errorMsg) {
    if (graph.nodes.empty()) {
        errorMsg = "Empty graph";
        return "";
    }

    std::vector<std::string> sortedNodeIds = topologicalSort(graph);
    if (sortedNodeIds.size() != graph.nodes.size()) {
        errorMsg = "Graph sorting failed (likely has cycles)";
        return "";
    }

    std::stringstream ss;
    
    // 1. Imports
    ss << "import(\"stdfaust.lib\");\n\n";
    
    // 2. Global Voice Parameters
    ss << "// Global Voice Controls\n";
    ss << "voice_freq = hslider(\"freq\", 440, 20, 20000, 0.01);\n";
    ss << "voice_gain = hslider(\"gain\", 1.0, 0, 1, 0.01);\n";
    ss << "voice_gate = hslider(\"gate\", 0, 0, 1, 1);\n\n";
    
    // Map of nodes for quick lookup
    std::map<std::string, const Node*> nodeMap;
    for (const auto& node : graph.nodes) {
        nodeMap[node.id] = &node;
    }
    
    // 3. Emit local UI sliders for parameters
    ss << "// Node Parameters\n";
    for (const auto& node : graph.nodes) {
        ss << "// Parameters for " << node.id << " (" << node.type << ")\n";
        for (const auto& [paramName, value] : node.params) {
            float val = value;
            ParamRange range = getParamRange(node.type, paramName);
            
            // Generate slider definition
            ss << "param_" << node.id << "_" << paramName << " = hslider(\"" 
               << node.id << "/" << paramName << "\", " 
               << val << ", " << range.minVal << ", " << range.maxVal << ", " << range.step << ");\n";
        }
    }
    ss << "\n";
    
    // 4. Map connections (target -> sources)
    std::map<std::string, std::vector<std::string>> incomingConnections;
    std::map<std::string, std::map<std::string, std::vector<std::string>>> parameterModulations;
    
    for (const auto& conn : graph.connections) {
        if (!conn.targetHandle.empty() && conn.targetHandle.rfind("param-", 0) == 0) {
            std::string paramName = conn.targetHandle.substr(6); // strip "param-"
            parameterModulations[conn.target][paramName].push_back(conn.source);
        } else {
            incomingConnections[conn.target].push_back(conn.source);
        }
    }
    
    // 5. Generate node evaluation code
    ss << "// DSP Signal Flow\n";
    for (const auto& nodeId : sortedNodeIds) {
        const Node* nodePtr = nodeMap[nodeId];
        if (!nodePtr) continue;
        const Node& node = *nodePtr;
        
        // Build inputs expression
        std::string inputsExpr = "";
        const auto& inputs = incomingConnections[nodeId];
        if (!inputs.empty()) {
            if (inputs.size() == 1) {
                inputsExpr = "node_" + inputs[0];
            } else {
                std::stringstream inputsSS;
                inputsSS << "(";
                for (size_t i = 0; i < inputs.size(); ++i) {
                    inputsSS << "node_" << inputs[i];
                    if (i < inputs.size() - 1) inputsSS << " + ";
                }
                inputsSS << ")";
                inputsExpr = inputsSS.str();
            }
        }
        
        // Node parameter access macros
        auto getParamExpr = [&](const std::string& name, const std::string& defaultVal) -> std::string {
            std::string baseVal = defaultVal;
            if (node.params.count(name) > 0) {
                baseVal = "param_" + nodeId + "_" + name;
            }
            if (parameterModulations.count(nodeId) > 0 && parameterModulations.at(nodeId).count(name) > 0) {
                const auto& modSources = parameterModulations.at(nodeId).at(name);
                if (!modSources.empty()) {
                    std::stringstream modSS;
                    modSS << "(" << baseVal;
                    for (const auto& modSrc : modSources) {
                        modSS << " + node_" << modSrc;
                    }
                    modSS << ")";
                    ParamRange range = getParamRange(node.type, name);
                    return "min(" + std::to_string(range.maxVal) + ", max(" + std::to_string(range.minVal) + ", " + modSS.str() + "))";
                }
            }
            return baseVal;
        };
        
        ss << "node_" << nodeId << " = ";
        
        if (node.type == "sine" || node.type == "saw" || node.type == "square") {
            bool trackKb = true;
            if (node.params.count("keyboard_tracking") > 0) {
                trackKb = (node.params.at("keyboard_tracking") != 0.0f);
            }
            
            std::string freqExpr;
            if (trackKb) {
                freqExpr = "voice_freq";
                if (node.params.count("freq") > 0) {
                    freqExpr = "voice_freq * (" + getParamExpr("freq", "440.0") + " / 440.0)";
                }
            } else {
                freqExpr = getParamExpr("freq", "440.0");
            }
            
            if (!inputsExpr.empty()) {
                freqExpr = "max(20.0, " + freqExpr + " + (" + inputsExpr + "))";
            }
            
            if (node.type == "sine") {
                ss << "os.osc(" << freqExpr << ")";
            } else if (node.type == "saw") {
                ss << "os.sawtooth(" << freqExpr << ")";
            } else if (node.type == "square") {
                ss << "os.square(" << freqExpr << ")";
            }
        }
        else if (node.type == "noise") {
            ss << "no.noise";
        }
        else if (node.type == "gain") {
            std::string gainExpr = getParamExpr("gain", "voice_gain");
            if (inputsExpr.empty()) {
                ss << "*(0.0)"; // No input
            } else {
                ss << inputsExpr << " * " << gainExpr;
            }
        }
        else if (node.type == "lowpass") {
            std::string cutoffExpr = getParamExpr("cutoff", "2000.0");
            std::string resonanceExpr = getParamExpr("resonance", "0.707");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr << " : fi.resonlp(" << cutoffExpr << ", " << resonanceExpr << ", 1.0)";
            }
        }
        else if (node.type == "highpass") {
            std::string cutoffExpr = getParamExpr("cutoff", "200.0");
            std::string resonanceExpr = getParamExpr("resonance", "0.707");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr << " : fi.resonhp(" << cutoffExpr << ", " << resonanceExpr << ", 1.0)";
            }
        }
        else if (node.type == "delay") {
            std::string delayExpr = getParamExpr("delay", "0.1");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr << " : de.delay(262144, " << delayExpr << " * ma.SR)";
            }
        }
        else if (node.type == "adsr") {
            std::string a = getParamExpr("attack", "0.01");
            std::string d = getParamExpr("decay", "0.1");
            ss << "en.adsr(" << a << ", " << d << ", " 
               << getParamExpr("sustain", "0.5") << ", " 
               << getParamExpr("release", "0.2") << ", voice_gate)";
            if (!inputsExpr.empty()) {
                ss << " * " << inputsExpr;
            }
        }
        else if (node.type == "lfo") {
            std::string freqExpr = getParamExpr("freq", "5.0");
            if (!inputsExpr.empty()) {
                freqExpr = "max(0.01, " + freqExpr + " + (" + inputsExpr + "))";
            }
            ss << "os.osc(" << freqExpr << ")";
        }
        else if (node.type == "mixer") {
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr;
            }
        }
        else if (node.type == "reverb") {
            // Simple comb-filter based feedback reverb (mono)
            std::string sizeExpr = getParamExpr("size", "0.5");
            std::string dampExpr = getParamExpr("damp", "0.5");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                // Use standard mono freeverb
                ss << inputsExpr << " : re.mono_freeverb(" << sizeExpr << ", 0.5, " << dampExpr << ", 0.0)";
            }
        }
        else if (node.type == "karplus_string") {
            bool trackKb = true;
            if (node.params.count("keyboard_tracking") > 0) {
                trackKb = (node.params.at("keyboard_tracking") != 0.0f);
            }
            std::string freqExpr;
            if (trackKb) {
                freqExpr = "voice_freq";
                if (node.params.count("freq") > 0) {
                    freqExpr = "voice_freq * (param_" + nodeId + "_freq / 440.0)";
                }
            } else {
                freqExpr = getParamExpr("freq", "440.0");
            }
            std::string dampingExpr = getParamExpr("damping", "0.9");
            // Native Karplus-Strong implementation: feedback loop with a delay and average filter
            // Triggered by input (noise burst)
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                // Standard Faust physical model string: ks(length, damping, excitation)
                ss << "pm.ks(pm.f2l(" << freqExpr << "), " << dampingExpr << ", " << inputsExpr << ")";
            }
        }
        else if (node.type == "body_resonator") {
            std::string sizeExpr = getParamExpr("size", "0.5");
            std::string brightnessExpr = getParamExpr("brightness", "0.5");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                // Highpass/Lowpass filterbank or simple bandpass resonator
                ss << inputsExpr << " : fi.resonbp(100.0 * (1.0 + " << sizeExpr << " * 10.0), 5.0, 1.0)";
            }
        }
        else if (node.type == "modal_resonator") {
            bool trackKb = true;
            if (node.params.count("keyboard_tracking") > 0) {
                trackKb = (node.params.at("keyboard_tracking") != 0.0f);
            }
            std::string freqExpr;
            if (trackKb) {
                freqExpr = "voice_freq";
                if (node.params.count("freq") > 0) {
                    freqExpr = "voice_freq * (param_" + nodeId + "_freq / 440.0)";
                }
            } else {
                freqExpr = getParamExpr("freq", "440.0");
            }
            std::string qExpr = getParamExpr("q", "100.0");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr << " : fi.resonbp(" << freqExpr << ", " << qExpr << ", 1.0)";
            }
        }
        else {
            // Fallback for unknown nodes
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr;
            }
        }
        ss << ";\n";
    }
    ss << "\n";
    
    // 6. Set main process output (stereo duplicated)
    ss << "// Output\n";
    ss << "process = node_" << graph.outputNodeId << " <: _, _;\n";
    
    return ss.str();
}

} // namespace OrchFaust
