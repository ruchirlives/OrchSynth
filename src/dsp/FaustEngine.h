#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include "faust/gui/MapUI.h"

// Forward declarations of Faust types
class llvm_dsp_factory;
class llvm_dsp;

namespace OrchFaust {

class FaustEngine {
public:
    FaustEngine();
    ~FaustEngine();

    bool compileFromCode(const std::string& name, const std::string& code);
    void prepare(double sampleRate, int blockSize);
    void process(float** inputs, float** outputs, int numFrames);
    void setParameter(const std::string& path, float value);
    float getParameter(const std::string& path);
    std::string getLastError() const { return lastError; }
    bool isCompiled() const { return compiled.load(); }

private:
    void cleanup();

    llvm_dsp_factory* factory;
    llvm_dsp* dspInstance;
    std::unique_ptr<MapUI> mapUI;

    double currentSampleRate;
    int currentBlockSize;

    std::atomic<bool> compiled;
    std::string lastError;
    std::mutex engineMutex;
};

} // namespace OrchFaust
