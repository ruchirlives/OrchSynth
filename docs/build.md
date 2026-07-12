# OrchSynth Build Guide

This project requires a C++17 compliant compiler and CMake 3.15+.

## Dependencies
Most dependencies are configured by CMake:

1. **Steinberg VST3 SDK** - fetched by CMake from Steinberg's public GitHub repository.
2. **oscpack** - fetched by CMake for UDP OSC message processing.
3. **nlohmann/json** - fetched by CMake for JSON parsing.
4. **libfaust** - required locally for the LLVM JIT compiler backend.

The repository does not vendor prebuilt Faust binaries or the VST3 SDK. The
`third_party/` directory is intentionally ignored by git.

## Preparing libfaust

Create this local directory layout before configuring CMake:

```text
third_party/
  libfaust/
    include/
    lib/
      faust.dll        # Windows runtime
      faust.lib        # Windows import/static library
    share/
      faust/
        *.lib          # Faust standard library files
```

On Windows, install or extract a Faust release that includes the LLVM-enabled
libfaust SDK, then copy its `include`, `lib`, and `share/faust` contents into
`third_party/libfaust`.

On macOS, the GitHub Actions workflow downloads Faust release DMGs and stages
the same `third_party/libfaust` layout during CI.

## Building on Windows

From a Developer PowerShell or a shell with Visual Studio build tools available:

```powershell
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
```

This produces the VST3 bundle under:

```text
build/VST3/Release/OrchSynth.vst3
```

The plug-in expects Faust runtime files and standard-library `.lib` files to be
copied into the VST3 bundle during the CMake build.
