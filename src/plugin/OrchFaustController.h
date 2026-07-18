#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "OrchEditorView.h"
#include <string>

namespace OrchFaust {

extern const Steinberg::FUID OrchFaustControllerUID;

class OrchFaustController : public Steinberg::Vst::EditController,
                            public Steinberg::Vst::INoteExpressionController {
public:
    OrchFaustController();
    ~OrchFaustController() override = default;

    static Steinberg::FUnknown* createInstance(void* context);

    // EditController overrides
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    Steinberg::int32 PLUGIN_API getNoteExpressionCount(Steinberg::int32 busIndex,
                                                        Steinberg::int16 channel) override;
    Steinberg::tresult PLUGIN_API getNoteExpressionInfo(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::int32 noteExpressionIndex,
        Steinberg::Vst::NoteExpressionTypeInfo& info) override;
    Steinberg::tresult PLUGIN_API getNoteExpressionStringByValue(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        Steinberg::Vst::NoteExpressionValue valueNormalized,
        Steinberg::Vst::String128 string) override;
    Steinberg::tresult PLUGIN_API getNoteExpressionValueByString(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id, const Steinberg::Vst::TChar* string,
        Steinberg::Vst::NoteExpressionValue& valueNormalized) override;

    OBJ_METHODS(OrchFaustController, Steinberg::Vst::EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::INoteExpressionController)
    END_DEFINE_INTERFACES(Steinberg::Vst::EditController)
    REFCOUNT_METHODS(Steinberg::Vst::EditController)

    void requestPortFromProcessor(OrchEditorView* view);
    void requestCurrentPatchName();
    void requestDialLayout();
    void requestGraphState();
    void requestWaveform();
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
