#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include "graph/GraphParser.h"
#include "graph/FaustGraphCompiler.h"
#include "dsp/VoiceManager.h"
#include "util/Logging.h"
#include "faust/dsp/llvm-dsp.h"

// Helper to get directory of current module
extern "C" IMAGE_DOS_HEADER __ImageBase;
static std::string getDllDir() {
    char path[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
    std::string s(path);
    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return s.substr(0, lastSlash);
    }
    return "";
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  OrchFaustSynth Integration Test Harness" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 1. Define Correct Graph JSON Representation
    std::string jsonGraph = R"({
        "version": 1,
        "name": "TestPatch",
        "nodes": [
            {
                "id": "osc1",
                "type": "sine",
                "params": {
                    "freq": 440.0
                }
            },
            {
                "id": "gain1",
                "type": "gain",
                "params": {
                    "gain": 0.5
                }
            },
            {
                "id": "out1",
                "type": "output",
                "params": {}
            }
        ],
        "connections": [
            {
                "source": "osc1",
                "target": "gain1"
            },
            {
                "source": "gain1",
                "target": "out1"
            }
        ],
        "output": "out1"
    })";

    // 2. Parse JSON
    std::string err;
    std::cout << "[Step 1] Parsing Graph JSON..." << std::endl;
    auto optGraph = OrchFaust::GraphParser::parse(jsonGraph, err);
    if (!optGraph) {
        std::cout << "❌ FAILED: JSON Graph parsing failed: " << err << std::endl;
        return 1;
    }
    std::cout << "   SUCCESS: Graph parsed with " << optGraph->nodes.size() << " nodes." << std::endl;

    // 3. Compile Graph to Faust code
    std::cout << "[Step 2] Compiling Graph to Faust DSP code..." << std::endl;
    std::string dspCode = OrchFaust::FaustGraphCompiler::compile(*optGraph, err);
    if (dspCode.empty()) {
        std::cout << "❌ FAILED: Faust Graph compilation failed: " << err << std::endl;
        return 1;
    }
    std::cout << "   SUCCESS: Generated Faust code:\n" << dspCode << std::endl;

    // 4. Initialize Faust Compiler arguments
    std::string dllDir = getDllDir();
    std::string importPath = dllDir + "\\faust";
    std::cout << "   Using import path: " << importPath << std::endl;

    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(importPath.c_str());

    // 5. Compile JIT binary using libfaust LLVM directly
    std::cout << "[Step 3] Compiling JIT binary using libfaust LLVM..." << std::endl;
    llvm_dsp_factory* factory = createDSPFactoryFromString(
        "OrchFaustTestDSP",
        dspCode,
        static_cast<int>(argv.size()),
        argv.data(),
        "",
        err,
        -1
    );

    if (!factory) {
        std::cout << "❌ FAILED: libfaust compile failed: " << err << std::endl;
        return 1;
    }
    std::cout << "   SUCCESS: JIT compiled successfully." << std::endl;

    // 6. Instantiate DSP instance
    std::cout << "[Step 4] Instantiating JIT DSP instance..." << std::endl;
    llvm_dsp* dsp = factory->createDSPInstance();
    if (!dsp) {
        std::cout << "❌ FAILED: Failed to create DSP instance." << std::endl;
        deleteDSPFactory(factory);
        return 1;
    }
    std::cout << "   SUCCESS: DSP instance created." << std::endl;

    // 7. Initialize DSP
    std::cout << "[Step 5] Initializing DSP at 48kHz..." << std::endl;
    dsp->init(48000);
    std::cout << "   SUCCESS: DSP initialized." << std::endl;

    // 8. Process one block of audio
    const int numFrames = 256;
    float* outputs[2];
    std::vector<float> outBufferL(numFrames, 0.0f);
    std::vector<float> outBufferR(numFrames, 0.0f);
    outputs[0] = outBufferL.data();
    outputs[1] = outBufferR.data();

    std::cout << "[Step 6] Running DSP process for " << numFrames << " frames..." << std::endl;
    dsp->compute(numFrames, nullptr, outputs);
    std::cout << "   SUCCESS: DSP processed audio successfully." << std::endl;

    // 9. Inspect output to verify it generated audio
    float maxVal = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        maxVal = (std::max)(maxVal, (std::abs)(outBufferL[i]));
    }
    std::cout << "Peak output amplitude: " << maxVal << std::endl;

    if (maxVal > 0.0f) {
        std::cout << "🎉 ALL TESTS PASSED: Non-silent audio generated successfully!" << std::endl;
    } else {
        std::cout << "❌ FAILED: Silence generated." << std::endl;
    }

    // Clean up
    delete dsp;
    deleteDSPFactory(factory);
    return 0;
}
