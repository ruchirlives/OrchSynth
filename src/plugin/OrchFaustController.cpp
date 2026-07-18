#include "OrchFaustController.h"
#include "OrchVstGuiEditor.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/pluginview.h"
#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"
#include <nlohmann/json.hpp>
#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#endif
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace OrchFaust {

namespace {
using json = nlohmann::json;

std::string fromTCharString(const Steinberg::Vst::TChar* value) {
    if (!value) {
        return "";
    }

    std::string result;
    while (*value) {
        const auto ch = static_cast<unsigned int>(*value++);
        result.push_back(ch <= 0x7f ? static_cast<char>(ch) : '?');
    }
    return result;
}

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
}

// UUID definition for the controller
const Steinberg::FUID OrchFaustControllerUID(0x8D385BAA, 0x8C1E4F43, 0x9330922D, 0xCD26FB2F);

// Preset strings
const char* PRESET_SINE = R"({
  "version": 1,
  "name": "Default Poly Sine",
  "nodes": [
    { "id": "osc1", "type": "sine", "params": { "freq": 440.0, "keyboard_tracking": 1.0 } },
    { "id": "gain1", "type": "gain", "params": { "gain": 0.5 } },
    { "id": "out1", "type": "output" }
  ],
  "connections": [
    { "source": "osc1", "target": "gain1" },
    { "source": "gain1", "target": "out1" }
  ],
  "output": "out1"
})";

const char* PRESET_SAW = R"({
  "version": 1,
  "name": "Saw Filter Gain",
  "nodes": [
    { "id": "osc1", "type": "saw", "params": { "freq": 440.0, "keyboard_tracking": 1.0 } },
    { "id": "filter1", "type": "lowpass", "params": { "cutoff": 2000.0, "resonance": 0.707 } },
    { "id": "gain1", "type": "gain", "params": { "gain": 0.25 } },
    { "id": "out1", "type": "output" }
  ],
  "connections": [
    { "source": "osc1", "target": "filter1" },
    { "source": "filter1", "target": "gain1" },
    { "source": "gain1", "target": "out1" }
  ],
  "output": "out1"
})";

const char* PRESET_PLUCK = R"({
  "version": 1,
  "name": "Pluck String Body",
  "nodes": [
    { "id": "noise1", "type": "noise", "params": {} },
    { "id": "adsr1", "type": "adsr", "params": { "attack": 0.001, "decay": 0.1, "sustain": 0.0, "release": 0.1 } },
    { "id": "burst_gate", "type": "gain", "params": { "gain": 1.0 } },
    { "id": "string1", "type": "karplus_string", "params": { "freq": 220.0, "damping": 4.0 } },
    { "id": "body1", "type": "body_resonator", "params": { "size": 0.5, "brightness": 0.5 } },
    { "id": "gain1", "type": "gain", "params": { "gain": 0.5 } },
    { "id": "out1", "type": "output" }
  ],
  "connections": [
    { "source": "noise1", "target": "burst_gate" },
    { "source": "adsr1", "target": "burst_gate" },
    { "source": "burst_gate", "target": "string1" },
    { "source": "string1", "target": "body1" },
    { "source": "body1", "target": "gain1" },
    { "source": "gain1", "target": "out1" }
  ],
  "output": "out1"
})";

#if defined(_WIN32)
static std::filesystem::path getPresetsDir() {
    const char* home = std::getenv("HOME");
    if (home) {
        auto path = std::filesystem::path(home) / "Documents" / "Orch" / "modules" / "orch_synth_editor" / "presets";
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    // Fall back through Windows Documents locations for hosts that do not
    // inherit the HOME environment variable.
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
        auto presetsPath = std::filesystem::path(path) / "Orch" / "modules" / "orch_synth_editor" / "presets";
        if (std::filesystem::exists(presetsPath)) {
            return presetsPath;
        }
    }
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return std::filesystem::path(userProfile) / "Documents" / "Orch" / "modules" / "orch_synth_editor" / "presets";
    }
    return "";
}

class OrchFaustEditorView : public Steinberg::CPluginView, public OrchEditorView {
public:
    OrchFaustEditorView(OrchFaustController* controller) 
        : CPluginView(nullptr), controller(controller)
    {
        // Wider native editor layout for preset and bridge controls.
        Steinberg::ViewRect r(0, 0, 500, 390);
        setRect(r);
    }

    ~OrchFaustEditorView() override {
        deleteFonts();
    }

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override {
        if (strcmp(type, Steinberg::kPlatformTypeHWND) == 0) {
            return Steinberg::kResultOk;
        }
        return Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override {
        if (strcmp(type, Steinberg::kPlatformTypeHWND) != 0) {
            return Steinberg::kResultFalse;
        }

        systemWindow = parent;
        HWND parentHwnd = (HWND)parent;

        // Set window property to point to this view instance for WNDPROC access
        SetPropA(parentHwnd, "OrchFaustEditorView", this);
        oldParentWndProc = (WNDPROC)SetWindowLongPtr(parentHwnd, GWLP_WNDPROC, (LONG_PTR)ParentSubclassProc);

        HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(parentHwnd, GWLP_HINSTANCE);

        fontTitle = CreateFontA(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        fontBody = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        fontLabel = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        fontSmall = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        brushBackground = CreateSolidBrush(RGB(22, 25, 31));
        brushHeader = CreateSolidBrush(RGB(32, 38, 48));
        brushPanel = CreateSolidBrush(RGB(39, 45, 56));

        HWND title = CreateWindowA(
            "STATIC", "Orch Synth",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            24, 18, 452, 28,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(title, fontTitle);

        HWND subtitle = CreateWindowA(
            "STATIC", "VST3 Controller",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            24, 48, 452, 18,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(subtitle, fontSmall);

        // Test C1 button
        btnTest = CreateWindowA(
            "BUTTON", "Play C1 Note",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            28, 86, 190, 36,
            parentHwnd, (HMENU)1001, hInstance, NULL
        );
        applyFont(btnTest, fontBody);

        // Preset Label
        HWND presetLabel = CreateWindowA(
            "STATIC", "Load Preset",
            WS_VISIBLE | WS_CHILD,
            28, 140, 140, 22,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(presetLabel, fontLabel);

        lblCurrentPatch = CreateWindowA(
            "STATIC", "Current Patch: Default Poly Sine",
            WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE,
            170, 138, 302, 24,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(lblCurrentPatch, fontSmall);

        // Preset ComboBox
        cbPresets = CreateWindowA(
            "COMBOBOX", "",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            28, 164, 306, 180,
            parentHwnd, (HMENU)1003, hInstance, NULL
        );
        applyFont(cbPresets, fontBody);

        // Reload Preset Button
        btnLoad = CreateWindowA(
            "BUTTON", "Reload Preset",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            352, 162, 120, 32,
            parentHwnd, (HMENU)1002, hInstance, NULL
        );
        applyFont(btnLoad, fontBody);

        // Dynamic VST Target Instance Selection Controls
        HWND dialTitle = CreateWindowA(
            "STATIC", "Graph Dials",
            WS_VISIBLE | WS_CHILD,
            28, 232, 140, 20,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(dialTitle, fontLabel);

        btnOpenEditor = CreateWindowA(
            "BUTTON", "Open Web Editor",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            28, 334, 190, 34,
            parentHwnd, (HMENU)1004, hInstance, NULL
        );
        applyFont(btnOpenEditor, fontBody);

        lblPort = CreateWindowA(
            "STATIC", "Editor Port: Discovered...",
            WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE,
            236, 337, 236, 28,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(lblPort, fontBody);

        lblPresetCount = CreateWindowA(
            "STATIC", "Scanning presets...",
            WS_VISIBLE | WS_CHILD,
            28, 196, 306, 18,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(lblPresetCount, fontSmall);

        HWND credit = CreateWindowA(
            "STATIC", "Created by Ruchir Shah 2026",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            236, 374, 236, 18,
            parentHwnd, NULL, hInstance, NULL
        );
        applyFont(credit, fontSmall);

        // Populate presets by scanning the user Documents folder
        presetNames.clear();
        presetPaths.clear();

        std::filesystem::path presetsDir = getPresetsDir();
        if (!presetsDir.empty() && std::filesystem::exists(presetsDir)) {
            try {
                for (const auto& entry : std::filesystem::directory_iterator(presetsDir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".json") {
                        std::string filename = entry.path().stem().string();
                        presetNames.push_back(filename);
                        presetPaths.push_back(entry.path());
                    }
                }
            } catch (...) {}
        }

        // Fallback to hardcoded presets if none found
        if (presetNames.empty()) {
            presetNames = {"Default Poly Sine", "Saw Filter Gain", "Pluck String Body"};
        } else {
            std::vector<size_t> order(presetNames.size());
            for (size_t i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
                return presetNames[a] < presetNames[b];
            });
            std::vector<std::string> sortedNames;
            std::vector<std::filesystem::path> sortedPaths;
            sortedNames.reserve(order.size());
            sortedPaths.reserve(order.size());
            for (size_t i : order) {
                sortedNames.push_back(presetNames[i]);
                sortedPaths.push_back(presetPaths[i]);
            }
            presetNames = std::move(sortedNames);
            presetPaths = std::move(sortedPaths);
        }

        for (const auto& name : presetNames) {
            SendMessageA(cbPresets, CB_ADDSTRING, 0, (LPARAM)name.c_str());
        }
        SendMessageA(cbPresets, CB_SETCURSEL, 0, 0);
        if (lblPresetCount) {
            std::string countText = std::to_string(presetNames.size()) + " presets loaded";
            SetWindowTextA(lblPresetCount, countText.c_str());
        }

        // Request active port from processor via component message channel
        if (controller) {
            controller->requestPortFromProcessor(this);
            controller->requestCurrentPatchName();
            controller->requestDialLayout();
        }

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API removed() override {
        HWND parentHwnd = (HWND)systemWindow;
        if (parentHwnd) {
            // Restore original WndProc
            SetWindowLongPtr(parentHwnd, GWLP_WNDPROC, (LONG_PTR)oldParentWndProc);
            RemovePropA(parentHwnd, "OrchFaustEditorView");
        }
        if (controller) {
            controller->clearActiveView();
        }
        deleteFonts();
        systemWindow = nullptr;
        return Steinberg::kResultOk;
    }

    void updatePortLabel(int port) {
        if (lblPort) {
            std::string text = "Editor Port: " + std::to_string(port);
            SetWindowTextA(lblPort, text.c_str());
        }
    }

    void updateCurrentPatchLabel(const std::string& name) {
        if (lblCurrentPatch) {
            std::string text = "Current Patch: " + (name.empty() ? std::string("Untitled Patch") : name);
            SetWindowTextA(lblCurrentPatch, text.c_str());
        }
    }

    void updateDialLayout(const std::vector<std::tuple<std::string, std::string, float>>& layout) {
        HWND parentHwnd = (HWND)systemWindow;
        if (!parentHwnd) {
            return;
        }

        dialControls.clear();

        int visibleIndex = 0;
        for (const auto& [key, label, value] : layout) {
            if (visibleIndex >= 8) {
                break;
            }
            const int col = visibleIndex % 4;
            const int row = visibleIndex / 4;
            const int x = 28 + col * 112;
            const int y = 256 + row * 44;

            DialControl dial;
            dial.key = key;
            dial.label = label.empty() ? key : label;
            dial.value = std::clamp(value, 0.0f, 1.0f);
            dial.bounds = {x, y, x + 82, y + 58};
            dialControls.push_back(dial);
            ++visibleIndex;
        }
        InvalidateRect(parentHwnd, NULL, TRUE);
    }

    void openWebEditor() {
        int port = controller ? controller->getActivePort() : 9020;
        std::string url = "http://localhost:5000/orch_synth_editor?port=" + std::to_string(port);
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    void triggerC1NoteOn() {
        sendOscNoteOn(36.0f, 1.0f);
    }

    void triggerC1NoteOff() {
        sendOscNoteOff(36.0f);
    }

    void loadSelectedPreset() {
        int index = (int)SendMessageA(cbPresets, CB_GETCURSEL, 0, 0);
        if (index < 0 || index >= (int)presetNames.size()) {
            return;
        }

        std::string jsonGraph;
        if (index < (int)presetPaths.size()) {
            // Read JSON file from disk
            std::ifstream f(presetPaths[index]);
            if (f.is_open()) {
                std::stringstream buffer;
                buffer << f.rdbuf();
                jsonGraph = buffer.str();
            }
        } else {
            // Fallback hardcoded logic
            std::string name = presetNames[index];
            if (name == "Default Poly Sine") {
                jsonGraph = PRESET_SINE;
            } else if (name == "Saw Filter Gain") {
                jsonGraph = PRESET_SAW;
            } else if (name == "Pluck String Body") {
                jsonGraph = PRESET_PLUCK;
            }
        }

        if (!jsonGraph.empty()) {
            jsonGraph = graphJsonWithPresetName(jsonGraph, presetNames[index]);
            if (controller) {
                controller->setCurrentPatchName(presetNames[index]);
            }
            sendOscLoadGraph(jsonGraph.c_str());
        }
    }

    bool handleDialMouseDown(HWND hWnd, int x, int y) {
        for (size_t i = 0; i < dialControls.size(); ++i) {
            if (PtInRect(&dialControls[i].bounds, POINT{x, y})) {
                activeDialIndex = static_cast<int>(i);
                setDialFromPoint(hWnd, x, y);
                SetCapture(hWnd);
                return true;
            }
        }
        return false;
    }

    bool handleDialMouseMove(HWND hWnd, int x, int y, WPARAM buttons) {
        if (activeDialIndex < 0 || (buttons & MK_LBUTTON) == 0) {
            return false;
        }
        setDialFromPoint(hWnd, x, y);
        return true;
    }

    bool handleDialMouseUp() {
        if (activeDialIndex < 0) {
            return false;
        }
        activeDialIndex = -1;
        ReleaseCapture();
        return true;
    }

private:
    OrchFaustController* controller;
    HWND btnTest = NULL;
    HWND cbPresets = NULL;
    HWND btnLoad = NULL;
    HWND btnOpenEditor = NULL;
    HWND lblPort = NULL;
    HWND lblCurrentPatch = NULL;
    HWND lblPresetCount = NULL;
    struct DialControl {
        std::string key;
        std::string label;
        float value = 0.0f;
        RECT bounds = {};
    };
    std::vector<DialControl> dialControls;
    int activeDialIndex = -1;
    WNDPROC oldParentWndProc = NULL;
    HFONT fontTitle = NULL;
    HFONT fontBody = NULL;
    HFONT fontLabel = NULL;
    HFONT fontSmall = NULL;
    HBRUSH brushBackground = NULL;
    HBRUSH brushHeader = NULL;
    HBRUSH brushPanel = NULL;
    std::vector<std::string> presetNames;
    std::vector<std::filesystem::path> presetPaths;

    void applyFont(HWND control, HFONT font) {
        if (control && font) {
            SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
        }
    }

    void deleteFonts() {
        if (fontTitle) {
            DeleteObject(fontTitle);
            fontTitle = NULL;
        }
        if (fontBody) {
            DeleteObject(fontBody);
            fontBody = NULL;
        }
        if (fontLabel) {
            DeleteObject(fontLabel);
            fontLabel = NULL;
        }
        if (fontSmall) {
            DeleteObject(fontSmall);
            fontSmall = NULL;
        }
        if (brushBackground) {
            DeleteObject(brushBackground);
            brushBackground = NULL;
        }
        if (brushHeader) {
            DeleteObject(brushHeader);
            brushHeader = NULL;
        }
        if (brushPanel) {
            DeleteObject(brushPanel);
            brushPanel = NULL;
        }
    }

    void paintBackground(HWND hWnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT client;
        GetClientRect(hWnd, &client);
        FillRect(hdc, &client, brushBackground ? brushBackground : (HBRUSH)(COLOR_WINDOW + 1));

        RECT header = {16, 12, 484, 74};
        FillRect(hdc, &header, brushHeader ? brushHeader : (HBRUSH)(COLOR_WINDOW + 1));

        RECT presetPanel = {18, 130, 482, 218};
        FillRect(hdc, &presetPanel, brushPanel ? brushPanel : (HBRUSH)(COLOR_WINDOW + 1));

        RECT dialPanel = {18, 224, 482, 326};
        FillRect(hdc, &dialPanel, brushPanel ? brushPanel : (HBRUSH)(COLOR_WINDOW + 1));

        RECT bridgePanel = {18, 328, 482, 372};
        FillRect(hdc, &bridgePanel, brushPanel ? brushPanel : (HBRUSH)(COLOR_WINDOW + 1));

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(67, 76, 92));
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, header.left, header.top, header.right, header.bottom, 8, 8);
        RoundRect(hdc, presetPanel.left, presetPanel.top, presetPanel.right, presetPanel.bottom, 8, 8);
        RoundRect(hdc, dialPanel.left, dialPanel.top, dialPanel.right, dialPanel.bottom, 8, 8);
        RoundRect(hdc, bridgePanel.left, bridgePanel.top, bridgePanel.right, bridgePanel.bottom, 8, 8);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        drawDialControls(hdc);

        EndPaint(hWnd, &ps);
    }

    void drawDialControls(HDC hdc) {
        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, fontSmall ? fontSmall : GetStockObject(DEFAULT_GUI_FONT));

        for (const auto& dial : dialControls) {
            RECT labelRect = {dial.bounds.left, dial.bounds.top, dial.bounds.right, dial.bounds.top + 14};
            SetTextColor(hdc, RGB(235, 241, 248));
            DrawTextA(hdc, dial.label.c_str(), -1, &labelRect, DT_CENTER | DT_END_ELLIPSIS | DT_SINGLELINE);

            const int centerX = dial.bounds.left + 41;
            const int centerY = dial.bounds.top + 35;
            const int radius = 17;
            RECT knobRect = {centerX - radius, centerY - radius, centerX + radius, centerY + radius};

            HBRUSH knobFill = CreateSolidBrush(RGB(30, 37, 48));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, knobFill);
            HPEN knobBorder = CreatePen(PS_SOLID, 1, RGB(86, 100, 124));
            HPEN oldPen = (HPEN)SelectObject(hdc, knobBorder);
            Ellipse(hdc, knobRect.left, knobRect.top, knobRect.right, knobRect.bottom);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(knobBorder);
            DeleteObject(knobFill);

            const float angle = (-135.0f + dial.value * 270.0f) * 3.14159265f / 180.0f;
            const int indicatorX = centerX + static_cast<int>(std::cos(angle) * 12.0f);
            const int indicatorY = centerY + static_cast<int>(std::sin(angle) * 12.0f);
            HPEN indicatorPen = CreatePen(PS_SOLID, 3, RGB(126, 231, 135));
            oldPen = (HPEN)SelectObject(hdc, indicatorPen);
            MoveToEx(hdc, centerX, centerY, NULL);
            LineTo(hdc, indicatorX, indicatorY);
            SelectObject(hdc, oldPen);
            DeleteObject(indicatorPen);

            RECT valueRect = {dial.bounds.left, dial.bounds.bottom - 12, dial.bounds.right, dial.bounds.bottom};
            const int percent = std::clamp(static_cast<int>(std::round(dial.value * 100.0f)), 0, 100);
            std::string valueText = std::to_string(percent) + "%";
            SetTextColor(hdc, RGB(170, 184, 204));
            DrawTextA(hdc, valueText.c_str(), -1, &valueRect, DT_CENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, oldFont);
    }

    void setDialFromPoint(HWND hWnd, int, int y) {
        if (activeDialIndex < 0 || activeDialIndex >= static_cast<int>(dialControls.size())) {
            return;
        }

        auto& dial = dialControls[activeDialIndex];
        const int top = dial.bounds.top + 16;
        const int bottom = dial.bounds.bottom - 8;
        const int dragRange = bottom > top ? bottom - top : 1;
        const float next = std::clamp(1.0f - (static_cast<float>(y - top) / static_cast<float>(dragRange)), 0.0f, 1.0f);
        if (std::abs(next - dial.value) < 0.001f) {
            return;
        }

        dial.value = next;
        if (controller) {
            controller->setVstDial(dial.key, dial.value);
        }
        InvalidateRect(hWnd, &dial.bounds, TRUE);
    }

    HBRUSH handleCtlColorStatic(HDC hdc, HWND control) {
        SetBkMode(hdc, TRANSPARENT);
        if (control == lblPort) {
            SetTextColor(hdc, RGB(126, 231, 135));
            return brushPanel ? brushPanel : (HBRUSH)GetStockObject(NULL_BRUSH);
        }
        if (control == lblPresetCount) {
            SetTextColor(hdc, RGB(170, 184, 204));
            return brushPanel ? brushPanel : (HBRUSH)GetStockObject(NULL_BRUSH);
        }
        if (control == lblCurrentPatch) {
            SetTextColor(hdc, RGB(126, 231, 135));
            return brushPanel ? brushPanel : (HBRUSH)GetStockObject(NULL_BRUSH);
        }
        SetTextColor(hdc, RGB(235, 241, 248));
        return brushBackground ? brushBackground : (HBRUSH)GetStockObject(NULL_BRUSH);
    }

    HBRUSH handleCtlColorControl(HDC hdc) {
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB(31, 37, 48));
        SetTextColor(hdc, RGB(235, 241, 248));
        return brushPanel ? brushPanel : (HBRUSH)GetStockObject(NULL_BRUSH);
    }

    bool handleDrawItem(DRAWITEMSTRUCT* item) {
        if (!item || (item->CtlID != 1001 && item->CtlID != 1002 && item->CtlID != 1004)) {
            return false;
        }

        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        const bool focused = (item->itemState & ODS_FOCUS) != 0;
        HBRUSH fill = CreateSolidBrush(pressed ? RGB(50, 60, 76) : RGB(38, 46, 59));
        FillRect(item->hDC, &item->rcItem, fill);
        DeleteObject(fill);

        HPEN border = CreatePen(PS_SOLID, 1, focused ? RGB(126, 231, 135) : RGB(72, 84, 104));
        HGDIOBJ oldPen = SelectObject(item->hDC, border);
        HGDIOBJ oldBrush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, 8, 8);
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(border);

        char text[128] = {};
        GetWindowTextA(item->hwndItem, text, sizeof(text));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(245, 248, 252));
        HGDIOBJ oldFont = SelectObject(item->hDC, fontBody ? fontBody : GetStockObject(DEFAULT_GUI_FONT));
        RECT textRect = item->rcItem;
        if (pressed) {
            OffsetRect(&textRect, 1, 1);
        }
        DrawTextA(item->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(item->hDC, oldFont);
        return true;
    }

    void sendOscLoadGraph(const char* jsonStr) {
        try {
            int port = controller ? controller->getActivePort() : 9020;
            char buffer[1024 * 16];
            osc::OutboundPacketStream p(buffer, sizeof(buffer));
            p << osc::BeginMessage("/orch_faust/load_graph")
              << jsonStr
              << osc::EndMessage;

            UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", port));
            transmitSocket.Send(p.Data(), p.Size());

            // Also send trigger compile
            p.Clear();
            p << osc::BeginMessage("/orch_faust/compile")
              << osc::EndMessage;
            transmitSocket.Send(p.Data(), p.Size());
        } catch (...) {}
    }

    void sendOscNoteOn(float pitch, float velocity) {
        try {
            int port = controller ? controller->getActivePort() : 9020;
            char buffer[1024];
            osc::OutboundPacketStream p(buffer, sizeof(buffer));
            p << osc::BeginMessage("/orch_faust/note_on")
              << pitch << velocity
              << osc::EndMessage;

            UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", port));
            transmitSocket.Send(p.Data(), p.Size());
        } catch (...) {}
    }

    void sendOscNoteOff(float pitch) {
        try {
            int port = controller ? controller->getActivePort() : 9020;
            char buffer[1024];
            osc::OutboundPacketStream p(buffer, sizeof(buffer));
            p << osc::BeginMessage("/orch_faust/note_off")
              << pitch
              << osc::EndMessage;

            UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", port));
            transmitSocket.Send(p.Data(), p.Size());
        } catch (...) {}
    }

    static LRESULT CALLBACK ParentSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        auto* view = (OrchFaustEditorView*)GetPropA(hWnd, "OrchFaustEditorView");
        if (view) {
            if (uMsg == WM_PAINT) {
                view->paintBackground(hWnd);
                return 0;
            } else if (uMsg == WM_ERASEBKGND) {
                return 1;
            } else if (uMsg == WM_CTLCOLORSTATIC) {
                return (LRESULT)view->handleCtlColorStatic((HDC)wParam, (HWND)lParam);
            } else if (uMsg == WM_CTLCOLORBTN || uMsg == WM_CTLCOLOREDIT || uMsg == WM_CTLCOLORLISTBOX) {
                return (LRESULT)view->handleCtlColorControl((HDC)wParam);
            } else if (uMsg == WM_DRAWITEM) {
                if (view->handleDrawItem((DRAWITEMSTRUCT*)lParam)) {
                    return TRUE;
                }
            } else if (uMsg == WM_COMMAND) {
                int wmId = LOWORD(wParam);
                int notifyCode = HIWORD(wParam);
                if (wmId == 1001) {
                    view->triggerC1NoteOn();
                    SetTimer(hWnd, 2001, 500, NULL);
                    return 0;
                } else if (wmId == 1002 && notifyCode == BN_CLICKED) {
                    view->loadSelectedPreset();
                    return 0;
                } else if (wmId == 1003 && notifyCode == CBN_SELCHANGE) {
                    view->loadSelectedPreset();
                    return 0;
                } else if (wmId == 1004) {
                    view->openWebEditor();
                    return 0;
                }
            } else if (uMsg == WM_LBUTTONDOWN) {
                if (view->handleDialMouseDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
                    return 0;
                }
            } else if (uMsg == WM_MOUSEMOVE) {
                if (view->handleDialMouseMove(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam)) {
                    return 0;
                }
            } else if (uMsg == WM_LBUTTONUP) {
                if (view->handleDialMouseUp()) {
                    return 0;
                }
            } else if (uMsg == WM_HSCROLL) {
                return 0;
            } else if (uMsg == WM_TIMER) {
                if (wParam == 2001) {
                    KillTimer(hWnd, 2001);
                    view->triggerC1NoteOff();
                    return 0;
                }
            } else if (uMsg == WM_DESTROY) {
                WNDPROC oldProc = view->oldParentWndProc;
                SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)oldProc);
                RemovePropA(hWnd, "OrchFaustEditorView");
                return CallWindowProc(oldProc, hWnd, uMsg, wParam, lParam);
            }
            return CallWindowProc(view->oldParentWndProc, hWnd, uMsg, wParam, lParam);
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
};
#endif

OrchFaustController::OrchFaustController() {}

Steinberg::FUnknown* OrchFaustController::createInstance(void* context) {
    return (Steinberg::Vst::IEditController*)new OrchFaustController();
}

Steinberg::tresult PLUGIN_API OrchFaustController::initialize(Steinberg::FUnknown* context) {
    Steinberg::tresult result = Steinberg::Vst::EditController::initialize(context);
    if (result != Steinberg::kResultOk) return result;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API OrchFaustController::terminate() {
    return Steinberg::Vst::EditController::terminate();
}

Steinberg::IPlugView* PLUGIN_API OrchFaustController::createView(Steinberg::FIDString name) {
    if (strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
        return new OrchVstGuiEditor(this);
    }
    return nullptr;
}

void OrchFaustController::requestPortFromProcessor(OrchEditorView* view) {
    activeView = view;
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("GetOscServerPort");
        sendMessage(msg);
    }
}

void OrchFaustController::requestCurrentPatchName() {
    if (activeView) {
        activeView->updateCurrentPatchLabel(currentPatchName);
    }
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("GetCurrentPatchName");
        sendMessage(msg);
    }
}

void OrchFaustController::requestDialLayout() {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("GetVstDialLayout");
        sendMessage(msg);
    }
}

Steinberg::int32 PLUGIN_API OrchFaustController::getNoteExpressionCount(
    Steinberg::int32 busIndex, Steinberg::int16 channel) {
    return busIndex == 0 && channel >= 0 && channel < 16 ? 3 : 0;
}

Steinberg::tresult PLUGIN_API OrchFaustController::getNoteExpressionInfo(
    Steinberg::int32 busIndex, Steinberg::int16 channel, Steinberg::int32 noteExpressionIndex,
    Steinberg::Vst::NoteExpressionTypeInfo& info) {
    if (busIndex != 0 || channel < 0 || channel >= 16 || noteExpressionIndex < 0 || noteExpressionIndex >= 3)
        return Steinberg::kResultFalse;

    using namespace Steinberg;
    using namespace Steinberg::Vst;
    info = {};
    info.unitId = -1;
    info.associatedParameterId = kNoParamId;
    info.valueDesc.minimum = 0.0;
    info.valueDesc.maximum = 1.0;
    info.valueDesc.stepCount = 0;

    if (noteExpressionIndex == 0) {
        info.typeId = kTuningTypeID;
        UString128(STR16("Tuning")).copyTo(info.title, 128);
        UString128(STR16("Tune")).copyTo(info.shortTitle, 128);
        UString128(STR16("semitones")).copyTo(info.units, 128);
        info.valueDesc.defaultValue = 0.5;
        info.flags = NoteExpressionTypeInfo::kIsBipolar;
    } else if (noteExpressionIndex == 1) {
        info.typeId = kBrightnessTypeID;
        UString128(STR16("Timbre")).copyTo(info.title, 128);
        UString128(STR16("Timbre")).copyTo(info.shortTitle, 128);
        info.valueDesc.defaultValue = 0.0;
        info.flags = 0;
    } else {
        info.typeId = kExpressionTypeID;
        UString128(STR16("Expression")).copyTo(info.title, 128);
        UString128(STR16("Expr")).copyTo(info.shortTitle, 128);
        info.valueDesc.defaultValue = 0.0;
        info.flags = 0;
    }
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API OrchFaustController::getNoteExpressionStringByValue(
    Steinberg::int32, Steinberg::int16, Steinberg::Vst::NoteExpressionTypeID,
    Steinberg::Vst::NoteExpressionValue, Steinberg::Vst::String128) {
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API OrchFaustController::getNoteExpressionValueByString(
    Steinberg::int32, Steinberg::int16, Steinberg::Vst::NoteExpressionTypeID,
    const Steinberg::Vst::TChar*, Steinberg::Vst::NoteExpressionValue&) {
    return Steinberg::kResultFalse;
}

void OrchFaustController::requestGraphState() {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("GetGraphState");
        sendMessage(msg);
    }
}

void OrchFaustController::requestWaveform() {
    if (activeView) {
        if (auto* msg = allocateMessage()) {
            msg->setMessageID("GetWaveform");
            sendMessage(msg);
        }
    }
}

void OrchFaustController::setCurrentPatchName(std::string name) {
    currentPatchName = std::move(name);
    if (activeView) {
        activeView->updateCurrentPatchLabel(currentPatchName);
    }
}

void OrchFaustController::setVstDial(const std::string& key, float value) {
    if (auto* msg = allocateMessage()) {
        msg->setMessageID("SetVstDial");
        auto keyText = std::vector<Steinberg::Vst::TChar>();
        keyText.reserve(key.size() + 1);
        for (unsigned char ch : key) {
            keyText.push_back(static_cast<Steinberg::Vst::TChar>(ch));
        }
        keyText.push_back(0);
        msg->getAttributes()->setString("key", keyText.data());
        msg->getAttributes()->setFloat("value", value);
        sendMessage(msg);
    }
}

Steinberg::tresult PLUGIN_API OrchFaustController::notify(Steinberg::Vst::IMessage* message) {
    if (!message) return Steinberg::kResultFalse;
    
    if (strcmp(message->getMessageID(), "OscServerPort") == 0) {
        Steinberg::int64 portVal = 9020;
        if (message->getAttributes()->getInt("port", portVal) == Steinberg::kResultOk) {
            this->activePort = (int)portVal;
            if (activeView) {
                activeView->updatePortLabel(this->activePort);
            }
        }
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "CurrentPatchName") == 0) {
        Steinberg::Vst::TChar patchName[256] = {};
        if (message->getAttributes()->getString("name", patchName, sizeof(patchName)) == Steinberg::kResultOk) {
            currentPatchName = fromTCharString(patchName);
            if (activeView) {
                activeView->updateCurrentPatchLabel(currentPatchName);
            }
        }
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "VstDialLayout") == 0) {
        Steinberg::Vst::TChar layoutText[4096] = {};
        if (message->getAttributes()->getString("layout", layoutText, sizeof(layoutText)) == Steinberg::kResultOk) {
            std::vector<std::tuple<std::string, std::string, float>> layout;
            std::stringstream lines(fromTCharString(layoutText));
            std::string line;
            while (std::getline(lines, line)) {
                std::stringstream fields(line);
                std::string key;
                std::string label;
                std::string valueText;
                if (std::getline(fields, key, '\t') &&
                    std::getline(fields, label, '\t') &&
                    std::getline(fields, valueText, '\t') &&
                    !key.empty()) {
                    layout.emplace_back(key, label.empty() ? key : label, std::clamp(std::stof(valueText), 0.0f, 1.0f));
                }
            }
            if (activeView) {
                activeView->updateDialLayout(layout);
            }
        }
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "GraphState") == 0) {
        Steinberg::Vst::TChar graph[16384] = {};
        if (message->getAttributes()->getString("graph", graph, sizeof(graph)) == Steinberg::kResultOk && activeView) {
            activeView->updateGraphState(fromTCharString(graph));
        }
        return Steinberg::kResultOk;
    }


    if (strcmp(message->getMessageID(), "Waveform") == 0) {
        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        if (message->getAttributes()->getBinary("samples", data, size) == Steinberg::kResultOk &&
            data && size >= sizeof(float) && size % sizeof(float) == 0 && activeView) {
            const auto* samples = static_cast<const float*>(data);
            activeView->updateWaveform(std::vector<float>(samples, samples + size / sizeof(float)));
        }
        return Steinberg::kResultOk;
    }
    
    return Steinberg::Vst::EditController::notify(message);
}

} // namespace OrchFaust
