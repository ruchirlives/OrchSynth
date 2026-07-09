#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "dsp/VoiceManager.h"
#include "osc/OscServer.h"
#include "osc/OscCommandQueue.h"

// Forward declaration of Faust compiler types
class llvm_dsp_factory;

namespace OrchFaust {

extern const Steinberg::FUID OrchFaustProcessorUID;

class OrchFaustProcessor : public Steinberg::Vst::AudioEffect {
public:
    OrchFaustProcessor();
    ~OrchFaustProcessor() override;

    static Steinberg::FUnknown* createInstance(void* context);

    // AudioEffect overrides
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // Standard class declaration
    // (addRef, release, queryInterface are inherited from AudioEffect/FObject)

private:
    void handleMidiEvents(Steinberg::Vst::IEventList* eventList);
    void checkAndApplyOscCommands();

    VoiceManager voiceManager;
    OscCommandQueue commandQueue;
    std::unique_ptr<OscServer> oscServer;

    // Queue to pass compiled factories from OSC thread to Audio thread
    LockFreeSPSCQueue<llvm_dsp_factory*, 16> compiledFactoryQueue;
    
    // Store current graph state
    std::string currentGraphJson;

    // Compiler helper
    void processLoadGraph(const std::string& jsonStr);
    void processCompile();
};

} // namespace OrchFaust
