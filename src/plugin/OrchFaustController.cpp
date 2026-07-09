#include "OrchFaustController.h"
#include "public.sdk/source/common/pluginview.h"
#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"
#include <windows.h>

namespace OrchFaust {

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
  "output": "gain1"
})";

const char* PRESET_SAW = R"({
  "version": 1,
  "name": "Saw Filter Gain",
  "nodes": [
    { "id": "osc1", "type": "saw", "params": { "freq": 440.0, "keyboard_tracking": 1.0 } },
    { "id": "filter1", "type": "lowpass", "params": { "cutoff": 2000.0 } },
    { "id": "gain1", "type": "gain", "params": { "gain": 0.25 } },
    { "id": "out1", "type": "output" }
  ],
  "connections": [
    { "source": "osc1", "target": "filter1" },
    { "source": "filter1", "target": "gain1" },
    { "source": "gain1", "target": "out1" }
  ],
  "output": "gain1"
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
  "output": "gain1"
})";

class OrchFaustEditorView : public Steinberg::CPluginView {
public:
    OrchFaustEditorView(OrchFaustController* controller) 
        : CPluginView(nullptr), controller(controller)
    {
        // Default size of the editor
        Steinberg::ViewRect r(0, 0, 400, 200);
        setRect(r);
    }

    ~OrchFaustEditorView() override = default;

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

        // UI Creation
        // Title label
        CreateWindowA(
            "STATIC", "Orch Faust Synth VST3 Controller",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 10, 380, 20,
            parentHwnd, NULL, hInstance, NULL
        );

        // Test C1 button
        btnTest = CreateWindowA(
            "BUTTON", "Test C1 Note (Hold)",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            20, 40, 180, 40,
            parentHwnd, (HMENU)1001, hInstance, NULL
        );

        // Preset Label
        CreateWindowA(
            "STATIC", "Select Preset:",
            WS_VISIBLE | WS_CHILD,
            20, 100, 120, 20,
            parentHwnd, NULL, hInstance, NULL
        );

        // Preset ComboBox
        cbPresets = CreateWindowA(
            "COMBOBOX", "",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 120, 220, 150,
            parentHwnd, (HMENU)1003, hInstance, NULL
        );

        // Load Preset Button
        btnLoad = CreateWindowA(
            "BUTTON", "Load Preset",
            WS_VISIBLE | WS_CHILD,
            260, 118, 120, 26,
            parentHwnd, (HMENU)1002, hInstance, NULL
        );

        // Populate presets
        SendMessageA(cbPresets, CB_ADDSTRING, 0, (LPARAM)"Default Poly Sine");
        SendMessageA(cbPresets, CB_ADDSTRING, 0, (LPARAM)"Saw Filter Gain");
        SendMessageA(cbPresets, CB_ADDSTRING, 0, (LPARAM)"Pluck String Body");
        SendMessageA(cbPresets, CB_SETCURSEL, 0, 0);

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API removed() override {
        HWND parentHwnd = (HWND)systemWindow;
        if (parentHwnd) {
            // Restore original WndProc
            SetWindowLongPtr(parentHwnd, GWLP_WNDPROC, (LONG_PTR)oldParentWndProc);
            RemovePropA(parentHwnd, "OrchFaustEditorView");
        }
        systemWindow = nullptr;
        return Steinberg::kResultOk;
    }

    void triggerC1NoteOn() {
        sendOscNoteOn(36.0f, 1.0f);
    }

    void triggerC1NoteOff() {
        sendOscNoteOff(36.0f);
    }

    void loadSelectedPreset() {
        int index = (int)SendMessageA(cbPresets, CB_GETCURSEL, 0, 0);
        const char* jsonGraph = nullptr;
        if (index == 0) {
            jsonGraph = PRESET_SINE;
        } else if (index == 1) {
            jsonGraph = PRESET_SAW;
        } else if (index == 2) {
            jsonGraph = PRESET_PLUCK;
        }

        if (jsonGraph) {
            sendOscLoadGraph(jsonGraph);
        }
    }

private:
    OrchFaustController* controller;
    HWND btnTest = NULL;
    HWND cbPresets = NULL;
    HWND btnLoad = NULL;
    WNDPROC oldParentWndProc = NULL;

    void sendOscLoadGraph(const char* jsonStr) {
        try {
            char buffer[1024 * 16];
            osc::OutboundPacketStream p(buffer, sizeof(buffer));
            p << osc::BeginMessage("/orch_faust/load_graph")
              << jsonStr
              << osc::EndMessage;

            UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", 9020));
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
            char buffer[1024];
            osc::OutboundPacketStream p(buffer, sizeof(buffer));
            p << osc::BeginMessage("/orch_faust/note_on")
              << pitch << velocity
              << osc::EndMessage;

            UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", 9020));
            transmitSocket.Send(p.Data(), p.Size());
        } catch (...) {}
    }

    void sendOscNoteOff(float pitch) {
        try {
            char buffer[1024];
            osc::OutboundPacketStream p(buffer, sizeof(buffer));
            p << osc::BeginMessage("/orch_faust/note_off")
              << pitch
              << osc::EndMessage;

            UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", 9020));
            transmitSocket.Send(p.Data(), p.Size());
        } catch (...) {}
    }

    static LRESULT CALLBACK ParentSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        auto* view = (OrchFaustEditorView*)GetPropA(hWnd, "OrchFaustEditorView");
        if (view) {
            if (uMsg == WM_COMMAND) {
                int wmId = LOWORD(wParam);
                if (wmId == 1001) {
                    view->triggerC1NoteOn();
                    SetTimer(hWnd, 2001, 500, NULL);
                    return 0;
                } else if (wmId == 1002) {
                    view->loadSelectedPreset();
                    return 0;
                }
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
        return new OrchFaustEditorView(this);
    }
    return nullptr;
}

} // namespace OrchFaust
