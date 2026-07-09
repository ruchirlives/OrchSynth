#include "OrchFaustProcessor.h"
#include "OrchFaustController.h"
#include "graph/GraphParser.h"
#include "graph/FaustGraphCompiler.h"
#include "util/Logging.h"
#include "pluginterfaces/base/ibstream.h"
#include "faust/dsp/llvm-dsp.h"

#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace OrchFaust {

static std::string getDllDir() {
    char path[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
    std::string s(path);
    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return s.substr(0, lastSlash);
    }
    return "";
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
    std::string importPath = dllDir + "\\faust";
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
    voiceManager.prepare(newSetup.sampleRate, newSetup.maxSamplesPerBlock);
    return Steinberg::Vst::AudioEffect::setupProcessing(newSetup);
}

Steinberg::tresult PLUGIN_API OrchFaustProcessor::setActive(Steinberg::TBool state) {
    return Steinberg::Vst::AudioEffect::setActive(state);
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
    
    std::string dspCode = FaustGraphCompiler::compile(*optGraph, err);
    if (dspCode.empty()) {
        Logger::logError("Processor: Faust code generation failed: ", err);
        return;
    }
    
    Logger::logInfo("Generated Faust Code:\n", dspCode);
    
    std::string dllDir = getDllDir();
    std::string importPath = dllDir + "\\faust";
    
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
                    break;
                case Steinberg::Vst::Event::kNoteOffEvent:
                    voiceManager.noteOff(static_cast<int>(e.noteOff.pitch));
                    break;
            }
        }
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
    
    return Steinberg::Vst::AudioEffect::notify(message);
}

} // namespace OrchFaust
