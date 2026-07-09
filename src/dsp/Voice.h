#pragma once

#include <memory>
#include "faust/gui/MapUI.h"

class llvm_dsp;

namespace OrchFaust {

class Voice {
public:
    Voice();
    ~Voice();

    void setDsp(llvm_dsp* newDsp);
    void noteOn(int note, float velocity);
    void noteOff();
    void setParameter(const std::string& path, float value);
    float getParameter(const std::string& path);
    void process(float** outputs, int numFrames, float** tempBuffers);
    void prepare(double rate);

    int getNote() const { return midiNote; }
    bool isActive() const { return active; }
    void clear();

private:
    bool setVoiceParam(const std::string& name, float value);

    llvm_dsp* dsp;
    std::unique_ptr<MapUI> mapUI;
    int midiNote;
    bool active;
    bool gateActive;
    int silentFramesCount;
    double sampleRate;
};

} // namespace OrchFaust
