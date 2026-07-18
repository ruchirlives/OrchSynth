#include <iostream>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;
#else
#include <dlfcn.h>
#include <limits.h>
#endif
#include "graph/GraphParser.h"
#include "graph/FaustGraphCompiler.h"
#include "dsp/VoiceManager.h"
#include "dsp/ConvolutionProcessor.h"
#include "util/Logging.h"
#include "faust/dsp/llvm-dsp.h"
#include "faust/gui/MapUI.h"

// Helper to get directory of current module
static std::string getDllDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
    std::string s(path);
    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return s.substr(0, lastSlash);
    }
    return "";
#else
    Dl_info info;
    if (dladdr((void*)&getDllDir, &info) && info.dli_fname) {
        std::string s(info.dli_fname);
        size_t lastSlash = s.find_last_of("/");
        if (lastSlash != std::string::npos) {
            return s.substr(0, lastSlash);
        }
    }
    return "";
#endif
}

static void writeU16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xFF));
    output.put(static_cast<char>((value >> 8) & 0xFF));
}

static void writeU32(std::ofstream& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) output.put(static_cast<char>((value >> shift) & 0xFF));
}

static bool writeIdentityWav(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output.write("RIFF", 4);
    writeU32(output, 38);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16);
    writeU16(output, 1);
    writeU16(output, 1);
    writeU32(output, 48000);
    writeU32(output, 96000);
    writeU16(output, 2);
    writeU16(output, 16);
    output.write("data", 4);
    writeU32(output, 2);
    writeU16(output, 32767);
    return static_cast<bool>(output);
}

int main() {
    std::cout << "==================================================" << std::endl;

    const auto irPath = std::filesystem::temp_directory_path() / "orchsynth_identity_ir.wav";
    if (!writeIdentityWav(irPath)) {
        std::cout << "FAILED: Could not create convolution test WAV." << std::endl;
        return 1;
    }
    OrchFaust::ConvolutionProcessor convolution;
    convolution.prepare(48000.0, 256);
    convolution.setMix(1.0f, 1.0f);
    std::string convolutionError;
    if (!convolution.loadImpulseResponse(irPath.string(), false, convolutionError)) {
        std::cout << "FAILED: Convolution IR load failed: " << convolutionError << std::endl;
        std::filesystem::remove(irPath);
        return 1;
    }
    std::vector<float> convolutionLeft(256, 0.0f);
    std::vector<float> convolutionRight(256, 0.0f);
    convolutionLeft[0] = 0.5f;
    convolutionRight[0] = -0.25f;
    convolution.process(convolutionLeft.data(), convolutionRight.data(), 256);
    std::filesystem::remove(irPath);
    if (std::abs(convolutionLeft[0] - 0.5f) > 0.001f || std::abs(convolutionRight[0] + 0.25f) > 0.001f) {
        std::cout << "FAILED: Identity convolution did not preserve stereo samples." << std::endl;
        return 1;
    }
    std::cout << "SUCCESS: Native WAV convolution passed identity test." << std::endl;

    const std::string convolutionGraph = R"({
        "nodes": [
            {"id": "ir", "type": "convolution", "params": {"wet": 0.4}, "stringParams": {"ir_path": "C:/IRs/room.wav"}},
            {"id": "out", "type": "output", "params": {}}
        ],
        "connections": [],
        "output": "out"
    })";
    auto parsedConvolutionGraph = OrchFaust::GraphParser::parse(convolutionGraph, convolutionError);
    if (!parsedConvolutionGraph || parsedConvolutionGraph->nodes[0].stringParams["ir_path"] != "C:/IRs/room.wav") {
        std::cout << "FAILED: Graph parser did not preserve convolution IR path." << std::endl;
        return 1;
    }
    std::cout << "SUCCESS: Graph parser preserves convolution IR paths." << std::endl;

    OrchFaust::Graph bodyGraph;
    bodyGraph.name = "Body Convolution Test";
    bodyGraph.outputNodeId = "out";
    OrchFaust::Node noiseNode;
    noiseNode.id = "noise";
    noiseNode.type = "noise";
    OrchFaust::Node bodyNode;
    bodyNode.id = "body";
    bodyNode.type = "body_convolution";
    bodyNode.params = {{"wet", 1.0f}, {"gain", 1.0f}, {"normalize", 1.0f}};
    OrchFaust::Node bodyOutput;
    bodyOutput.id = "out";
    bodyOutput.type = "output";
    bodyGraph.nodes = {noiseNode, bodyNode, bodyOutput};
    bodyGraph.connections = {{"noise", "body", "", "", ""}, {"body", "out", "", "", ""}};
    const std::string bodyCode = OrchFaust::FaustGraphCompiler::compile(bodyGraph, convolutionError);
    if (bodyCode.empty() || bodyCode.find("node_body = node_noise") == std::string::npos || bodyCode.find("fi.fir") != std::string::npos) {
        std::cout << "FAILED: Legacy Body Convolution graph was not safely bypassed." << std::endl;
        return 1;
    }
    std::cout << "  OrchFaustSynth Integration Test Harness" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 1. Define Correct Graph JSON Representation
    std::string jsonGraph = R"({
        "version": 1,
        "name": "TestPatch",
        "nodes": [
            {
                "id": "tuning1",
                "type": "temperament_tuning",
                "params": {
                    "mode": 0.0,
                    "root_note": 0.0,
                    "scale_amount": 1.0,
                    "cent_2": 3.910,
                    "cent_4": -13.686
                }
            },
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
                "id": "debug1",
                "type": "debug",
                "params": {}
            },
            {
                "id": "sym1",
                "type": "sympathetic_strings",
                "params": {
                    "freq": 440.0,
                    "keyboard_tracking": 1.0,
                    "tuning_preset": 1.0,
                    "q": 35.0,
                    "sym_decay": 5.0,
                    "sym_coupling": 0.8,
                    "jawari": 0.25,
                    "bridge_tone": 0.45,
                    "string_1_semitones": 0.0,
                    "string_2_semitones": 7.0,
                    "string_3_semitones": 12.0,
                    "string_4_semitones": 19.0,
                    "string_5_semitones": 24.0,
                    "string_6_semitones": 31.0,
                    "wet": 0.6,
                    "gain": 0.8
                }
            },
            {
                "id": "rev1",
                "type": "reverb",
                "params": {
                    "size": 0.55,
                    "damp": 0.35,
                    "wet": 0.35
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
                "source": "tuning1",
                "sourceHandle": "output-2",
                "target": "osc1",
                "targetHandle": "param-freq"
            },
            {
                "source": "osc1",
                "target": "debug1"
            },
            {
                "source": "debug1",
                "target": "gain1"
            },
            {
                "source": "gain1",
                "target": "sym1"
            },
            {
                "source": "sym1",
                "target": "rev1"
            },
            {
                "source": "rev1",
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
#ifdef _WIN32
    std::string importPath = dllDir + "\\faust";
#else
    std::string importPath = dllDir + "/faust";
#endif
    std::cout << "   Using import path: " << importPath << std::endl;

    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(importPath.c_str());

    llvm_dsp_factory* bodyFactory = createDSPFactoryFromString(
        "OrchFaustBodyConvolutionTest", bodyCode, static_cast<int>(argv.size()), argv.data(), "", convolutionError, -1);
    if (!bodyFactory) {
        std::cout << "FAILED: Body Convolution Faust JIT failed: " << convolutionError << "\n" << bodyCode << std::endl;
        return 1;
    }
    deleteDSPFactory(bodyFactory);
    std::cout << "SUCCESS: Body Convolution graph JIT compiles." << std::endl;

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

    MapUI tuningUI;
    dsp->buildUserInterface(&tuningUI);
    auto findZone = [&](const std::string& suffix) -> FAUSTFLOAT* {
        for (const auto& [path, zone] : tuningUI.getFullpathMap()) {
            if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return zone;
            }
        }
        return nullptr;
    };
    FAUSTFLOAT* noteZone = findZone("/note_number");
    FAUSTFLOAT* modeZone = findZone("/tuning1_mode");
    FAUSTFLOAT* rootZone = findZone("/tuning1_root_note");
    FAUSTFLOAT* amountZone = findZone("/tuning1_scale_amount");
    FAUSTFLOAT* customFourthZone = findZone("/tuning1_cent_4");
    FAUSTFLOAT* playedMeter = findZone("/debug_tuning1_note");
    FAUSTFLOAT* pitchClassMeter = findZone("/debug_tuning1_pitch_class");
    FAUSTFLOAT* correctionMeter = findZone("/debug_tuning1_correction");
    if (!noteZone || !modeZone || !rootZone || !amountZone || !customFourthZone || !playedMeter || !pitchClassMeter || !correctionMeter) {
        std::cout << "FAILED: Temperament controls or debug outputs were not exposed by Faust." << std::endl;
        delete dsp;
        deleteDSPFactory(factory);
        return 1;
    }

    const int tuningFrames = 8;
    std::vector<float> tuningLeft(tuningFrames, 0.0f);
    std::vector<float> tuningRight(tuningFrames, 0.0f);
    float* tuningOutputs[2] = {tuningLeft.data(), tuningRight.data()};
    auto runTuningCase = [&](float note, float mode, float root, float amount,
                             float expectedPitchClass, float expectedCorrection,
                             const char* label) -> bool {
        *noteZone = note;
        *modeZone = mode;
        *rootZone = root;
        *amountZone = amount;
        dsp->compute(tuningFrames, nullptr, tuningOutputs);
        const bool passed = std::abs(static_cast<float>(*playedMeter) - note) < 0.001f &&
            std::abs(static_cast<float>(*pitchClassMeter) - expectedPitchClass) < 0.001f &&
            std::abs(static_cast<float>(*correctionMeter) - expectedCorrection) < 0.001f;
        if (!passed) {
            std::cout << "FAILED: " << label << " (note=" << *playedMeter
                      << ", pitchClass=" << *pitchClassMeter
                      << ", correction=" << *correctionMeter << ")" << std::endl;
        }
        return passed;
    };
    const float majorThirdCorrection = static_cast<float>(12.0 * std::log2(5.0 / 4.0) - 4.0);
    const float majorSecondCorrection = static_cast<float>(12.0 * std::log2(9.0 / 8.0) - 2.0);
    if (!runTuningCase(-1.0f, 2.0f, 0.0f, 1.0f, 11.0f, 0.0f, "no note bypass") ||
        !runTuningCase(64.0f, 1.0f, 0.0f, 1.0f, 4.0f, 0.0f, "equal temperament") ||
        !runTuningCase(64.0f, 2.0f, 0.0f, 1.0f, 4.0f, majorThirdCorrection, "just major third") ||
        !runTuningCase(76.0f, 2.0f, 0.0f, 1.0f, 4.0f, majorThirdCorrection, "multiple octaves") ||
        !runTuningCase(64.0f, 2.0f, 2.0f, 1.0f, 2.0f, majorSecondCorrection, "root note change") ||
        !runTuningCase(64.0f, 0.0f, 0.0f, 1.0f, 4.0f, 0.0f, "disabled tuning") ||
        !runTuningCase(64.0f, 2.0f, 0.0f, 0.5f, 4.0f, majorThirdCorrection * 0.5f, "scale amount")) {
        delete dsp;
        deleteDSPFactory(factory);
        return 1;
    }
    *customFourthZone = 25.0f;
    if (!runTuningCase(64.0f, 2.0f, 0.0f, 1.0f, 4.0f, 0.25f, "custom graph cents")) {
        delete dsp;
        deleteDSPFactory(factory);
        return 1;
    }
    std::cout << "SUCCESS: Faust temperament tuning cases passed." << std::endl;

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
