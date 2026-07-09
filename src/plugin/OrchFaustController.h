#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace OrchFaust {

extern const Steinberg::FUID OrchFaustControllerUID;

class OrchFaustController : public Steinberg::Vst::EditController {
public:
    OrchFaustController();
    ~OrchFaustController() override = default;

    static Steinberg::FUnknown* createInstance(void* context);

    // EditController overrides
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
};

} // namespace OrchFaust
