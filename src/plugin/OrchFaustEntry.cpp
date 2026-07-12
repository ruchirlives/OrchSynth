#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "OrchFaustProcessor.h"
#include "OrchFaustController.h"

// Define VST3 plug-in details
#ifndef VERSION_STR
#define VERSION_STR "1.0.0"
#endif

BEGIN_FACTORY_DEF("Orch", "https://github.com/ruchirlives", "")

    // The Audio Processor
    DEF_CLASS2(INLINE_UID_FROM_FUID(OrchFaust::OrchFaustProcessorUID),
               Steinberg::PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               "Orch Synth VST3",
               Steinberg::Vst::kDistributable,
               Steinberg::Vst::PlugType::kInstrument,
               VERSION_STR,
               kVstVersionString,
               OrchFaust::OrchFaustProcessor::createInstance)

    // The Edit Controller
    DEF_CLASS2(INLINE_UID_FROM_FUID(OrchFaust::OrchFaustControllerUID),
               Steinberg::PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               "Orch Synth Controller",
               0,
               "",
               VERSION_STR,
               kVstVersionString,
               OrchFaust::OrchFaustController::createInstance)

END_FACTORY
