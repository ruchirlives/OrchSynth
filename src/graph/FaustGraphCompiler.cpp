#include "FaustGraphCompiler.h"
#include "util/Logging.h"
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <queue>
#include <algorithm>
#include <optional>

namespace OrchFaust {

struct ParamRange {
    float minVal;
    float maxVal;
    float step;
};

ParamRange getParamRange(const std::string& nodeType, const std::string& paramName) {
    // Node-specific overrides for feedback-based nodes that need tighter ranges
    if (nodeType == "resonator_reverb_loop") {
        if (paramName == "gain") return {0.0f, 5.0f, 0.01f};
        if (paramName == "q") return {1.0f, 100.0f, 1.0f};
    }
    if (nodeType == "sympathetic_strings") {
        if (paramName == "gain") return {0.0f, 5.0f, 0.01f};
        if (paramName == "q") return {1.0f, 80.0f, 1.0f};
        if (paramName == "sym_decay") return {0.1f, 20.0f, 0.01f};
        if (paramName == "sym_coupling") return {0.0f, 2.0f, 0.01f};
        if (paramName == "jawari") return {0.0f, 1.0f, 0.01f};
        if (paramName == "bridge_tone") return {0.0f, 1.0f, 0.01f};
        if (paramName == "tuning_preset") return {0.0f, 4.0f, 1.0f};
        if (paramName == "sym_feedback") return {0.0f, 0.75f, 0.01f};
        if (paramName == "sym_loss") return {0.0f, 1.0f, 0.01f};
        if (paramName == "sym_damp") return {100.0f, 20000.0f, 1.0f};
        if (paramName == "sym_delay") return {0.001f, 0.5f, 0.001f};
        if (paramName.rfind("string_", 0) == 0 && paramName.find("_semitones") != std::string::npos) {
            return {-24.0f, 48.0f, 1.0f};
        }
    }

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
    if (paramName == "feedback_delay") return {0.001f, 5.0f, 0.001f};
    if (paramName == "feedback_damp") return {20.0f, 20000.0f, 1.0f};
    if (paramName == "damping") return {0.0f, 10.0f, 0.01f};
    if (paramName == "q") return {1.0f, 1000.0f, 1.0f};
    if (paramName == "size" || paramName == "brightness" || paramName == "damp" ||
        paramName == "pluck_position" || paramName == "mute" || paramName == "bow_pressure" ||
        paramName == "bow_velocity" || paramName == "bow_position" || paramName == "pressure" ||
        paramName == "reed_stiffness" || paramName == "bell_opening" ||
        paramName == "lips_tension" || paramName == "mouth_position" ||
        paramName == "strike_sharpness" || paramName == "t60" ||
        paramName == "feedback" || paramName == "wet") return {0.0f, 1.0f, 0.01f};
    if (paramName == "strike_position") return {0.0f, 6.0f, 0.01f};
    if (paramName == "strike_cutoff") return {20.0f, 20000.0f, 1.0f};
    if (paramName == "velocity" || paramName == "scale" || paramName == "pitch_bend") return {-1.0f, 1.0f, 0.01f};
    if (paramName == "aftertouch") return {0.0f, 1.0f, 0.01f};
    if (paramName == "cc") return {0.0f, 127.0f, 1.0f};
    if (paramName == "range") return {0.0f, 24.0f, 0.1f};
    if (paramName == "flute_tune_cents") return {-100.0f, 100.0f, 1.0f};
    if (paramName == "flute_track_scale") return {0.9f, 1.1f, 0.001f};
    if (paramName == "flute_length_offset") return {0.0f, 0.5f, 0.001f};
    if (paramName == "offset") return {-5000.0f, 5000.0f, 0.1f};
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

std::string prefixedGroupId(const std::string& groupId, const std::string& childId) {
    return groupId + "__" + childId;
}

const GroupInput* findGroupInput(const GroupDefinition& group, const std::string& handle) {
    std::string id = handle;
    const std::string prefix = "input-";
    if (id.rfind(prefix, 0) == 0) {
        id = id.substr(prefix.size());
    }
    for (const auto& input : group.inputs) {
        if (input.id == id) {
            return &input;
        }
    }
    if (!group.inputs.empty() && (handle.empty() || handle == "input-0")) {
        return &group.inputs.front();
    }
    return nullptr;
}

const GroupOutput* findGroupOutput(const GroupDefinition& group, const std::string& handle) {
    std::string id = handle;
    const std::string prefix = "output-";
    if (id.rfind(prefix, 0) == 0) {
        id = id.substr(prefix.size());
    }
    for (const auto& output : group.outputs) {
        if (output.id == id) {
            return &output;
        }
    }
    if (!group.outputs.empty() && handle.empty()) {
        return &group.outputs.front();
    }
    return nullptr;
}

Graph flattenGroups(const Graph& graph, std::string& errorMsg) {
    Graph flat;
    flat.version = graph.version;
    flat.name = graph.name;
    flat.outputNodeId = graph.outputNodeId;

    std::map<std::string, const Node*> groupNodes;
    for (const auto& node : graph.nodes) {
        if (node.type == "group") {
            if (!node.group) {
                errorMsg = "Group node missing group definition: " + node.id;
                return {};
            }
            groupNodes[node.id] = &node;
            for (auto child : node.group->nodes) {
                const std::string originalChildId = child.id;
                if (child.type == "group") {
                    errorMsg = "Nested group nodes are not supported yet: " + node.id + "/" + child.id;
                    return {};
                }
                child.id = prefixedGroupId(node.id, child.id);
                for (const auto& promoted : node.group->promotedParams) {
                    if (promoted.targetNode == originalChildId && node.params.count(promoted.id) > 0) {
                        child.params[promoted.targetParam] = node.params.at(promoted.id);
                    }
                }
                flat.nodes.push_back(std::move(child));
            }
            if (flat.outputNodeId == node.id) {
                const GroupOutput* output = findGroupOutput(*node.group, "");
                if (!output) {
                    errorMsg = "Output group has no output socket: " + node.id;
                    return {};
                }
                flat.outputNodeId = prefixedGroupId(node.id, output->sourceNode);
            }
        } else {
            flat.nodes.push_back(node);
        }
    }

    for (const auto& node : graph.nodes) {
        if (node.type != "group" || !node.group) {
            continue;
        }
        for (auto conn : node.group->connections) {
            conn.source = prefixedGroupId(node.id, conn.source);
            conn.target = prefixedGroupId(node.id, conn.target);
            flat.connections.push_back(std::move(conn));
        }
    }

    for (const auto& conn : graph.connections) {
        auto sourceGroupIt = groupNodes.find(conn.source);
        auto targetGroupIt = groupNodes.find(conn.target);

        if (sourceGroupIt != groupNodes.end() && targetGroupIt != groupNodes.end()) {
            errorMsg = "Direct group-to-group connections are not supported yet";
            return {};
        }
        if (targetGroupIt != groupNodes.end()) {
            const Node* groupNode = targetGroupIt->second;
            const GroupInput* groupInput = findGroupInput(*groupNode->group, conn.targetHandle);
            if (!groupInput) {
                errorMsg = "Group input not found for connection into " + groupNode->id + ": " + conn.targetHandle;
                return {};
            }
            Connection rewritten = conn;
            rewritten.target = prefixedGroupId(groupNode->id, groupInput->targetNode);
            rewritten.targetHandle = groupInput->targetHandle;
            flat.connections.push_back(std::move(rewritten));
        } else if (sourceGroupIt != groupNodes.end()) {
            const Node* groupNode = sourceGroupIt->second;
            const GroupOutput* groupOutput = findGroupOutput(*groupNode->group, conn.sourceHandle);
            if (!groupOutput) {
                errorMsg = "Group output not found for connection from " + groupNode->id + ": " + conn.sourceHandle;
                return {};
            }
            Connection rewritten = conn;
            rewritten.source = prefixedGroupId(groupNode->id, groupOutput->sourceNode);
            rewritten.sourceHandle = groupOutput->sourceHandle;
            flat.connections.push_back(std::move(rewritten));
        } else {
            flat.connections.push_back(conn);
        }
    }

    return flat;
}

std::string FaustGraphCompiler::compile(const Graph& graph, std::string& errorMsg) {
    Graph workingGraph = flattenGroups(graph, errorMsg);
    if (!errorMsg.empty()) {
        return "";
    }

    if (workingGraph.nodes.empty()) {
        errorMsg = "Empty graph";
        return "";
    }

    std::vector<std::string> sortedNodeIds = topologicalSort(workingGraph);
    if (sortedNodeIds.size() != workingGraph.nodes.size()) {
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
    ss << "voice_gate = hslider(\"gate\", 0, 0, 1, 1);\n";
    ss << "aftertouch = hslider(\"aftertouch\", 0, 0, 1, 0.01);\n";
    ss << "pitch_bend = hslider(\"pitch_bend\", 0, -1, 1, 0.01);\n";
    for (int cc = 0; cc < 128; ++cc) {
        ss << "cc_" << cc << " = hslider(\"cc_" << cc << "\", 0, 0, 1, 0.01);\n";
    }
    ss << "\n";
    
    // Map of nodes for quick lookup
    std::map<std::string, const Node*> nodeMap;
    for (const auto& node : workingGraph.nodes) {
        nodeMap[node.id] = &node;
    }
    
    // 3. Emit local UI sliders for parameters
    ss << "// Node Parameters\n";
    for (const auto& node : workingGraph.nodes) {
        ss << "// Parameters for " << node.id << " (" << node.type << ")\n";
        for (const auto& [paramName, value] : node.params) {
            if (node.type == "cc" && paramName == "cc") {
                continue;
            }
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
    std::map<std::string, std::map<std::string, std::vector<Connection>>> parameterModulations;
    
    for (const auto& conn : workingGraph.connections) {
        if (!conn.targetHandle.empty() && conn.targetHandle.rfind("param-", 0) == 0) {
            std::string paramName = conn.targetHandle.substr(6); // strip "param-"
            parameterModulations[conn.target][paramName].push_back(conn);
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
                    modSS << "(" << baseVal << ")";
                    for (const auto& modConn : modSources) {
                        const Node* sourceNode = nullptr;
                        auto sourceIt = nodeMap.find(modConn.source);
                        if (sourceIt != nodeMap.end()) {
                            sourceNode = sourceIt->second;
                        }

                        if (name == "freq") {
                            // Any modulation targeting freq is treated as semitones
                            // and applied exponentially for musically correct pitch shifting
                            modSS << " * pow(2.0, node_" << modConn.source << " / 12.0)";
                        } else if (modConn.operation == "add") {
                            modSS << " + node_" << modConn.source;
                        } else {
                            modSS << " * node_" << modConn.source;
                        }
                    }
                    ParamRange range = getParamRange(node.type, name);
                    return "min(" + std::to_string(range.maxVal) + ", max(" + std::to_string(range.minVal) + ", " + modSS.str() + "))";
                }
            }
            return baseVal;
        };

        auto getTrackedFreqExpr = [&]() -> std::string {
            bool trackKb = true;
            if (node.params.count("keyboard_tracking") > 0) {
                trackKb = (node.params.at("keyboard_tracking") != 0.0f);
            }
            if (trackKb) {
                if (node.params.count("freq") > 0) {
                    return "voice_freq * (" + getParamExpr("freq", "440.0") + " / 440.0)";
                }
                return "voice_freq";
            }
            return getParamExpr("freq", "440.0");
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
        else if (node.type == "velocity") {
            ss << "voice_gain * " << getParamExpr("velocity", "1.0");
        }
        else if (node.type == "aftertouch") {
            ss << "aftertouch * " << getParamExpr("scale", "1.0");
        }
        else if (node.type == "pitch_bend") {
            ss << "pitch_bend * " << getParamExpr("range", "2.0");
        }
        else if (node.type == "cc") {
            int ccNumber = 1;
            if (node.params.count("cc") > 0) {
                ccNumber = std::clamp(static_cast<int>(node.params.at("cc")), 0, 127);
            }
            ss << "cc_" << ccNumber << " * " << getParamExpr("scale", "1.0");
        }
        else if (node.type == "gain") {
            std::string gainExpr = getParamExpr("gain", "voice_gain");
            std::string offsetExpr = getParamExpr("offset", "0.0");
            if (inputsExpr.empty()) {
                ss << "*(0.0)"; // No input
            } else {
                ss << "(" << inputsExpr << " * " << gainExpr << " + " << offsetExpr << ")";
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
               << getParamExpr("release", "0.2") << ", voice_gate) * " << getParamExpr("scale", "1.0");
            if (!inputsExpr.empty()) {
                ss << " * " << inputsExpr;
            }
        }
        else if (node.type == "lfo") {
            std::string freqExpr = getParamExpr("freq", "5.0");
            if (!inputsExpr.empty()) {
                freqExpr = "max(0.01, " + freqExpr + " + (" + inputsExpr + "))";
            }
            ss << "os.osc(" << freqExpr << ") * " << getParamExpr("scale", "1.0");
        }
        else if (node.type == "mixer") {
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                ss << inputsExpr;
            }
        }
        else if (node.type == "reverb") {
            std::string sizeExpr = getParamExpr("size", "0.5");
            std::string dampExpr = getParamExpr("damp", "0.5");
            std::string wetExpr = getParamExpr("wet", "0.35");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                // Faust's mono Freeverb sounds much better as a blended effect than
                // as a fully wet insert. Keep feedback below runaway/metallic ranges,
                // remove low-frequency buildup before the tank, and darken the tail.
                ss << "((" << inputsExpr << ") * (1.0 - " << wetExpr << ") + "
                   << "((" << inputsExpr << ") : fi.highpass(1, 80.0) "
                   << ": re.mono_freeverb(min(0.96, 0.35 + (" << sizeExpr << " * 0.6)), 0.55, "
                   << dampExpr << ", 23.0) "
                   << ": fi.lowpass(2, max(1200.0, 12000.0 - (" << dampExpr << " * 9000.0))) "
                   << "* " << wetExpr << ")) : ma.tanh";
            }
        }
        else if (node.type == "resonator_reverb_loop") {
            std::string freqExpr = getTrackedFreqExpr();
            std::string qExpr = getParamExpr("q", "100.0");
            std::string sizeExpr = getParamExpr("size", "0.6");
            std::string dampExpr = getParamExpr("damp", "0.35");
            std::string feedbackExpr = "min(0.98, " + getParamExpr("feedback", "0.35") + ")";
            std::string feedbackDelayExpr = getParamExpr("feedback_delay", "0.08");
            std::string feedbackDampExpr = getParamExpr("feedback_damp", "3000.0");
            std::string wetExpr = getParamExpr("wet", "0.7");
            std::string gainExpr = getParamExpr("gain", "0.8");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                // ma.tanh inside the feedback path provides soft saturation to prevent
                // the resonant feedback loop from blowing up to inf/NaN and cutting out.
                // Final ma.tanh on output is a safety limiter.
                ss << "((" << inputsExpr << " <: *(1.0 - " << wetExpr << "), "
                   << "(((+ : fi.resonbp(" << freqExpr << ", " << qExpr << ", 1.0) "
                   << ": re.mono_freeverb(" << sizeExpr << ", 0.5, " << dampExpr << ", 0.0)) "
                   << "~ (ma.tanh : de.sdelay(262144, 1024, " << feedbackDelayExpr << " * ma.SR) "
                   << ": fi.lowpass(1, " << feedbackDampExpr << ") : *(" << feedbackExpr << "))) "
                   << ": *(" << wetExpr << "))) : +) * " << gainExpr << " : ma.tanh";
            }
        }
        else if (node.type == "karplus_string") {
            std::string freqExpr = getTrackedFreqExpr();
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
        else if (node.type == "steel_string") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.guitar(pm.f2l(" << freqExpr << "), "
               << getParamExpr("pluck_position", "0.25") << ", "
               << getParamExpr("gain", "0.8") << ", voice_gate)";
        }
        else if (node.type == "nylon_string") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.nylonGuitar(pm.f2l(" << freqExpr << "), "
               << getParamExpr("pluck_position", "0.25") << ", "
               << getParamExpr("gain", "0.8") << ", voice_gate)";
        }
        else if (node.type == "electric_guitar") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.elecGuitar(pm.f2l(" << freqExpr << "), "
               << getParamExpr("pluck_position", "0.25") << ", "
               << getParamExpr("mute", "0.2") << ", "
               << getParamExpr("gain", "0.8") << ", voice_gate)";
        }
        else if (node.type == "bowed_string") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.violinModel(pm.f2l(" << freqExpr << "), "
               << getParamExpr("bow_pressure", "0.45") << ", "
               << "(" << getParamExpr("bow_velocity", "0.08") << " * voice_gate), "
               << getParamExpr("bow_position", "0.15") << ") * "
               << getParamExpr("gain", "0.7");
        }
        else if (node.type == "clarinet_reed") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.clarinetModel(pm.f2l(" << freqExpr << "), "
               << "(" << getParamExpr("pressure", "0.55") << " * voice_gate), "
               << getParamExpr("reed_stiffness", "0.45") << ", "
               << getParamExpr("bell_opening", "0.45") << ") * "
               << getParamExpr("gain", "0.7");
        }
        else if (node.type == "brass_tube") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.brassModel(pm.f2l(" << freqExpr << "), "
               << getParamExpr("lips_tension", "0.5") << ", "
               << getParamExpr("mute", "0.3") << ", "
               << "(" << getParamExpr("pressure", "0.6") << " * voice_gate)) * "
               << getParamExpr("gain", "0.7");
        }
        else if (node.type == "flute_tube") {
            std::string freqExpr = getTrackedFreqExpr();
            std::string calibratedFreqExpr = "(440.0 * pow((" + freqExpr + ") / 440.0, "
                + getParamExpr("flute_track_scale", "1.0")
                + ") * pow(2.0, " + getParamExpr("flute_tune_cents", "0.0") + " / 1200.0))";
            ss << "pm.fluteModel(max(0.01, pm.f2l(" << calibratedFreqExpr << ") - "
               << getParamExpr("flute_length_offset", "0.27") << "), "
               << getParamExpr("mouth_position", "0.5") << ", "
               << "(" << getParamExpr("pressure", "0.6") << " * voice_gate)) * "
               << getParamExpr("gain", "0.7");
        }
        else if (node.type == "marimba_bar") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.marimba(" << freqExpr << ", "
               << getParamExpr("strike_position", "2.0") << ", "
               << getParamExpr("strike_cutoff", "7000.0") << ", "
               << getParamExpr("strike_sharpness", "0.25") << ", "
               << getParamExpr("gain", "0.8") << ", voice_gate)";
        }
        else if (node.type == "djembe") {
            std::string freqExpr = getTrackedFreqExpr();
            ss << "pm.djembe(" << freqExpr << ", "
               << getParamExpr("strike_position", "0.4") << ", "
               << getParamExpr("strike_sharpness", "0.5") << ", "
               << getParamExpr("gain", "0.8") << ", voice_gate)";
        }
        else if (node.type == "body_resonator") {
            std::string sizeExpr = getParamExpr("size", "0.5");
            std::string brightnessExpr = getParamExpr("brightness", "0.5");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                std::string baseFreq = "(120.0 + " + sizeExpr + " * 1180.0)";
                std::string brightFreq = "(400.0 + " + brightnessExpr + " * 7600.0)";
                std::string highModeGain = "(0.15 + " + brightnessExpr + " * 0.85)";
                ss << inputsExpr << " : ("
                   << "_ <: "
                   << "fi.resonbp(" << baseFreq << ", 4.0, 0.75), "
                   << "fi.resonbp(" << baseFreq << " * (1.8 + " << brightnessExpr << " * 0.7), 7.0, " << highModeGain << ")"
                   << " :> _ : fi.lowpass(1, " << brightFreq << "))";
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
        else if (node.type == "modal_bank") {
            std::string freqExpr = getTrackedFreqExpr();
            std::string t60Expr = getParamExpr("t60", "0.5");
            std::string brightnessExpr = getParamExpr("brightness", "0.7");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                std::string qExpr = "(8.0 + " + t60Expr + " * 120.0)";
                ss << inputsExpr << " : (_ <: "
                   << "fi.resonbp(" << freqExpr << ", " << qExpr << ", 0.55), "
                   << "fi.resonbp(" << freqExpr << " * 1.5, " << qExpr << " * 0.75, 0.35 + " << brightnessExpr << " * 0.20), "
                   << "fi.resonbp(" << freqExpr << " * 2.1, " << qExpr << " * 0.55, " << brightnessExpr << " * 0.35), "
                   << "fi.resonbp(" << freqExpr << " * 2.8, " << qExpr << " * 0.40, " << brightnessExpr << " * 0.22)"
                   << " :> _ : *(0.35))";
            }
        }
        else if (node.type == "sympathetic_strings") {
            // Sympathetic string resonator: six independently tuned physical string
            // waveguides driven by the incoming signal. This keeps the graph acyclic
            // at the editor level while using Faust's internal string/body model.
            std::string freqExpr = getTrackedFreqExpr();
            std::string qExpr = getParamExpr("q", "35.0");
            std::string decayExpr = getParamExpr("sym_decay", "5.0");
            std::string couplingExpr = getParamExpr("sym_coupling", "0.8");
            std::string jawariExpr = getParamExpr("jawari", "0.25");
            std::string bridgeToneExpr = getParamExpr("bridge_tone", "0.45");
            std::string tuningPresetExpr = getParamExpr("tuning_preset", "0.0");
            std::string wetExpr = getParamExpr("wet", "0.6");
            std::string gainExpr = getParamExpr("gain", "0.8");
            std::string s1Expr = getParamExpr("string_1_semitones", "0.0");
            std::string s2Expr = getParamExpr("string_2_semitones", "7.0");
            std::string s3Expr = getParamExpr("string_3_semitones", "12.0");
            std::string s4Expr = getParamExpr("string_4_semitones", "19.0");
            std::string s5Expr = getParamExpr("string_5_semitones", "24.0");
            std::string s6Expr = getParamExpr("string_6_semitones", "31.0");
            if (inputsExpr.empty()) {
                ss << "0.0";
            } else {
                std::string dampingExpr = "(min(0.985, max(0.15, 0.35 + (" + decayExpr + " / 20.0) * 0.55 + (" + qExpr + " / 80.0) * 0.10)))";
                std::string presetIndex = "(floor(" + tuningPresetExpr + " + 0.5))";
                auto presetSemitone = [&](const std::string& customExpr,
                                           const std::string& sitar,
                                           const std::string& fifths,
                                           const std::string& chromatic,
                                           const std::string& drone) {
                    return "("
                        + customExpr + " * (" + presetIndex + " < 0.5)"
                        + " + " + sitar + " * (" + presetIndex + " >= 0.5) * (" + presetIndex + " < 1.5)"
                        + " + " + fifths + " * (" + presetIndex + " >= 1.5) * (" + presetIndex + " < 2.5)"
                        + " + " + chromatic + " * (" + presetIndex + " >= 2.5) * (" + presetIndex + " < 3.5)"
                        + " + " + drone + " * (" + presetIndex + " >= 3.5)"
                        + ")";
                };
                std::string t1Expr = presetSemitone(s1Expr, "0.0", "0.0", "0.0", "-12.0");
                std::string t2Expr = presetSemitone(s2Expr, "2.0", "7.0", "1.0", "-7.0");
                std::string t3Expr = presetSemitone(s3Expr, "4.0", "12.0", "2.0", "0.0");
                std::string t4Expr = presetSemitone(s4Expr, "7.0", "19.0", "3.0", "0.0");
                std::string t5Expr = presetSemitone(s5Expr, "9.0", "24.0", "4.0", "7.0");
                std::string t6Expr = presetSemitone(s6Expr, "12.0", "31.0", "5.0", "12.0");
                auto appendString = [&](const std::string& semitones,
                                        const std::string& detuneCents,
                                        const std::string& stringGain,
                                        const std::string& pluckPosition) {
                    std::string tunedFreq = "(" + freqExpr + " * pow(2.0, (" + semitones + " + " + detuneCents + " / 100.0) / 12.0))";
                    std::string lengthExpr = "pm.f2l(" + tunedFreq + ")";
                    ss << "(*(" << couplingExpr << " * " << stringGain << ")"
                       << " : (_ <: _, (*(4.0 + " << jawariExpr << " * 18.0) : +(0.35) : ma.tanh : -(ma.tanh(0.35)) : *(" << jawariExpr << ")) : +)"
                       << " : fi.highpass(1, 120.0)"
                       << " : fi.lowpass(1, 9000.0)"
                       << " : (_ <: _, (_, _ : * : -(0.02) : *(" << jawariExpr << " * 0.18)) : +)"
                       << " : fi.highpass(1, 35.0)"
                       << " : fi.lowpass(1, 7000.0)"
                       << " : pm.guitarModel(" << lengthExpr << ", " << pluckPosition << ")"
                       << " : *((" << dampingExpr << ") * " << stringGain << "))";
                };
                // Structure: ((dry_path, wet_path) : +) * gain : ma.tanh
                // wet_path = six independently tuned physical string/body models.
                // q and sym_decay bias the damping amount without exposing the old
                // feedback-delay parameters.
                ss << "((";
                ss << inputsExpr << " <: *(1.0 - " << wetExpr << "), ";
                ss << "(_ <: ";
                appendString(t1Expr, "-3.0", "0.95", "0.18");
                ss << ", ";
                appendString(t2Expr, "2.0", "0.82", "0.22");
                ss << ", ";
                appendString(t3Expr, "-1.5", "0.70", "0.27");
                ss << ", ";
                appendString(t4Expr, "3.5", "0.58", "0.31");
                ss << ", ";
                appendString(t5Expr, "-4.0", "0.46", "0.36");
                ss << ", ";
                appendString(t6Expr, "1.0", "0.36", "0.41");
                ss << " :> _ : fi.highpass(1, 55.0) : "
                   << "(_ <: "
                   << "fi.lowpass(1, 3200.0 + (1.0 - " << bridgeToneExpr << ") * 5200.0), "
                   << "fi.resonbp(850.0 + " << bridgeToneExpr << " * 2400.0, 2.0 + " << bridgeToneExpr << " * 10.0, " << bridgeToneExpr << " * 0.55), "
                   << "(fi.highpass(1, 1800.0) : *(" << bridgeToneExpr << " * 0.22))"
                   << " :> _) : *(0.30) ";
                ss << ": *(" << wetExpr << "))) : +) * " << gainExpr << " : ma.tanh";
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
    ss << "process = node_" << workingGraph.outputNodeId << " <: _, _;\n";
    
    return ss.str();
}

} // namespace OrchFaust
