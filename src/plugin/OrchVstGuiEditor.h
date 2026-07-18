#pragma once

#include "OrchEditorView.h"
#include "OrchFaustController.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <string>
#include <tuple>
#include <vector>

namespace OrchFaust {

class OrchVstGuiEditor : public Steinberg::Vst::VSTGUIEditor,
                         public OrchEditorView,
                         public VSTGUI::IControlListener {
public:
    explicit OrchVstGuiEditor(OrchFaustController* controller);
    ~OrchVstGuiEditor() override;

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close() override;
    VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender, const char* message) override;

    void valueChanged(VSTGUI::CControl* control) override;
    void updatePortLabel(int port) override;
    void updateCurrentPatchLabel(const std::string& name) override;
    void updateDialLayout(const std::vector<std::tuple<std::string, std::string, float>>& layout) override;
    void updateGraphState(const std::string& graphJson) override;
    void updateWaveform(const std::vector<float>& samples) override;

private:
    void rebuild();
    void requestInitialState();
    void loadPresets();
    void loadSelectedPreset();
    void playTestNote();
    void openWebEditor();
    void chooseImpulseResponse(bool body);
    void setImpulseResponse(bool body, const std::string& path);
    void toggleImpulseNormalize(bool body);
    void toggleImpulseEnabled(bool body);

    struct Impl;
    Impl* impl = nullptr;
};

} // namespace OrchFaust
