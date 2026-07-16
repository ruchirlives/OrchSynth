#include "OrchVstGuiEditor.h"

#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include <nlohmann/json.hpp>
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"

#if defined(_WIN32)
#include "vstgui/lib/platform/win32/win32factory.h"
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
#include <map>
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
constexpr int32_t kTagRefreshState = 1005;
constexpr int32_t kTagDialBase = 2000;
constexpr Steinberg::int32 kEditorWidth = 540;
constexpr Steinberg::int32 kEditorHeight = 430;
// Coordinates are in the backing-plate pixel space (the skin is 540x430).
// Set ORCH_GUI_CALIBRATION=1 while tuning the skin/control alignment.
constexpr VSTGUI::CCoord kLeftRailCenterX = 79.0;
constexpr VSTGUI::CCoord kRightRailCenterX = 462.0;

const VSTGUI::CColor kBg(8, 9, 10);
const VSTGUI::CColor kPanel(28, 29, 28);
const VSTGUI::CColor kPanel2(18, 19, 18);
const VSTGUI::CColor kPanelLift(39, 40, 38);
const VSTGUI::CColor kBorder(62, 65, 61);
const VSTGUI::CColor kText(226, 229, 220);
const VSTGUI::CColor kMuted(143, 149, 139);
const VSTGUI::CColor kAccent(151, 233, 91);
const VSTGUI::CColor kRackLine(48, 50, 46);

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
    label->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
    label->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
    label->setFontColor(color);
    return label;
}

VSTGUI::CTextButton* makeButton(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int32_t tag,
                                const std::string& title, VSTGUI::CCoord fontSize = 0.0) {
    auto* button = new VSTGUI::CTextButton(rect, listener, tag, title.c_str());
    // The skin supplies the button surfaces; this control is only the hit target and label.
    button->setRoundRadius(0.0);
    button->setFrameWidth(0.0);
    button->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
    button->setFrameColorHighlighted(VSTGUI::CColor(0, 0, 0, 0));
    button->setTextColor(kText);
    button->setTextColorHighlighted(kText);
    button->setGradient(VSTGUI::CGradient::create(0, 1, VSTGUI::CColor(0, 0, 0, 0), VSTGUI::CColor(0, 0, 0, 0)));
    button->setGradientHighlighted(VSTGUI::CGradient::create(0, 1, VSTGUI::CColor(0, 0, 0, 0), VSTGUI::CColor(0, 0, 0, 0)));
    if (fontSize > 0.0) {
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", fontSize);
        button->setFont(font.get());
    }
    return button;
}

std::string guiAssetPath(const std::string& filename) {
    std::string dir;
#if defined(_WIN32)
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&guiAssetPath),
        &module);
    char path[MAX_PATH] = {};
    if (module && GetModuleFileNameA(module, path, MAX_PATH) > 0) {
        dir = path;
        const auto slash = dir.find_last_of("\\/");
        if (slash != std::string::npos) {
            dir = dir.substr(0, slash);
        }
    }
#else
    dir = ".";
#endif
#if defined(_WIN32)
    return dir + "\\gui\\" + filename;
#else
    return dir + "/gui/" + filename;
#endif
}

VSTGUI::SharedPointer<VSTGUI::CBitmap> loadGuiBitmap(const std::string& filename) {
    static std::map<std::string, std::string> assetPathCache;
#if defined(_WIN32)
    // VSTGUI resolves string resources relative to the platform factory base path.
    // Configure it once so assets can remain alongside the VST3 binary in gui/.
    static const bool resourceBasePathConfigured = [] {
        auto* factory = VSTGUI::getPlatformFactory().asWin32Factory();
        if (factory) {
            factory->setResourceBasePath(guiAssetPath("" ).c_str());
        }
        return factory != nullptr;
    }();
    (void)resourceBasePathConfigured;
#endif
    auto [it, inserted] = assetPathCache.emplace(filename, filename);
    auto* bitmap = new VSTGUI::CBitmap(VSTGUI::CResourceDescription(it->second.c_str()));
    if (!bitmap->isLoaded()) {
        bitmap->forget();
        return nullptr;
    }
    return VSTGUI::SharedPointer<VSTGUI::CBitmap>(bitmap);
}

json loadGuiLayout() {
    try {
        std::ifstream file(guiAssetPath("orch_gui_layout.json"));
        if (file.is_open()) {
            return json::parse(file);
        }
    } catch (...) {
    }
    return json::object();
}

// Layout JSON uses backing-plate coordinates. Containers are an implementation
// detail, so convert from the shared 540x430 canvas into each child view's
// local coordinate space at the point of use.
VSTGUI::CRect layoutRect(const json& layout, const char* collection, const char* name, const VSTGUI::CRect& fallback) {
    try {
        const auto collectionIt = layout.find(collection);
        if (collectionIt == layout.end() || !collectionIt->is_object()) {
            return fallback;
        }
        const auto itemIt = collectionIt->find(name);
        if (itemIt == collectionIt->end() || !itemIt->is_object()) {
            return fallback;
        }
        const auto& item = *itemIt;
        const auto x = item.value("x", static_cast<double>(fallback.left));
        const auto y = item.value("y", static_cast<double>(fallback.top));
        const auto width = item.value("w", static_cast<double>(fallback.getWidth()));
        const auto height = item.value("h", static_cast<double>(fallback.getHeight()));
        return VSTGUI::CRect(x, y, x + width, y + height);
    } catch (...) {
        return fallback;
    }
}

VSTGUI::CRect layoutRectInParent(const json& layout, const char* collection, const char* name,
                                 const VSTGUI::CRect& absoluteFallback, const VSTGUI::CRect& parentBounds) {
    const auto absolute = layoutRect(layout, collection, name, absoluteFallback);
    return VSTGUI::CRect(
        absolute.left - parentBounds.left,
        absolute.top - parentBounds.top,
        absolute.right - parentBounds.left,
        absolute.bottom - parentBounds.top);
}

VSTGUI::CRect layoutGroupBounds(const json& layout, const char* name, const VSTGUI::CRect& absoluteFallback,
                                const VSTGUI::CRect& parentBounds) {
    const auto bounds = layoutRect(layout, "groups", name, absoluteFallback);
    try {
        const auto groupsIt = layout.find("groups");
        if (groupsIt == layout.end() || !groupsIt->is_object()) {
            return bounds;
        }
        const auto groupIt = groupsIt->find(name);
        if (groupIt == groupsIt->end() || !groupIt->is_object() || !groupIt->contains("parent")) {
            return bounds;
        }
        // Earlier positioner versions persisted child-panel coordinates relative
        // to their rail. Accept that form as well as the current absolute form.
        if (bounds.left < parentBounds.left) {
            return VSTGUI::CRect(bounds.left + parentBounds.left, bounds.top + parentBounds.top,
                                 bounds.right + parentBounds.left, bounds.bottom + parentBounds.top);
        }
    } catch (...) {
    }
    return bounds;
}

VSTGUI::CCoord layoutFontSize(const json& layout, const char* name, VSTGUI::CCoord fallback) {
    try {
        const auto elementsIt = layout.find("elements");
        if (elementsIt == layout.end() || !elementsIt->is_object()) {
            return fallback;
        }
        const auto itemIt = elementsIt->find(name);
        if (itemIt == elementsIt->end() || !itemIt->is_object()) {
            return fallback;
        }
        return std::clamp(static_cast<VSTGUI::CCoord>(itemIt->value("fontSize", fallback)), 6.0, 24.0);
    } catch (...) {
        return fallback;
    }
}

class BitmapView : public VSTGUI::CView {
public:
    BitmapView(const VSTGUI::CRect& rect, const std::string& filename, float alpha = 1.0f)
        : CView(rect), bitmap(loadGuiBitmap(filename)), alpha(alpha) {}

    void draw(VSTGUI::CDrawContext* context) override {
        if (bitmap) {
            bitmap->draw(context, getViewSize(), VSTGUI::CPoint(0, 0), alpha);
        }
        setDirty(false);
    }

private:
    VSTGUI::SharedPointer<VSTGUI::CBitmap> bitmap;
    float alpha;

    CLASS_METHODS_NOCOPY(BitmapView, VSTGUI::CView)
};

class StyledPanel : public VSTGUI::CViewContainer {
public:
    StyledPanel(const VSTGUI::CRect& rect, VSTGUI::CColor fill, VSTGUI::CColor stroke, VSTGUI::CCoord radius = 8.0)
        : CViewContainer(rect), fill(fill), stroke(stroke), radius(radius) {
        setTransparency(true);
    }

    void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override {
        const auto view = getViewSize();
        const VSTGUI::CRect size(0, 0, view.getWidth(), view.getHeight());
        auto path = VSTGUI::owned(context->createRoundRectGraphicsPath(size, radius));
        context->setFillColor(fill);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        context->setFrameColor(VSTGUI::CColor(37, 39, 35));
        for (auto y = size.top + 3.0; y < size.bottom; y += 4.0) {
            context->drawLine(VSTGUI::CPoint(size.left + 2.0, y), VSTGUI::CPoint(size.right - 2.0, y));
        }
        context->setFrameColor(stroke);
        context->setLineWidth(1.0);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

private:
    VSTGUI::CColor fill;
    VSTGUI::CColor stroke;
    VSTGUI::CCoord radius;

    CLASS_METHODS_NOCOPY(StyledPanel, VSTGUI::CViewContainer)
};

class RackBackground : public VSTGUI::CViewContainer {
public:
    explicit RackBackground(const VSTGUI::CRect& rect) : CViewContainer(rect), skin(loadGuiBitmap("orch_full_skin_dynamic.png")) {
        setTransparency(true);
    }

    void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override {
        const auto size = getViewSize();
        if (skin) {
            skin->draw(context, size);
            const auto* calibration = std::getenv("ORCH_GUI_CALIBRATION");
            if (calibration && calibration[0] == '1') {
                context->setFrameColor(VSTGUI::CColor(255, 64, 64, 210));
                context->setLineWidth(1.0);
                drawCrosshair(context, kLeftRailCenterX, 80.0);
                drawCrosshair(context, kRightRailCenterX, 80.0);
                drawCrosshair(context, kLeftRailCenterX, 176.0);
                drawCrosshair(context, kLeftRailCenterX, 306.0);
            }
            return;
        }

        // Keep bitmap failures visually obvious while developing the skin.
        context->setFillColor(VSTGUI::CColor(86, 24, 28));
        context->drawRect(size, VSTGUI::kDrawFilled);
    }

private:
    static void drawCrosshair(VSTGUI::CDrawContext* context, VSTGUI::CCoord x, VSTGUI::CCoord y) {
        context->drawLine(VSTGUI::CPoint(x - 8.0, y), VSTGUI::CPoint(x + 8.0, y));
        context->drawLine(VSTGUI::CPoint(x, y - 8.0), VSTGUI::CPoint(x, y + 8.0));
        context->drawEllipse(VSTGUI::CRect(x - 3.0, y - 3.0, x + 3.0, y + 3.0), VSTGUI::kDrawStroked);
    }

    VSTGUI::SharedPointer<VSTGUI::CBitmap> skin;
    CLASS_METHODS_NOCOPY(RackBackground, VSTGUI::CViewContainer)
};

class SoftOptionMenu : public VSTGUI::COptionMenu {
public:
    SoftOptionMenu(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int32_t tag)
        : COptionMenu(rect, listener, tag) {}

    void draw(VSTGUI::CDrawContext* context) override {
        const auto size = getViewSize();
        context->setFont(VSTGUI::kNormalFontSmall);
        context->setFontColor(kAccent);
        const auto current = getCurrentIndex();
        const char* title = "";
        if (current >= 0) {
            if (auto* entry = getEntry(current)) {
                title = entry->getTitle();
            }
        }
        context->drawString(title, VSTGUI::CRect(size.left + 12, size.top + 4, size.right - 24, size.bottom - 3), VSTGUI::kLeftText);
        setDirty(false);
    }

private:
    CLASS_METHODS_NOCOPY(SoftOptionMenu, VSTGUI::COptionMenu)
};

class ScopeDisplay : public VSTGUI::CViewContainer {
public:
    explicit ScopeDisplay(const VSTGUI::CRect& rect) : CViewContainer(rect) {
        setTransparency(true);
    }

    void setPatchName(const std::string& next) {
        patchName = next.empty() ? "Untitled Patch" : next;
        invalid();
    }

    void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override {
        const auto view = getViewSize();
        const VSTGUI::CRect size(0, 0, view.getWidth(), view.getHeight());
        auto screen = size;
        screen.inset(4.0, 10.0);
        context->setFillColor(VSTGUI::CColor(9, 15, 10));
        context->setFrameColor(VSTGUI::CColor(24, 32, 23));
        context->drawRect(screen, VSTGUI::kDrawFilledAndStroked);
        context->setFrameColor(VSTGUI::CColor(18, 26, 18));
        for (auto y = screen.top + 8.0; y < screen.bottom; y += 14.0) {
            context->drawLine(VSTGUI::CPoint(screen.left, y), VSTGUI::CPoint(screen.right, y));
        }

        context->setFont(VSTGUI::kNormalFontVerySmall);
        context->setFontColor(kAccent);
        context->drawString("CURRENT PATCH", VSTGUI::CRect(screen.left, screen.top + 11, screen.right, screen.top + 27), VSTGUI::kCenterText);
        context->setFont(VSTGUI::kNormalFontSmall);
        context->drawString(patchName.c_str(), VSTGUI::CRect(screen.left, screen.top + 30, screen.right, screen.top + 49), VSTGUI::kCenterText);

        VSTGUI::CPoint last;
        bool hasLast = false;
        context->setFrameColor(VSTGUI::CColor(162, 242, 112));
        context->setLineWidth(1.4);
        const auto width = screen.getWidth();
        const auto waveformTop = screen.top + 58.0;
        const auto waveformBottom = screen.bottom - 8.0;
        const auto mid = (waveformTop + waveformBottom) * 0.5;
        const auto amp = (waveformBottom - waveformTop) * 0.42;
        for (int i = 0; i <= 120; ++i) {
            const auto t = static_cast<double>(i) / 120.0;
            const auto x = screen.left + t * width;
            const auto y = mid + std::sin(t * 10.0 * 3.14159265358979323846) * amp;
            VSTGUI::CPoint p(x, y);
            if (hasLast) {
                context->drawLine(last, p);
            }
            last = p;
            hasLast = true;
        }
    }

private:
    std::string patchName = "Default Poly Sine";

    CLASS_METHODS_NOCOPY(ScopeDisplay, VSTGUI::CViewContainer)
};

class DecorativeKnob : public VSTGUI::CView {
public:
    DecorativeKnob(const VSTGUI::CRect& rect, std::string label, float value)
        : CView(rect), label(std::move(label)), value(std::clamp(value, 0.0f, 1.0f)) {}

    void draw(VSTGUI::CDrawContext* context) override {
        const auto size = getViewSize();
        auto knob = VSTGUI::CRect(size.left + 9, size.top + 2, size.right - 9, size.top + size.getWidth() - 16);
        drawKnob(context, knob, value, false);
        context->setFont(VSTGUI::kNormalFontVerySmall);
        context->setFontColor(kText);
        context->drawString(label.c_str(), VSTGUI::CRect(size.left, size.bottom - 17, size.right, size.bottom), VSTGUI::kCenterText);
        setDirty(false);
    }

private:
    static void drawKnob(VSTGUI::CDrawContext* context, VSTGUI::CRect knob, float value, bool leds) {
        const auto cx = (knob.left + knob.right) * 0.5;
        const auto cy = (knob.top + knob.bottom) * 0.5;
        const auto radius = knob.getWidth() * 0.5;
        if (leds) {
            context->setFillColor(kAccent);
            for (int i = 0; i < 18; ++i) {
                const auto a = (-140.0 + i * (280.0 / 17.0)) * 3.14159265358979323846 / 180.0;
                const auto dot = VSTGUI::CRect(cx + std::cos(a) * (radius + 7.0) - 1.5, cy + std::sin(a) * (radius + 7.0) - 1.5,
                                                cx + std::cos(a) * (radius + 7.0) + 1.5, cy + std::sin(a) * (radius + 7.0) + 1.5);
                context->drawEllipse(dot, VSTGUI::kDrawFilled);
            }
        }
        context->setFillColor(VSTGUI::CColor(8, 9, 9));
        context->setFrameColor(VSTGUI::CColor(58, 60, 55));
        context->setLineWidth(2.0);
        context->drawEllipse(knob, VSTGUI::kDrawFilledAndStroked);
        auto inner = knob;
        inner.inset(7, 7);
        context->setFillColor(VSTGUI::CColor(25, 26, 24));
        context->setFrameColor(VSTGUI::CColor(15, 16, 15));
        context->drawEllipse(inner, VSTGUI::kDrawFilledAndStroked);
        const auto angle = (-135.0 + value * 270.0) * 3.14159265358979323846 / 180.0;
        context->setFrameColor(VSTGUI::CColor(190, 195, 180));
        context->setLineWidth(2.0);
        context->drawLine(VSTGUI::CPoint(cx, cy), VSTGUI::CPoint(cx + std::cos(angle) * (radius - 8.0), cy + std::sin(angle) * (radius - 8.0)));
    }

    std::string label;
    float value;

    CLASS_METHODS_NOCOPY(DecorativeKnob, VSTGUI::CView)
};

class GraphDialControl : public VSTGUI::CControl {
public:
    GraphDialControl(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int32_t tag, std::string label, float value, bool compact = false)
        : CControl(rect, listener, tag), label(std::move(label)), compact(compact), knobBitmap(loadGuiBitmap("orch_knob_frames.png")) {
        setMin(0.0f);
        setMax(1.0f);
        setValueNormalized(std::clamp(value, 0.0f, 1.0f));
    }

    void draw(VSTGUI::CDrawContext* context) override {
        const auto size = getViewSize();
        const auto value = std::clamp(getValueNormalized(), 0.0f, 1.0f);
        constexpr auto bitmapSize = 62.0;
        // Right-rail controls reserve a separate text column, preventing the
        // dial label/value from intersecting the rotating bitmap.
        const auto bitmapLeft = compact
            ? size.left + (size.getWidth() - bitmapSize) * 0.5
            : size.left + 4.0;
        const VSTGUI::CRect bitmapRect(bitmapLeft, size.top, bitmapLeft + bitmapSize, size.top + bitmapSize);

        if (knobBitmap) {
            const auto frameIndex = std::clamp(static_cast<int>(value * 100.0f + 0.5f), 0, 100);
            knobBitmap->draw(context, bitmapRect, VSTGUI::CPoint(0, frameIndex * bitmapSize));
        } else {
            auto knob = bitmapRect;
            knob.inset(4.0, 4.0);
            context->setFillColor(VSTGUI::CColor(10, 13, 19));
            context->setFrameColor(VSTGUI::CColor(61, 64, 58));
            context->setLineWidth(2.0);
            context->drawEllipse(knob, VSTGUI::kDrawFilledAndStroked);

            auto inner = knob;
            inner.inset(7, 7);
            context->setFillColor(kPanelLift);
            context->setFrameColor(VSTGUI::CColor(99, 122, 151));
            context->drawEllipse(inner, VSTGUI::kDrawFilledAndStroked);
        }

        context->setFontColor(kText);
        char valueText[16] = {};
        snprintf(valueText, sizeof(valueText), "%d%%", static_cast<int>(value * 100.0f + 0.5f));
        context->setFontColor(kAccent);
        if (compact) {
            context->setFont(VSTGUI::kNormalFontVerySmall);
            context->setFontColor(kText);
            context->drawString(label.c_str(), VSTGUI::CRect(size.left, size.top + 56, size.right, size.top + 72), VSTGUI::kCenterText);
            context->setFontColor(kAccent);
            context->drawString(valueText, VSTGUI::CRect(size.left, size.top + 72, size.right, size.top + 88), VSTGUI::kCenterText);
        } else {
            context->setFont(VSTGUI::kNormalFontVerySmall);
            context->setFontColor(kText);
            context->drawString(label.c_str(), VSTGUI::CRect(size.left + 70, size.top + 17, size.right, size.top + 34), VSTGUI::kLeftText);
            context->setFontColor(kAccent);
            context->drawString(valueText, VSTGUI::CRect(size.left + 70, size.top + 36, size.right, size.top + 53), VSTGUI::kLeftText);
        }
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
        const auto top = size.top + 4;
        const auto bottom = size.top + 58;
        const auto value = std::clamp(static_cast<float>((bottom - where.y) / (bottom - top)), 0.0f, 1.0f);
        if (value != getValueNormalized()) {
            setValueNormalized(value);
            valueChanged();
            invalid();
        }
    }

    std::string label;
    bool compact;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> knobBitmap;

    CLASS_METHODS_NOCOPY(GraphDialControl, VSTGUI::CControl)
};

} // namespace

struct OrchVstGuiEditor::Impl {
    explicit Impl(OrchFaustController* controller) : controller(controller) {}

    OrchFaustController* controller = nullptr;
    VSTGUI::CViewContainer* root = nullptr;
    VSTGUI::CViewContainer* dialPanel = nullptr;
    VSTGUI::CViewContainer* extraDialPanel = nullptr;
    VSTGUI::CTextLabel* portLabel = nullptr;
    VSTGUI::CTextLabel* currentPatchLabel = nullptr;
    ScopeDisplay* scopeDisplay = nullptr;
    VSTGUI::CTextLabel* presetCountLabel = nullptr;
    VSTGUI::COptionMenu* presetMenu = nullptr;
    std::vector<std::string> presetNames;
    std::vector<std::filesystem::path> presetPaths;
    std::vector<std::string> dialKeys;
    std::vector<std::tuple<std::string, std::string, float>> pendingLayout;
    json guiLayout;
    VSTGUI::CRect dialPanelBounds;
    VSTGUI::CRect extraDialPanelBounds;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> noteOffTimer;
    bool requestedInitialState = false;
};

OrchVstGuiEditor::OrchVstGuiEditor(OrchFaustController* controller)
    : VSTGUIEditor(controller, nullptr), impl(new Impl(controller)) {
    Steinberg::ViewRect r(0, 0, kEditorWidth, kEditorHeight);
    setRect(r);
}

OrchVstGuiEditor::~OrchVstGuiEditor() {
    delete impl;
}

bool PLUGIN_API OrchVstGuiEditor::open(void* parent, const VSTGUI::PlatformType& platformType) {
    frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, kEditorWidth, kEditorHeight), this);
    if (!frame->open(parent, platformType)) {
        frame->forget();
        frame = nullptr;
        return false;
    }
    rebuild();
    loadPresets();
    impl->requestedInitialState = false;
    return true;
}

void PLUGIN_API OrchVstGuiEditor::close() {
    if (impl->controller) {
        impl->controller->clearActiveView();
    }
    impl->root = nullptr;
    impl->dialPanel = nullptr;
    impl->extraDialPanel = nullptr;
    impl->portLabel = nullptr;
    impl->currentPatchLabel = nullptr;
    impl->presetCountLabel = nullptr;
    impl->presetMenu = nullptr;
    if (impl->noteOffTimer) {
        impl->noteOffTimer->stop();
        impl->noteOffTimer = nullptr;
    }
    if (frame) {
        frame->close();
        frame = nullptr;
    }
}

VSTGUI::CMessageResult OrchVstGuiEditor::notify(VSTGUI::CBaseObject* sender, const char* message) {
    if (message == VSTGUI::CVSTGUITimer::kMsgTimer && !impl->requestedInitialState) {
        impl->requestedInitialState = true;
        requestInitialState();
    }
    return VSTGUIEditor::notify(sender, message);
}

void OrchVstGuiEditor::rebuild() {
    impl->guiLayout = loadGuiLayout();
    const auto absoluteRect = [this](const char* collection, const char* name, const VSTGUI::CRect& fallback) {
        return layoutRect(impl->guiLayout, collection, name, fallback);
    };
    const auto localRect = [this](const char* collection, const char* name,
                                  const VSTGUI::CRect& absoluteFallback, const VSTGUI::CRect& parentBounds) {
        return layoutRectInParent(impl->guiLayout, collection, name, absoluteFallback, parentBounds);
    };
    auto* root = new RackBackground(VSTGUI::CRect(0, 0, kEditorWidth, kEditorHeight));
    impl->root = root;
    frame->addView(root);

    const auto leftRailBounds = absoluteRect("groups", "leftRail", VSTGUI::CRect(13, 0, 143, 430));
    const auto centerPanelBounds = absoluteRect("groups", "centerPanel", VSTGUI::CRect(155, 0, 385, 430));
    const auto rightRailBounds = absoluteRect("groups", "rightRail", VSTGUI::CRect(397, 0, 527, 430));
    auto* leftRail = new VSTGUI::CViewContainer(leftRailBounds);
    leftRail->setTransparency(true);
    root->addView(leftRail);
    auto* centerPanel = new VSTGUI::CViewContainer(centerPanelBounds);
    centerPanel->setTransparency(true);
    root->addView(centerPanel);
    auto* rightRail = new VSTGUI::CViewContainer(rightRailBounds);
    rightRail->setTransparency(true);
    root->addView(rightRail);

    auto* perfTitle = makeLabel(localRect("elements", "performanceTitle", VSTGUI::CRect(37, 24, 119, 44), leftRailBounds), "PERFORMANCE", kMuted);
    perfTitle->setFont(VSTGUI::kNormalFontVerySmall);
    leftRail->addView(perfTitle);
    auto* playButton = makeButton(localRect("elements", "playButton", VSTGUI::CRect(48, 49, 110, 111), leftRailBounds), this, kTagTestNote, "");
    if (auto playBitmap = loadGuiBitmap("orch_button_play_fit.png")) {
        // CTextButton defaults to left-aligned icons. With an empty title that
        // shifts the 52 px play bitmap 5 px left inside its 62 px hit target.
        playButton->setIconPosition(VSTGUI::CDrawMethods::kIconCenterAbove);
        playButton->setIcon(playBitmap.get());
        playButton->setIconHighlighted(playBitmap.get());
    }
    leftRail->addView(playButton);
    auto* playLabel = makeLabel(localRect("elements", "playLabel", VSTGUI::CRect(13, 109, 143, 126), leftRailBounds), "PLAY C1 NOTE", kText);
    playLabel->setFont(VSTGUI::kNormalFontVerySmall);
    leftRail->addView(playLabel);

    auto* patchTitle = makeLabel(localRect("elements", "patchTitle", VSTGUI::CRect(13, 138, 143, 156), leftRailBounds), "PATCH", kMuted);
    patchTitle->setFont(VSTGUI::kNormalFontVerySmall);
    leftRail->addView(patchTitle);
    impl->presetMenu = new SoftOptionMenu(localRect("elements", "presetMenu", VSTGUI::CRect(19, 154, 137, 182), leftRailBounds), this, kTagPresetMenu);
    impl->presetMenu->setBackColor(VSTGUI::CColor(236, 242, 250));
    impl->presetMenu->setFrameColor(kBorder);
    impl->presetMenu->setFontColor(VSTGUI::CColor(17, 24, 39));
    leftRail->addView(impl->presetMenu);
    impl->presetCountLabel = makeLabel(localRect("elements", "presetCount", VSTGUI::CRect(27, 184, 129, 199), leftRailBounds), "0 presets loaded", kMuted);
    impl->presetCountLabel->setFont(VSTGUI::kNormalFontVerySmall);
    leftRail->addView(impl->presetCountLabel);
    leftRail->addView(makeButton(localRect("elements", "refreshButton", VSTGUI::CRect(35, 214, 121, 238), leftRailBounds), this, kTagRefreshState, "REFRESH", layoutFontSize(impl->guiLayout, "refreshButton", 12.0)));
    leftRail->addView(makeButton(localRect("elements", "reloadButton", VSTGUI::CRect(35, 244, 121, 268), leftRailBounds), this, kTagReloadPreset, "RELOAD", layoutFontSize(impl->guiLayout, "reloadButton", 12.0)));

    auto* systemTitle = makeLabel(localRect("elements", "systemTitle", VSTGUI::CRect(13, 278, 143, 297), leftRailBounds), "SYSTEM", kMuted);
    systemTitle->setFont(VSTGUI::kNormalFontVerySmall);
    leftRail->addView(systemTitle);
    auto* webEditorButton = makeButton(localRect("elements", "webEditorButton", VSTGUI::CRect(41, 318, 135, 342), leftRailBounds), this, kTagOpenEditor, "WEB EDITOR", layoutFontSize(impl->guiLayout, "webEditorButton", 9.0));
    leftRail->addView(webEditorButton);
    impl->portLabel = makeLabel(localRect("elements", "portLabel", VSTGUI::CRect(29, 353, 127, 379), leftRailBounds), "PORT 9020", kAccent);
    impl->portLabel->setFont(VSTGUI::kNormalFontVerySmall);
    leftRail->addView(impl->portLabel);

    auto* title = makeLabel(localRect("elements", "title", VSTGUI::CRect(151, 7, 381, 29), centerPanelBounds), "O R C H  S Y N T H");
    title->setFont(VSTGUI::kNormalFontBig);
    centerPanel->addView(title);
    auto* subtitle = makeLabel(localRect("elements", "subtitle", VSTGUI::CRect(155, 35, 385, 51), centerPanelBounds), "V S T 3  C O N T R O L L E R", kMuted);
    subtitle->setFont(VSTGUI::kNormalFontVerySmall);
    centerPanel->addView(subtitle);

    impl->scopeDisplay = new ScopeDisplay(localRect("elements", "scope", VSTGUI::CRect(169, 50, 371, 186), centerPanelBounds));
    centerPanel->addView(impl->scopeDisplay);
    impl->currentPatchLabel = makeLabel(VSTGUI::CRect(0, 0, 1, 1), "", kAccent);
    impl->currentPatchLabel->setVisible(false);
    centerPanel->addView(impl->currentPatchLabel);

    auto* brand = makeLabel(localRect("elements", "brand", VSTGUI::CRect(155, 370, 385, 390), centerPanelBounds), "ORCH", VSTGUI::CColor(111, 113, 106));
    brand->setFont(VSTGUI::kNormalFontSmall);
    centerPanel->addView(brand);

    auto* dialRailTitle = makeLabel(localRect("elements", "dialTitle", VSTGUI::CRect(397, 24, 517, 44), rightRailBounds), "GRAPH DIALS", kMuted);
    dialRailTitle->setFont(VSTGUI::kNormalFontVerySmall);
    rightRail->addView(dialRailTitle);
    // Graph-dial slots are authored in canvas coordinates.  Keep their
    // container coextensive with the rail so a slot can overlap a decorative
    // group boundary without clipping the knob bitmap or moving its centre.
    impl->dialPanelBounds = rightRailBounds;
    impl->dialPanel = new VSTGUI::CViewContainer(VSTGUI::CRect(
        impl->dialPanelBounds.left - rightRailBounds.left,
        impl->dialPanelBounds.top - rightRailBounds.top,
        impl->dialPanelBounds.right - rightRailBounds.left,
        impl->dialPanelBounds.bottom - rightRailBounds.top));
    impl->dialPanel->setTransparency(true);
    rightRail->addView(impl->dialPanel);

    impl->extraDialPanelBounds = centerPanelBounds;
    impl->extraDialPanel = new VSTGUI::CViewContainer(VSTGUI::CRect(
        impl->extraDialPanelBounds.left - centerPanelBounds.left,
        impl->extraDialPanelBounds.top - centerPanelBounds.top,
        impl->extraDialPanelBounds.right - centerPanelBounds.left,
        impl->extraDialPanelBounds.bottom - centerPanelBounds.top));
    impl->extraDialPanel->setTransparency(true);
    centerPanel->addView(impl->extraDialPanel);

    const auto ioPanelBounds = layoutGroupBounds(impl->guiLayout, "ioPanel", VSTGUI::CRect(409, 298, 505, 384), rightRailBounds);
    auto* ioPanel = new VSTGUI::CViewContainer(VSTGUI::CRect(
        ioPanelBounds.left - rightRailBounds.left,
        ioPanelBounds.top - rightRailBounds.top,
        ioPanelBounds.right - rightRailBounds.left,
        ioPanelBounds.bottom - rightRailBounds.top));
    ioPanel->setTransparency(true);
    rightRail->addView(ioPanel);
    auto* ioTitle = makeLabel(localRect("elements", "ioTitle", VSTGUI::CRect(409, 306, 505, 322), ioPanelBounds), "I/O", kMuted);
    ioTitle->setFont(VSTGUI::kNormalFontVerySmall);
    ioPanel->addView(ioTitle);
}

void OrchVstGuiEditor::requestInitialState() {
    if (!impl->controller) {
        return;
    }
    impl->controller->requestPortFromProcessor(this);
    impl->controller->requestCurrentPatchName();
    impl->controller->requestDialLayout();
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
    const auto activated = control && control->getValueNormalized() > 0.5f;
    if (tag == kTagTestNote) {
        if (activated) {
            playTestNote();
        }
    } else if (tag == kTagPresetMenu) {
        loadSelectedPreset();
    } else if (tag == kTagReloadPreset && activated) {
        loadSelectedPreset();
    } else if (tag == kTagRefreshState) {
        if (activated) {
            requestInitialState();
        }
    } else if (tag == kTagOpenEditor) {
        if (activated) {
            openWebEditor();
        }
    } else if (tag >= kTagDialBase) {
        const auto index = static_cast<size_t>(tag - kTagDialBase);
        if (impl->controller && index < impl->dialKeys.size()) {
            impl->controller->setVstDial(impl->dialKeys[index], std::clamp(control->getValueNormalized(), 0.0f, 1.0f));
        }
    }
}

void OrchVstGuiEditor::updatePortLabel(int port) {
    if (impl->portLabel) {
        impl->portLabel->setText(("PORT " + std::to_string(port)).c_str());
    }
}

void OrchVstGuiEditor::updateCurrentPatchLabel(const std::string& name) {
    if (impl->currentPatchLabel) {
        impl->currentPatchLabel->setText((name.empty() ? std::string("Untitled Patch") : name).c_str());
    }
    if (impl->scopeDisplay) {
        impl->scopeDisplay->setPatchName(name);
    }
}

void OrchVstGuiEditor::updateDialLayout(const std::vector<std::tuple<std::string, std::string, float>>& layout) {
    impl->pendingLayout = layout;
    if (!impl->dialPanel || !impl->extraDialPanel) {
        return;
    }
    impl->dialPanel->removeAll();
    impl->extraDialPanel->removeAll();
    impl->dialKeys.clear();

    int visibleIndex = 0;
    for (const auto& [key, label, value] : layout) {
        if (visibleIndex >= 9) {
            break;
        }
        impl->dialKeys.push_back(key);
        const auto dialLabel = label.empty() ? key : label;
        if (visibleIndex < 3) {
            const auto slotName = "rightDial" + std::to_string(visibleIndex + 1);
            const auto y = 4.0 + visibleIndex * 78.0;
            impl->dialPanel->addView(new GraphDialControl(
                layoutRectInParent(impl->guiLayout, "elements", slotName.c_str(),
                                   VSTGUI::CRect(397, 56 + visibleIndex * 78.0, 527, 126 + visibleIndex * 78.0),
                                   impl->dialPanelBounds),
                this, kTagDialBase + visibleIndex, dialLabel, value));
        } else {
            const auto extraIndex = visibleIndex - 3;
            const auto column = extraIndex % 3;
            const auto row = extraIndex / 3;
            const auto slotName = "extraDial" + std::to_string(extraIndex + 1);
            impl->extraDialPanel->addView(new GraphDialControl(
                layoutRectInParent(impl->guiLayout, "elements", slotName.c_str(),
                                   VSTGUI::CRect(156 + column * 76.0, 195 + row * 92.0,
                                                 232 + column * 76.0, 285 + row * 92.0),
                                   impl->extraDialPanelBounds),
                this, kTagDialBase + visibleIndex, dialLabel, value, true));
        }
        ++visibleIndex;
    }
    impl->dialPanel->invalid();
    impl->extraDialPanel->invalid();
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
        if (impl->noteOffTimer) {
            impl->noteOffTimer->stop();
        }
        char buffer[1024];
        osc::OutboundPacketStream packet(buffer, sizeof(buffer));
        UdpTransmitSocket socket(IpEndpointName("127.0.0.1", impl->controller ? impl->controller->getActivePort() : 9020));
        packet << osc::BeginMessage("/orch_faust/note_on") << 36.0f << 1.0f << osc::EndMessage;
        socket.Send(packet.Data(), packet.Size());
        impl->noteOffTimer = VSTGUI::owned(new VSTGUI::CVSTGUITimer([this](VSTGUI::CVSTGUITimer* timer) {
            timer->stop();
            try {
                char noteOffBuffer[1024];
                osc::OutboundPacketStream noteOffPacket(noteOffBuffer, sizeof(noteOffBuffer));
                noteOffPacket << osc::BeginMessage("/orch_faust/note_off") << 36.0f << osc::EndMessage;
                UdpTransmitSocket noteOffSocket(IpEndpointName("127.0.0.1", impl->controller ? impl->controller->getActivePort() : 9020));
                noteOffSocket.Send(noteOffPacket.Data(), noteOffPacket.Size());
            } catch (...) {
            }
        }, 500, true));
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
