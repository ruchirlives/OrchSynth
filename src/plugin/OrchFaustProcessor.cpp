#include "OrchFaustProcessor.h"
#include "OrchFaustController.h"
#include "graph/GraphParser.h"
#include "graph/FaustGraphCompiler.h"
#include "util/Logging.h"
#include "pluginterfaces/base/ibstream.h"
#include "faust/dsp/llvm-dsp.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#ifdef _WIN32
#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;
#else
#include <dlfcn.h>
#include <limits.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace OrchFaust {

namespace {
constexpr std::uint32_t kStateMagic = 0x4F534654; // "OSFT"
constexpr std::uint32_t kStateVersion = 3;

template <typename T>
bool writeValue(Steinberg::IBStream* stream, const T& value) {
    Steinberg::int32 bytesWritten = 0;
    return stream &&
        stream->write((void*)&value, static_cast<Steinberg::int32>(sizeof(T)), &bytesWritten) == Steinberg::kResultOk &&
        bytesWritten == static_cast<Steinberg::int32>(sizeof(T));
}

template <typename T>
bool readValue(Steinberg::IBStream* stream, T& value) {
    Steinberg::int32 bytesRead = 0;
    return stream &&
        stream->read(&value, static_cast<Steinberg::int32>(sizeof(T)), &bytesRead) == Steinberg::kResultOk &&
        bytesRead == static_cast<Steinberg::int32>(sizeof(T));
}

bool writeString(Steinberg::IBStream* stream, const std::string& value) {
    if (value.size() > static_cast<size_t>((std::numeric_limits<std::uint32_t>::max)())) {
        return false;
    }

    const auto length = static_cast<std::uint32_t>(value.size());
    if (!writeValue(stream, length)) {
        return false;
    }

    if (length == 0) {
        return true;
    }

    Steinberg::int32 bytesWritten = 0;
    return stream->write((void*)value.data(), static_cast<Steinberg::int32>(length), &bytesWritten) == Steinberg::kResultOk &&
        bytesWritten == static_cast<Steinberg::int32>(length);
}

bool readString(Steinberg::IBStream* stream, std::string& value) {
    std::uint32_t length = 0;
    if (!readValue(stream, length)) {
        return false;
    }

    value.clear();
    if (length == 0) {
        return true;
    }

    value.resize(length);
    Steinberg::int32 bytesRead = 0;
    return stream->read(value.data(), static_cast<Steinberg::int32>(length), &bytesRead) == Steinberg::kResultOk &&
        bytesRead == static_cast<Steinberg::int32>(length);
}

std::vector<Steinberg::Vst::TChar> toTCharString(const std::string& value) {
    std::vector<Steinberg::Vst::TChar> result;
    result.reserve(value.size() + 1);
    for (unsigned char ch : value) {
        result.push_back(static_cast<Steinberg::Vst::TChar>(ch));
    }
    result.push_back(0);
    return result;
}
}

static std::string getDllDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
    std::string s(path);
    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return s.substr(0, lastSlash);
    }
    return "";
#else
    Dl_info info;
    if (dladdr((void*)&getDllDir, &info) && info.dli_fname) {
        std::string s(info.dli_fname);
        size_t lastSlash = s.find_last_of("/");
        if (lastSlash != std::string::npos) {
            return s.substr(0, lastSlash);
        }
    }
    return "";
#endif
}

const Steinberg::FUID OrchFaustProcessorUID(0x8D385BAA, 0x8C1E4F43, 0x9330922D, 0xCD26FB1F);

OrchFaustProcessor::OrchFaustProcessor() : voiceManager(16) {
    // Register controller class
    setControllerClass(Steinberg::FUID(0x8D385BAA, 0x8C1E4F43, 0x9330922D, 0xCD26FB2F));
}

OrchFaustProcessor::~OrchFaustProcessor() {
    terminate();
}

Steinberg::FUnknown* OrchFaustProcessor::createInstance(void* context) {
    return (Steinberg::Vst::IAudioProcessor*)new OrchFaustProcessor();
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::initialize(Steinberg::FUnknown* context) {
    Steinberg::tresult result = Steinberg::Vst::AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk) return result;

    // Add stereo audio outputs
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::MediaTypes::kAudio);
    addEventInput(STR16("Event Input"), 16);
    
    // Start OSC server
    oscServer = std::make_unique<OscServer>(commandQueue);
    oscServer->start(9020);

    // Initial compile of a simple sine wave synth so the plugin sounds out-of-the-box
    std::string defaultDsp = 
        "import(\"stdfaust.lib\");\n"
        "voice_freq = hslider(\"freq\", 440, 20, 20000, 0.01);\n"
        "voice_gain = hslider(\"gain\", 0.5, 0, 1, 0.01);\n"
        "voice_gate = hslider(\"gate\", 0, 0, 1, 1);\n"
        "process = os.osc(voice_freq) * voice_gain * voice_gate <: _, _;\n";
        
    std::string dllDir = getDllDir();
#ifdef _WIN32
    std::string importPath = dllDir + "\\faust";
#else
    std::string importPath = dllDir + "/faust";
#endif
    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(importPath.c_str());

    std::string err;
    llvm_dsp_factory* defaultFactory = createDSPFactoryFromString(
        "DefaultSine", 
        defaultDsp, 
        static_cast<int>(argv.size()), 
        argv.data(), 
        "", 
        err, 
        -1
    );
    if (defaultFactory) {
        voiceManager.updateFactory(defaultFactory);
    } else {
        Logger::logError("Failed to compile default sine synth: ", err);
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::terminate() {
    if (oscServer) {
        oscServer->stop();
        oscServer.reset();
    }
    return Steinberg::Vst::AudioEffect::terminate();
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) {
    processingSampleRate = newSetup.sampleRate;
    voiceManager.prepare(newSetup.sampleRate, newSetup.maxSamplesPerBlock);
    bodyConvolutionProcessor.prepare(newSetup.sampleRate, newSetup.maxSamplesPerBlock);
    convolutionProcessor.prepare(newSetup.sampleRate, newSetup.maxSamplesPerBlock);
    return Steinberg::Vst::AudioEffect::setupProcessing(newSetup);
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::setActive(Steinberg::TBool state) {
    return Steinberg::Vst::AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::setProcessing(Steinberg::TBool state) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::process(Steinberg::Vst::ProcessData& data) {
    // 1. Process OSC Commands (non-compilation commands)
    checkAndApplyOscCommands();

    // 2. Check if a new Faust DSP factory has been compiled
    llvm_dsp_factory* newFactory = nullptr;
    while (auto popped = compiledFactoryQueue.pop()) {
        newFactory = *popped;
    }
    if (newFactory) {
        voiceManager.updateFactory(newFactory);
        applyCurrentControls();
    }

    // 3. Process incoming VST3 MIDI events
    if (data.inputEvents) {
        handleMidiEvents(data.inputEvents);
    }

    // 4. Render Audio
    if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2) {
        float* outputs[2] = {
            data.outputs[0].channelBuffers32[0],
            data.outputs[0].channelBuffers32[1]
        };
        voiceManager.process(outputs, data.numSamples);
        bodyConvolutionProcessor.process(outputs[0], outputs[1], data.numSamples);
        convolutionProcessor.process(outputs[0], outputs[1], data.numSamples);

        // Publish a mono snapshot of the final plug-in output for the editor.
        // Atomic samples keep the audio thread independent from UI/message locks.
        if (data.numSamples > 0) {
            for (std::size_t i = 0; i < kWaveformSampleCount; ++i) {
                const auto sourceIndex = (std::min)(
                    static_cast<Steinberg::int32>((i * static_cast<std::size_t>(data.numSamples)) /
                                                  kWaveformSampleCount),
                    data.numSamples - 1);
                const float mono = 0.5f * (outputs[0][sourceIndex] + outputs[1][sourceIndex]);
                outputWaveform[i].store(std::isfinite(mono) ? mono : 0.0f, std::memory_order_relaxed);
            }
        }
    }

    return Steinberg::kResultOk;
}

void OrchFaustProcessor::checkAndApplyOscCommands() {
    while (auto opCmd = commandQueue.pop()) {
        const auto& cmd = *opCmd;
        switch (cmd.type) {
            case CommandType::LoadGraph:
                processLoadGraph(cmd.stringArg1);
                break;
            case CommandType::Compile:
                processCompile();
                break;
            case CommandType::SetParam:
                voiceManager.setParameter(cmd.stringArg1 + "/" + cmd.stringArg2, cmd.floatArg1);
                break;
            case CommandType::NoteOn:
                voiceManager.noteOn(static_cast<int>(cmd.floatArg1), cmd.floatArg2);
                break;
            case CommandType::NoteOff:
                voiceManager.noteOff(static_cast<int>(cmd.floatArg1));
                break;
            case CommandType::AllNotesOff:
                voiceManager.allNotesOff();
                break;
            case CommandType::Status:
                Logger::logInfo("Status query: Synth compiled = ", voiceManager.isCompiled());
                break;
            case CommandType::RequestGraph:
                if (oscServer) oscServer->sendGraphState(currentGraphJson);
                break;
        }
    }
}

void OrchFaustProcessor::processLoadGraph(const std::string& jsonStr) {
    Logger::logInfo("Processor: Processing LoadGraph JSON");
    currentGraphJson = jsonStr;
    
    // Trigger compilation in this thread (the OSC/processor thread is safe to compile)
    processCompile();
}

void OrchFaustProcessor::processCompile() {
    if (currentGraphJson.empty()) {
        Logger::logWarning("Processor: Compile requested but no graph JSON loaded");
        return;
    }
    
    std::string err;
    auto optGraph = GraphParser::parse(currentGraphJson, err);
    if (!optGraph) {
        Logger::logError("Processor: Graph validation failed: ", err);
        return;
    }
    currentPatchName = optGraph->name.empty() ? "Untitled Patch" : optGraph->name;
    std::set<std::string> connectedVstDialIds;
    for (const auto& connection : optGraph->connections) {
        connectedVstDialIds.insert(connection.source);
    }
    currentVstDialLayout.clear();
    for (const auto& node : optGraph->nodes) {
        if (node.type == "vst_dial" && connectedVstDialIds.count(node.id) > 0) {
            const std::string label = node.name.empty() ? node.id : node.name;
            currentVstDialLayout.emplace_back(node.id, label);
            if (currentVstDialValues.find(node.id) == currentVstDialValues.end()) {
                currentVstDialValues[node.id] = 0.0f;
            }
        }
    }
    
    Graph faustGraph = *optGraph;
    std::vector<Node> convolutionNodes;
    std::vector<Node> bodyConvolutionNodes;
    std::set<std::string> convolutionNodeIds;
    for (const auto& node : faustGraph.nodes) {
        if (node.type == "convolution") {
            convolutionNodes.push_back(node);
            convolutionNodeIds.insert(node.id);
        }
        if (node.type == "body_convolution") bodyConvolutionNodes.push_back(node);
    }
    if (convolutionNodes.size() > 1) {
        Logger::logWarning("Processor: Multiple Convolution nodes found; using the first post-mix node");
    }
    const Node* outputNode = nullptr;
    for (const auto& node : faustGraph.nodes) {
        if (node.id == faustGraph.outputNodeId) {
            outputNode = &node;
            break;
        }
    }
    const bool bodyEnabled = outputNode && outputNode->params.count("body_enabled") &&
        outputNode->params.at("body_enabled") >= 0.5f;
    const bool roomEnabled = outputNode && outputNode->params.count("room_enabled") &&
        outputNode->params.at("room_enabled") >= 0.5f;
    const Node* standaloneBodyNode = bodyConvolutionNodes.empty() ? nullptr : &bodyConvolutionNodes.front();
    const bool standaloneBodyEnabled = standaloneBodyNode &&
        (!standaloneBodyNode->params.count("body_enabled") || standaloneBodyNode->params.at("body_enabled") >= 0.5f);
    if (bodyEnabled || standaloneBodyEnabled) {
        // Legacy Body Convolution nodes are processed after the voice mix so older
        // patches remain usable without emitting IR samples into Faust source.
        const Node* node = bodyEnabled ? outputNode : standaloneBodyNode;
        const std::string pathKey = bodyEnabled ? "body_ir_path" : "ir_path";
        const std::string wetKey = bodyEnabled ? "body_wet" : "wet";
        const std::string gainKey = bodyEnabled ? "body_gain" : "gain";
        const std::string normalizeKey = bodyEnabled ? "body_normalize" : "normalize";
        const auto pathIt = node->stringParams.find(pathKey);
        const std::string irPath = pathIt == node->stringParams.end() ? "" : pathIt->second;
        bodyConvolutionProcessor.setMix(node->params.count(wetKey) ? node->params.at(wetKey) : 0.5f,
            node->params.count(gainKey) ? node->params.at(gainKey) : 1.0f);
        std::string bodyError;
        if (!bodyConvolutionProcessor.loadImpulseResponse(irPath,
                !node->params.count(normalizeKey) || node->params.at(normalizeKey) >= 0.5f, bodyError)) {
            Logger::logWarning("Processor: Body IR bypassed: ", bodyError);
        }
    } else {
        bodyConvolutionProcessor.clear();
    }
    if (roomEnabled || !convolutionNodes.empty()) {
        // Legacy standalone nodes are accepted so existing presets keep their room IR.
        const Node* node = roomEnabled ? outputNode : &convolutionNodes.front();
        const std::string pathKey = roomEnabled ? "room_ir_path" : "ir_path";
        const std::string wetKey = roomEnabled ? "room_wet" : "wet";
        const std::string gainKey = roomEnabled ? "room_gain" : "gain";
        const std::string normalizeKey = roomEnabled ? "room_normalize" : "normalize";
        const auto pathIt = node->stringParams.find(pathKey);
        const std::string irPath = pathIt == node->stringParams.end() ? "" : pathIt->second;
        const float wet = node->params.count(wetKey) ? node->params.at(wetKey) : 0.35f;
        const float gain = node->params.count(gainKey) ? node->params.at(gainKey) : 1.0f;
        const bool normalize = !node->params.count(normalizeKey) || node->params.at(normalizeKey) >= 0.5f;
        convolutionProcessor.setMix(wet, gain);
        std::string convolutionError;
        if (!convolutionProcessor.loadImpulseResponse(irPath, normalize, convolutionError)) {
            Logger::logWarning("Processor: Convolution bypassed: ", convolutionError);
        }
    } else {
        convolutionProcessor.clear();
    }

    faustGraph.nodes.erase(std::remove_if(faustGraph.nodes.begin(), faustGraph.nodes.end(),
        [](const Node& node) { return node.type == "convolution"; }), faustGraph.nodes.end());
    faustGraph.connections.erase(std::remove_if(faustGraph.connections.begin(), faustGraph.connections.end(),
        [&convolutionNodeIds](const Connection& connection) {
            return convolutionNodeIds.count(connection.source) > 0 || convolutionNodeIds.count(connection.target) > 0;
        }), faustGraph.connections.end());

    std::string dspCode = FaustGraphCompiler::compile(faustGraph, err);
    if (dspCode.empty()) {
        Logger::logError("Processor: Faust code generation failed: ", err);
        return;
    }
    
    Logger::logInfo("Generated Faust Code:\n", dspCode);
    
    std::string dllDir = getDllDir();
#ifdef _WIN32
    std::string importPath = dllDir + "\\faust";
#else
    std::string importPath = dllDir + "/faust";
#endif
    
    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(importPath.c_str());
    
    llvm_dsp_factory* newFactory = createDSPFactoryFromString(
        "OrchFaustDSP",
        dspCode,
        static_cast<int>(argv.size()),
        argv.data(),
        "",
        err,
        -1
    );
    
    if (!newFactory) {
        Logger::logError("Processor: libfaust JIT compilation failed: ", err);
        return;
    }
    
    // Push the factory to the audio thread
    if (!compiledFactoryQueue.push(newFactory)) {
        Logger::logWarning("Processor: Compiled factory queue full, deleting new factory");
        deleteDSPFactory(newFactory);
    } else {
        Logger::logInfo("Processor: Sent compiled factory to audio thread.");
        notifyPatchNameChanged();
        notifyDialLayoutChanged();
    }
}

void OrchFaustProcessor::handleMidiEvents(Steinberg::Vst::IEventList* eventList) {
    int count = eventList->getEventCount();
    for (int i = 0; i < count; ++i) {
        Steinberg::Vst::Event e;
        if (eventList->getEvent(i, e) == Steinberg::kResultOk) {
            switch (e.type) {
                case Steinberg::Vst::Event::kNoteOnEvent:
                voiceManager.noteOn(static_cast<int>(e.noteOn.pitch), e.noteOn.velocity);
                    applyCurrentControls();
                    break;
                case Steinberg::Vst::Event::kNoteOffEvent:
                    voiceManager.noteOff(static_cast<int>(e.noteOff.pitch));
                    break;
                case Steinberg::Vst::Event::kPolyPressureEvent:
                    currentAftertouch = std::clamp(e.polyPressure.pressure, 0.0f, 1.0f);
                    voiceManager.setGlobalControl("aftertouch", currentAftertouch);
                    break;
                case Steinberg::Vst::Event::kLegacyMIDICCOutEvent: {
                    const int controlNumber = static_cast<int>(e.midiCCOut.controlNumber);
                    if (controlNumber == Steinberg::Vst::kAfterTouch) {
                        currentAftertouch = std::clamp(static_cast<float>(e.midiCCOut.value) / 127.0f, 0.0f, 1.0f);
                        voiceManager.setGlobalControl("aftertouch", currentAftertouch);
                    } else if (controlNumber == Steinberg::Vst::kPitchBend) {
                        const int lsb = static_cast<int>(e.midiCCOut.value) & 0x7f;
                        const int msb = static_cast<int>(e.midiCCOut.value2) & 0x7f;
                        const int raw = (msb << 7) | lsb;
                        currentPitchBend = std::clamp((static_cast<float>(raw) - 8192.0f) / 8192.0f, -1.0f, 1.0f);
                        voiceManager.setGlobalControl("pitch_bend", currentPitchBend);
                    } else if (controlNumber >= 0 && controlNumber < 128) {
                        currentCcValues[controlNumber] = std::clamp(static_cast<float>(e.midiCCOut.value) / 127.0f, 0.0f, 1.0f);
                        voiceManager.setGlobalControl("cc_" + std::to_string(controlNumber), currentCcValues[controlNumber]);
                    }
                    break;
                }
            }
        }
    }
}

void OrchFaustProcessor::applyCurrentControls() {
    voiceManager.setGlobalControl("aftertouch", currentAftertouch);
    voiceManager.setGlobalControl("pitch_bend", currentPitchBend);
    for (int cc = 0; cc < 128; ++cc) {
        voiceManager.setGlobalControl("cc_" + std::to_string(cc), currentCcValues[cc]);
    }
    for (const auto& [key, value] : currentVstDialValues) {
        voiceManager.setGlobalControl("vst_dial_" + key, value);
    }
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::notify(Steinberg::Vst::IMessage* message) {
    if (!message) return Steinberg::kResultFalse;
    
    if (strcmp(message->getMessageID(), "GetOscServerPort") == 0) {
        if (oscServer) {
            if (auto* reply = allocateMessage()) {
                reply->setMessageID("OscServerPort");
                reply->getAttributes()->setInt("port", oscServer->getPortNum());
                sendMessage(reply);
            }
        }
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "GetCurrentPatchName") == 0) {
        notifyPatchNameChanged();
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "GetVstDialLayout") == 0) {
        notifyDialLayoutChanged();
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "GetGraphState") == 0) {
        notifyGraphState();
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "GetWaveform") == 0) {
        if (auto* reply = allocateMessage()) {
            std::array<float, kWaveformSampleCount> snapshot {};
            for (std::size_t i = 0; i < snapshot.size(); ++i) {
                snapshot[i] = outputWaveform[i].load(std::memory_order_relaxed);
            }
            reply->setMessageID("Waveform");
            reply->getAttributes()->setBinary("samples", snapshot.data(),
                static_cast<Steinberg::uint32>(snapshot.size() * sizeof(float)));
            sendMessage(reply);
        }
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "SetVstDial") == 0) {
        Steinberg::Vst::TChar keyChars[256] = {};
        double value = 0.0;
        if (message->getAttributes()->getString("key", keyChars, sizeof(keyChars)) == Steinberg::kResultOk &&
            message->getAttributes()->getFloat("value", value) == Steinberg::kResultOk) {
            std::string key;
            for (auto* ch = keyChars; *ch; ++ch) {
                key.push_back(static_cast<char>(*ch));
            }
            if (!key.empty()) {
                const float clamped = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
                currentVstDialValues[key] = clamped;
                voiceManager.setGlobalControl("vst_dial_" + key, clamped);
            }
        }
        return Steinberg::kResultOk;
    }
    
    return Steinberg::Vst::AudioEffect::notify(message);
}

void OrchFaustProcessor::notifyPatchNameChanged() {
    if (auto* reply = allocateMessage()) {
        reply->setMessageID("CurrentPatchName");
        auto patchName = toTCharString(currentPatchName);
        reply->getAttributes()->setString("name", patchName.data());
        sendMessage(reply);
    }
}

void OrchFaustProcessor::notifyDialLayoutChanged() {
    if (auto* reply = allocateMessage()) {
        reply->setMessageID("VstDialLayout");
        std::string layout;
        for (const auto& [key, label] : currentVstDialLayout) {
            layout += key;
            layout += '\t';
            layout += label;
            layout += '\t';
            auto valueIt = currentVstDialValues.find(key);
            layout += std::to_string(valueIt == currentVstDialValues.end() ? 0.0f : valueIt->second);
            layout += '\n';
        }
        auto layoutText = toTCharString(layout);
        reply->getAttributes()->setString("layout", layoutText.data());
        sendMessage(reply);
    }
}

void OrchFaustProcessor::notifyGraphState() {
    if (auto* reply = allocateMessage()) {
        reply->setMessageID("GraphState");
        auto graph = toTCharString(currentGraphJson);
        reply->getAttributes()->setString("graph", graph.data());
        sendMessage(reply);
    }
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::getState(Steinberg::IBStream* state) {
    if (!state) {
        return Steinberg::kResultFalse;
    }

    if (!writeValue(state, kStateMagic) ||
        !writeValue(state, kStateVersion) ||
        !writeString(state, currentGraphJson) ||
        !writeValue(state, currentAftertouch) ||
        !writeValue(state, currentPitchBend)) {
        return Steinberg::kResultFalse;
    }

    for (float value : currentCcValues) {
        if (!writeValue(state, value)) {
            return Steinberg::kResultFalse;
        }
    }

    const auto dialCount = static_cast<std::uint32_t>(currentVstDialValues.size());
    if (!writeValue(state, dialCount)) {
        return Steinberg::kResultFalse;
    }
    for (const auto& [key, value] : currentVstDialValues) {
        if (!writeString(state, key) || !writeValue(state, value)) {
            return Steinberg::kResultFalse;
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::setState(Steinberg::IBStream* state) {
    if (!state) {
        return Steinberg::kResultFalse;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::string graphJson;
    float aftertouch = 0.0f;
    float pitchBend = 0.0f;
    float ccValues[128] = {};

    if (!readValue(state, magic) ||
        !readValue(state, version) ||
        magic != kStateMagic ||
        (version < 1 || version > kStateVersion) ||
        !readString(state, graphJson) ||
        !readValue(state, aftertouch) ||
        !readValue(state, pitchBend)) {
        return Steinberg::kResultFalse;
    }

    for (float& value : ccValues) {
        if (!readValue(state, value)) {
            return Steinberg::kResultFalse;
        }
    }

    std::map<std::string, float> vstDialValues;
    if (version == 2) {
        for (int dial = 0; dial < 8; ++dial) {
            float value = 0.0f;
            if (!readValue(state, value)) {
                return Steinberg::kResultFalse;
            }
            if (value != 0.0f) {
                vstDialValues[std::to_string(dial)] = std::clamp(value, 0.0f, 1.0f);
            }
        }
    } else if (version >= 3) {
        std::uint32_t dialCount = 0;
        if (!readValue(state, dialCount)) {
            return Steinberg::kResultFalse;
        }
        for (std::uint32_t i = 0; i < dialCount; ++i) {
            std::string key;
            float value = 0.0f;
            if (!readString(state, key) || !readValue(state, value)) {
                return Steinberg::kResultFalse;
            }
            if (!key.empty()) {
                vstDialValues[key] = std::clamp(value, 0.0f, 1.0f);
            }
        }
    }

    currentGraphJson = graphJson;
    currentAftertouch = std::clamp(aftertouch, 0.0f, 1.0f);
    currentPitchBend = std::clamp(pitchBend, -1.0f, 1.0f);
    for (int cc = 0; cc < 128; ++cc) {
        currentCcValues[cc] = std::clamp(ccValues[cc], 0.0f, 1.0f);
    }
    currentVstDialValues = std::move(vstDialValues);

    applyCurrentControls();
    if (!currentGraphJson.empty()) {
        processCompile();
    } else {
        currentPatchName = "Default Poly Sine";
        notifyPatchNameChanged();
    }

    return Steinberg::kResultOk;
}

} // namespace OrchFaust
