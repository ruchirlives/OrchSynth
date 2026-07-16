#include "OrchVstGuiEditor.h"

#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include <nlohmann/json.hpp>
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace OrchFaust {

using json = nlohmann::json;

extern const char* PRESET_SINE;
extern const char* PRESET_SAW;
extern const char* PRESET_PLUCK;

namespace {

constexpr int32_t kTagTestNote = 1001;
constexpr int32_t kTagPresetMenu = 1002;
constexpr int32_t kTagReloadPreset = 1003;
constexpr int32_t kTagOpenEditor = 1004;
constexpr int32_t kTagDialBase = 2000;

const VSTGUI::CColor kBg(18, 21, 27);
const VSTGUI::CColor kPanel(33, 40, 52);
const VSTGUI::CColor kPanel2(25, 30, 39);
const VSTGUI::CColor kBorder(74, 88, 112);
const VSTGUI::CColor kText(240, 246, 255);
const VSTGUI::CColor kMuted(157, 172, 194);
const VSTGUI::CColor kAccent(76, 229, 139);

std::string graphJsonWithPresetName(const std::string& jsonGraph, const std::string& presetName) {
    if (jsonGraph.empty() || presetName.empty()) {
        return jsonGraph;
    }
    try {
        auto graph = json::parse(jsonGraph);
        graph["name"] = presetName;
        return graph.dump(2);
    } catch (...) {
        return jsonGraph;
    }
}

std::filesystem::path getPresetsDir() {
    const char* home = std::getenv("HOME");
    if (home) {
        auto path = std::filesystem::path(home) / "Documents" / "Orch" / "modules" / "orch_synth_editor" / "presets";
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
#if defined(_WIN32)
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, path))) {
        auto presetsPath = std::filesystem::path(path) / "Orch" / "modules" / "orch_synth_editor" / "presets";
        if (std::filesystem::exists(presetsPath)) {
            return presetsPath;
        }
    }
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return std::filesystem::path(userProfile) / "Documents" / "Orch" / "modules" / "orch_synth_editor" / "presets";
    }
#endif
    return {};
}

VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& rect, const std::string& text, VSTGUI::CColor color = kText) {
    auto* label = new VSTGUI::CTextLabel(rect, text.c_str());
    label->setBackColor(kPanel2);
    label->setFrameColor(kPanel2);
    label->setFontColor(color);
    return label;
}

VSTGUI::CTextButton* makeButton(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int32_t tag, const std::string& title) {
    auto* button = new VSTGUI::CTextButton(rect, listener, tag, title.c_str());
    button->setRoundRadius(7.0);
    button->setFrameWidth(1.0);
    button->setFrameColor(kBorder);
    button->setFrameColorHighlighted(kAccent);
    button->setTextColor(kText);
    button->setTextColorHighlighted(kText);
    return button;
}

class GraphDialControl : public VSTGUI::CControl {
public:
    GraphDialControl(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int32_t tag, std::string label, float value)
        : CControl(rect, listener, tag), label(std::move(label)) {
        setMin(0.0f);
        setMax(1.0f);
        setValueNormalized(std::clamp(value, 0.0f, 1.0f));
    }

    void draw(VSTGUI::CDrawContext* context) override {
        const auto size = getViewSize();
        auto knob = VSTGUI::CRect(size.left + 17, size.top + 6, size.left + 75, size.top + 64);
        const auto value = std::clamp(getValueNormalized(), 0.0f, 1.0f);
        const auto angle = (-135.0 + value * 270.0) * 3.14159265358979323846 / 180.0;
        const auto cx = (knob.left + knob.right) * 0.5;
        const auto cy = (knob.top + knob.bottom) * 0.5;
        const auto radius = (knob.getWidth() * 0.5) - 7.0;

        context->setFillColor(VSTGUI::CColor(14, 18, 24));
        context->setFrameColor(kBorder);
        context->setLineWidth(1.4);
        context->drawEllipse(knob, VSTGUI::kDrawFilledAndStroked);

        auto inner = knob;
        inner.inset(7, 7);
        context->setFillColor(VSTGUI::CColor(43, 53, 68));
        context->setFrameColor(VSTGUI::CColor(98, 118, 146));
        context->drawEllipse(inner, VSTGUI::kDrawFilledAndStroked);

        VSTGUI::CPoint start(cx, cy);
        VSTGUI::CPoint end(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
        context->setFrameColor(kAccent);
        context->setLineWidth(3.0);
        context->drawLine(start, end);

        context->setFontColor(kText);
        context->setFont(VSTGUI::kNormalFontSmall);
        context->drawString(label.c_str(), VSTGUI::CRect(size.left, size.top + 68, size.right, size.top + 86), VSTGUI::kCenterText);
        context->setFontColor(kMuted);
        char valueText[16] = {};
        snprintf(valueText, sizeof(valueText), "%d%%", static_cast<int>(value * 100.0f + 0.5f));
        context->drawString(valueText, VSTGUI::CRect(size.left, size.top + 84, size.right, size.top + 100), VSTGUI::kCenterText);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override {
        if (!buttons.isLeftButton()) {
            return VSTGUI::kMouseEventNotHandled;
        }
        beginEdit();
        setValueFromPoint(where);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override {
        if (!isEditing() || !buttons.isLeftButton()) {
            return VSTGUI::kMouseEventNotHandled;
        }
        setValueFromPoint(where);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override {
        if (isEditing()) {
            setValueFromPoint(where);
            endEdit();
            return VSTGUI::kMouseEventHandled;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    void setValueFromPoint(const VSTGUI::CPoint& where) {
        const auto size = getViewSize();
        const auto top = size.top + 6;
        const auto bottom = size.top + 64;
        const auto value = std::clamp(static_cast<float>((bottom - where.y) / (bottom - top)), 0.0f, 1.0f);
        if (value != getValueNormalized()) {
            setValueNormalized(value);
            valueChanged();
            invalid();
        }
    }

    std::string label;

    CLASS_METHODS_NOCOPY(GraphDialControl, VSTGUI::CControl)
};

} // namespace

struct OrchVstGuiEditor::Impl {
    explicit Impl(OrchFaustController* controller) : controller(controller) {}

    OrchFaustController* controller = nullptr;
    VSTGUI::CFrame* frame = nullptr;
    VSTGUI::CViewContainer* root = nullptr;
    VSTGUI::CViewContainer* dialPanel = nullptr;
    VSTGUI::CTextLabel* portLabel = nullptr;
    VSTGUI::CTextLabel* currentPatchLabel = nullptr;
    VSTGUI::CTextLabel* presetCountLabel = nullptr;
    VSTGUI::COptionMenu* presetMenu = nullptr;
    std::vector<std::string> presetNames;
    std::vector<std::filesystem::path> presetPaths;
    std::vector<std::string> dialKeys;
    std::vector<std::tuple<std::string, std::string, float>> pendingLayout;
};

OrchVstGuiEditor::OrchVstGuiEditor(OrchFaustController* controller)
    : VSTGUIEditor(controller, nullptr), impl(new Impl(controller)) {
    Steinberg::ViewRect r(0, 0, 500, 390);
    setRect(r);
}

OrchVstGuiEditor::~OrchVstGuiEditor() {
    delete impl;
}

bool PLUGIN_API OrchVstGuiEditor::open(void* parent, const VSTGUI::PlatformType& platformType) {
    impl->frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, 500, 390), this);
    if (!impl->frame->open(parent, platformType)) {
        impl->frame->forget();
        impl->frame = nullptr;
        return false;
    }
    rebuild();
    loadPresets();
    if (impl->controller) {
        impl->controller->requestPortFromProcessor(this);
        impl->controller->requestCurrentPatchName();
        impl->controller->requestDialLayout();
    }
    return true;
}

void PLUGIN_API OrchVstGuiEditor::close() {
    if (impl->controller) {
        impl->controller->clearActiveView();
    }
    impl->root = nullptr;
    impl->dialPanel = nullptr;
    impl->portLabel = nullptr;
    impl->currentPatchLabel = nullptr;
    impl->presetCountLabel = nullptr;
    impl->presetMenu = nullptr;
    if (impl->frame) {
        impl->frame->close();
        impl->frame = nullptr;
    }
}

void OrchVstGuiEditor::rebuild() {
    auto* root = new VSTGUI::CViewContainer(VSTGUI::CRect(0, 0, 500, 390));
    root->setBackgroundColor(kBg);
    impl->root = root;
    impl->frame->addView(root);

    auto* header = new VSTGUI::CViewContainer(VSTGUI::CRect(18, 12, 482, 72));
    header->setBackgroundColor(kPanel2);
    root->addView(header);
    auto* title = makeLabel(VSTGUI::CRect(0, 8, 464, 34), "Orch Synth");
    title->setFont(VSTGUI::kNormalFontBig);
    header->addView(title);
    auto* subtitle = makeLabel(VSTGUI::CRect(0, 36, 464, 54), "VST3 Controller", kMuted);
    subtitle->setFont(VSTGUI::kNormalFontSmall);
    header->addView(subtitle);

    root->addView(makeButton(VSTGUI::CRect(28, 94, 226, 130), this, kTagTestNote, "Play C1 Note"));

    auto* presetPanel = new VSTGUI::CViewContainer(VSTGUI::CRect(18, 140, 482, 222));
    presetPanel->setBackgroundColor(kPanel);
    root->addView(presetPanel);
    auto* presetTitle = makeLabel(VSTGUI::CRect(12, 8, 140, 28), "Load Preset");
    presetTitle->setFont(VSTGUI::kNormalFontSmall);
    presetPanel->addView(presetTitle);
    impl->currentPatchLabel = makeLabel(VSTGUI::CRect(152, 8, 440, 28), "Current Patch: Default Poly Sine", kAccent);
    impl->currentPatchLabel->setFont(VSTGUI::kNormalFontSmall);
    presetPanel->addView(impl->currentPatchLabel);
    impl->presetMenu = new VSTGUI::COptionMenu(VSTGUI::CRect(12, 38, 318, 62), this, kTagPresetMenu);
    impl->presetMenu->setBackColor(VSTGUI::CColor(245, 248, 252));
    impl->presetMenu->setFrameColor(kBorder);
    impl->presetMenu->setFontColor(VSTGUI::CColor(17, 24, 39));
    presetPanel->addView(impl->presetMenu);
    presetPanel->addView(makeButton(VSTGUI::CRect(340, 34, 452, 66), this, kTagReloadPreset, "Reload Preset"));
    impl->presetCountLabel = makeLabel(VSTGUI::CRect(12, 62, 220, 78), "0 presets loaded", kMuted);
    impl->presetCountLabel->setFont(VSTGUI::kNormalFontVerySmall);
    presetPanel->addView(impl->presetCountLabel);

    impl->dialPanel = new VSTGUI::CViewContainer(VSTGUI::CRect(18, 232, 482, 328));
    impl->dialPanel->setBackgroundColor(kPanel);
    root->addView(impl->dialPanel);
    auto* dialTitle = makeLabel(VSTGUI::CRect(12, 8, 140, 28), "Graph Dials");
    dialTitle->setFont(VSTGUI::kNormalFontSmall);
    impl->dialPanel->addView(dialTitle);

    auto* editorPanel = new VSTGUI::CViewContainer(VSTGUI::CRect(18, 338, 482, 378));
    editorPanel->setBackgroundColor(kPanel);
    root->addView(editorPanel);
    editorPanel->addView(makeButton(VSTGUI::CRect(12, 6, 210, 34), this, kTagOpenEditor, "Open Web Editor"));
    impl->portLabel = makeLabel(VSTGUI::CRect(224, 10, 390, 30), "Editor Port: 9020", kAccent);
    impl->portLabel->setFont(VSTGUI::kNormalFontSmall);
    editorPanel->addView(impl->portLabel);
}

void OrchVstGuiEditor::loadPresets() {
    impl->presetNames.clear();
    impl->presetPaths.clear();
    if (auto dir = getPresetsDir(); !dir.empty() && std::filesystem::exists(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                impl->presetNames.push_back(entry.path().stem().string());
                impl->presetPaths.push_back(entry.path());
            }
        }
    }
    if (impl->presetNames.empty()) {
        impl->presetNames = {"Default Poly Sine", "Saw Filter Gain", "Pluck String Body"};
    } else {
        std::vector<size_t> order(impl->presetNames.size());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [this](size_t a, size_t b) { return impl->presetNames[a] < impl->presetNames[b]; });
        std::vector<std::string> names;
        std::vector<std::filesystem::path> paths;
        for (auto i : order) {
            names.push_back(impl->presetNames[i]);
            paths.push_back(impl->presetPaths[i]);
        }
        impl->presetNames = std::move(names);
        impl->presetPaths = std::move(paths);
    }
    if (impl->presetMenu) {
        impl->presetMenu->removeAllEntry();
        for (const auto& name : impl->presetNames) {
            impl->presetMenu->addEntry(name.c_str());
        }
        impl->presetMenu->setCurrent(0);
    }
    if (impl->presetCountLabel) {
        impl->presetCountLabel->setText((std::to_string(impl->presetNames.size()) + " presets loaded").c_str());
    }
}

void OrchVstGuiEditor::valueChanged(VSTGUI::CControl* control) {
    const auto tag = control ? control->getTag() : -1;
    if (tag == kTagTestNote) {
        playTestNote();
    } else if (tag == kTagReloadPreset || tag == kTagPresetMenu) {
        loadSelectedPreset();
    } else if (tag == kTagOpenEditor) {
        openWebEditor();
    } else if (tag >= kTagDialBase) {
        const auto index = static_cast<size_t>(tag - kTagDialBase);
        if (impl->controller && index < impl->dialKeys.size()) {
            impl->controller->setVstDial(impl->dialKeys[index], std::clamp(control->getValueNormalized(), 0.0f, 1.0f));
        }
    }
}

void OrchVstGuiEditor::updatePortLabel(int port) {
    if (impl->portLabel) {
        impl->portLabel->setText(("Editor Port: " + std::to_string(port)).c_str());
    }
}

void OrchVstGuiEditor::updateCurrentPatchLabel(const std::string& name) {
    if (impl->currentPatchLabel) {
        impl->currentPatchLabel->setText(("Current Patch: " + (name.empty() ? std::string("Untitled Patch") : name)).c_str());
    }
}

void OrchVstGuiEditor::updateDialLayout(const std::vector<std::tuple<std::string, std::string, float>>& layout) {
    impl->pendingLayout = layout;
    if (!impl->dialPanel) {
        return;
    }
    impl->dialPanel->removeAll();
    auto* dialTitle = makeLabel(VSTGUI::CRect(12, 8, 140, 28), "Graph Dials");
    dialTitle->setFont(VSTGUI::kNormalFontSmall);
    impl->dialPanel->addView(dialTitle);
    impl->dialKeys.clear();

    int visibleIndex = 0;
    for (const auto& [key, label, value] : layout) {
        if (visibleIndex >= 4) {
            break;
        }
        const auto x = 18.0 + visibleIndex * 108.0;
        const auto y = 30.0;
        impl->dialKeys.push_back(key);
        impl->dialPanel->addView(new GraphDialControl(VSTGUI::CRect(x, y, x + 92, y + 92), this, kTagDialBase + visibleIndex, label.empty() ? key : label, value));
        ++visibleIndex;
    }
    impl->dialPanel->invalid();
}

void OrchVstGuiEditor::loadSelectedPreset() {
    if (!impl->presetMenu) {
        return;
    }
    const auto index = impl->presetMenu->getCurrentIndex();
    if (index < 0 || static_cast<size_t>(index) >= impl->presetNames.size()) {
        return;
    }
    std::string jsonGraph;
    if (static_cast<size_t>(index) < impl->presetPaths.size()) {
        std::ifstream file(impl->presetPaths[index]);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            jsonGraph = buffer.str();
        }
    } else {
        const auto& name = impl->presetNames[index];
        if (name == "Default Poly Sine") {
            jsonGraph = PRESET_SINE;
        } else if (name == "Saw Filter Gain") {
            jsonGraph = PRESET_SAW;
        } else if (name == "Pluck String Body") {
            jsonGraph = PRESET_PLUCK;
        }
    }
    if (jsonGraph.empty()) {
        return;
    }
    jsonGraph = graphJsonWithPresetName(jsonGraph, impl->presetNames[index]);
    if (impl->controller) {
        impl->controller->setCurrentPatchName(impl->presetNames[index]);
    }

    try {
        char buffer[1024 * 16];
        osc::OutboundPacketStream packet(buffer, sizeof(buffer));
        packet << osc::BeginMessage("/orch_faust/load_graph") << jsonGraph.c_str() << osc::EndMessage;
        UdpTransmitSocket socket(IpEndpointName("127.0.0.1", impl->controller ? impl->controller->getActivePort() : 9020));
        socket.Send(packet.Data(), packet.Size());
        packet.Clear();
        packet << osc::BeginMessage("/orch_faust/compile") << osc::EndMessage;
        socket.Send(packet.Data(), packet.Size());
    } catch (...) {
    }
}

void OrchVstGuiEditor::playTestNote() {
    try {
        char buffer[1024];
        osc::OutboundPacketStream packet(buffer, sizeof(buffer));
        UdpTransmitSocket socket(IpEndpointName("127.0.0.1", impl->controller ? impl->controller->getActivePort() : 9020));
        packet << osc::BeginMessage("/orch_faust/note_on") << 36.0f << 1.0f << osc::EndMessage;
        socket.Send(packet.Data(), packet.Size());
        packet.Clear();
        packet << osc::BeginMessage("/orch_faust/note_off") << 36.0f << osc::EndMessage;
        socket.Send(packet.Data(), packet.Size());
    } catch (...) {
    }
}

void OrchVstGuiEditor::openWebEditor() {
    const auto url = "http://localhost:5000/orch_synth_editor?port=" + std::to_string(impl->controller ? impl->controller->getActivePort() : 9020);
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open \"" + url + "\"").c_str());
#else
    std::system(("xdg-open \"" + url + "\"").c_str());
#endif
}

} // namespace OrchFaust
