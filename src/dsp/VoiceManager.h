#pragma once

#include "Voice.h"
#include "PerformanceEvent.h"
#include <vector>
#include <mutex>
#include <string>

class llvm_dsp_factory;

namespace OrchFaust {

class VoiceManager {
public:
    VoiceManager(int maxVoices = 16);
    ~VoiceManager();

    void prepare(double sampleRate, int blockSize);
    void process(float** outputs, int numFrames, int outputOffset = 0, bool clearOutput = true);
    
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void applyPerformanceEvent(const PerformanceEvent& event);
    void allNotesOff();
    
    void setParameter(const std::string& path, float value);
    void setGlobalControl(const std::string& name, float value);
    float getParameter(const std::string& path);

    bool updateFactory(llvm_dsp_factory* newFactory);
    bool isCompiled() const { return compiled; }

    std::int32_t allocateNoteId();

private:
    int maxVoices;
    std::vector<std::unique_ptr<Voice>> voices;
    std::mutex voiceMutex;
    
    double sampleRate;
    int blockSize;
    bool compiled;
    std::int32_t nextGeneratedNoteId = -10000;

    // Temporary scratch buffers for voices rendering (stereo)
    std::vector<float> scratchBufferL;
    std::vector<float> scratchBufferR;
    float* scratchPointers[2];
};

} // namespace OrchFaust
