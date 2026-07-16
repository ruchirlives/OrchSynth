# AGENTS.md

## Repository Notes

- This repo builds the OrchSynth VST3 plug-in.
- Primary local workspace: `E:\CODINGPROJECTS\C++\OrchSynth`
- Windows shell: PowerShell.
- Build output: `build\VST3\Release\OrchSynth.vst3`
- Installed Windows VST3 location:
  `C:\Program Files\Common Files\VST3\OrchSynth.vst3`

## Deploying the VST3

Before copying a new VST3 build into `C:\Program Files\Common Files\VST3`,
check whether Orch currently has the plug-in loaded. If it does, the installed
binary can be locked and `Copy-Item` will fail.

Use the Orch MCP tools to manage the VST bridge lifecycle:

1. Inspect the current VST/audio bridge routing.
2. Unload, disconnect, or remove the active OrchSynth VST3 instance from the
   Orch bridge graph before copying files.
3. Copy the rebuilt VST3 bundle from:
   `build\VST3\Release\OrchSynth.vst3`
   to:
   `C:\Program Files\Common Files\VST3\OrchSynth.vst3`
4. Reload or recreate the OrchSynth VST3 instance in the Orch bridge graph.
5. Reconnect the VST3 to the same audio/MIDI routing it had before unload.
6. Save the updated Orch bridge routing if the MCP workflow requires an
   explicit save.

Do not assume killing processes is the right deploy path. Prefer the Orch MCP
bridge controls so the VST host releases the DLL cleanly and comes back with
the intended routing.

If copy still fails after unloading via MCP, inspect for another DAW or validator
process that may have loaded:

```powershell
C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win\OrchSynth.vst3
```

## Build Verification

After code changes, build the plug-in target:

```powershell
cmake --build build --config Release --target OrchSynth --parallel
```

The Steinberg validator should report `0 tests failed` for OrchSynth.
