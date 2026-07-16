#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "OrchEditorView.h"
#include <string>

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
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    void requestPortFromProcessor(OrchEditorView* view);
    void requestCurrentPatchName();
    void requestDialLayout();
    void setCurrentPatchName(std::string name);
    void setVstDial(const std::string& key, float value);
    void clearActiveView() { activeView = nullptr; }
    int getActivePort() const { return activePort; }

private:
    int activePort = 9020;
    std::string currentPatchName = "Default Poly Sine";
    OrchEditorView* activeView = nullptr;
};

} // namespace OrchFaust
