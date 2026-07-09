# Orch Faust Synth Build Guide

This project requires a C++17 compliant compiler and CMake 3.15+.

## Dependencies
All dependencies are configured via CMake and resolved dynamically or locally:
1. **VST3 SDK** (Fetched via Steinberg's Github or submodules)
2. **oscpack** (UDP OSC message processing)
3. **libfaust** (LLVM JIT Compiler backend)
4. **nlohmann/json** (Header-only library for JSON parsing)

## Building on Windows
To build:
```powershell
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
```
This produces `OrchFaustSynth.vst3` which can be loaded into your host (Orch).
