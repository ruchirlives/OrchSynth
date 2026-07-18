#pragma once

#include <memory>
#include <cstdint>
#include "faust/gui/MapUI.h"

class llvm_dsp;

namespace OrchFaust {

class Voice {
public:
    Voice();
    ~Voice();

    void setDsp(llvm_dsp* newDsp);
    void noteOn(std::int32_t noteId, int bus, int channel, int note,
                double velocity, double tuningSemitones);
    void noteOff(double releaseVelocity = 0.0);
    void setNotePressure(double value);
    void setNotePitch(double semitones);
    void setNoteTimbre(double value);
    void setNoteExpression(double value);
    void setChannelPressure(double value);
    void setChannelPitchBend(double normalizedValue, double rangeSemitones = 2.0);
    void setParameter(const std::string& path, float value);
    float getParameter(const std::string& path);
    void process(float** outputs, int numFrames, float** tempBuffers);
    void prepare(double rate);

    int getNote() const { return midiNote; }
    std::int32_t getNoteId() const { return noteId; }
    int getBus() const { return eventBus; }
    int getChannel() const { return midiChannel; }
    bool isActive() const { return active; }
    void clear();

private:
    bool setVoiceParam(const std::string& name, float value);
    void updateFrequency();

    llvm_dsp* dsp;
    std::unique_ptr<MapUI> mapUI;
    int midiNote;
    std::int32_t noteId;
    int eventBus;
    int midiChannel;
    double initialTuningSemitones;
    double notePitchSemitones;
    double channelPitchSemitones;
    bool hasNotePressure;
    bool active;
    bool gateActive;
    int silentFramesCount;
    double sampleRate;
};

} // namespace OrchFaust
