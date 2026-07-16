# Orch GUI Positioner

Run from the repository root:

```powershell
node tools/gui-positioner/server.js
```

Open `http://127.0.0.1:4173`.

The tool overlays editable control bounds on `orch_full_skin_dynamic.png`. Drag a
bound, resize it with its lower-right handle, or enter exact absolute canvas
coordinates in the inspector. `Save layout` writes `resources/gui/orch_gui_layout.json`.

`OrchVstGuiEditor` reads that JSON whenever the VST editor is opened. Rebuild
and deploy only when the updated layout needs to be copied into an installed
VST3 bundle; otherwise close and reopen the editor to test a changed layout in
the current bundle.
