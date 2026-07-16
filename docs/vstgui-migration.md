# VSTGUI Migration Plan

## Goal

Replace the current native Win32-only editor implementation with a VSTGUI editor
that works in Windows and macOS DAWs while preserving the graph-driven OrchSynth
workflow.

## Constraints

- The VST must remain self-contained when loaded outside Orch.
- Orch integration, OSC, and the web editor remain optional enhancements.
- Graph JSON remains the portable preset/state format.
- Graph-declared controls, such as `vst_dial` nodes, must drive live DSP controls
  without requiring a recompile.

## Architecture

Use VSTGUI from the Steinberg VST3 SDK already fetched by CMake. The plugin now
links `vstgui_support`, which provides VSTGUI integration targets.

Prefer a custom C++ VSTGUI view hierarchy over a static `.uidesc` editor because
the control layout is dynamic:

- Header: plugin title and current patch name.
- Presets: local preset selector and reload/load actions.
- Graph controls: generated from processor-provided graph control layout.
- Footer/status: optional Orch editor launch and OSC/editor port status.

The controller should become a bridge between VSTGUI widgets and processor
messages:

- `GetCurrentPatchName` / `CurrentPatchName`
- `GetVstDialLayout` / `VstDialLayout`
- `SetVstDial`
- existing OSC port discovery for optional Orch editor integration

## Migration Steps

1. Add a VSTGUI-backed editor class alongside the current Win32 editor.
2. Move patch label, preset list, and graph dial rendering into VSTGUI controls.
3. Keep the current Win32 editor available behind a compile-time fallback until
   the VSTGUI editor reaches feature parity.
4. Replace `OrchFaustController::createView()` with the VSTGUI editor.
5. Remove Win32-only editor code and keep platform-specific code only for optional
   operations such as opening the external web editor URL.

## First Milestone

Build a VSTGUI editor shell with:

- fixed frame size matching the current editor,
- custom dark background,
- patch label,
- placeholder graph dial area.

Once this shell opens in the validator and a DAW, move controls one by one.
