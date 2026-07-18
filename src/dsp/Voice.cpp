#include "Voice.h"
#include "faust/dsp/llvm-dsp.h"
#include <cmath>

namespace OrchFaust {

Voice::Voice()
    : dsp(nullptr), midiNote(-1), noteId(-1), eventBus(0), midiChannel(0),
      initialTuningSemitones(0.0), notePitchSemitones(0.0), channelPitchSemitones(0.0),
      hasNotePressure(false), active(false), gateActive(false), silentFramesCount(0),
      sampleRate(44100.0) {}

Voice::~Voice() {
    clear();
}

void Voice::clear() {
    if (dsp) {
        delete dsp;
        dsp = nullptr;
    }
    mapUI.reset();
    midiNote = -1;
    noteId = -1;
    initialTuningSemitones = 0.0;
    notePitchSemitones = 0.0;
    channelPitchSemitones = 0.0;
    hasNotePressure = false;
    active = false;
    gateActive = false;
    silentFramesCount = 0;
}

void Voice::setDsp(llvm_dsp* newDsp) {
    clear();
    dsp = newDsp;
    if (dsp) {
        mapUI = std::make_unique<MapUI>();
        dsp->buildUserInterface(mapUI.get());
        dsp->init(static_cast<int>(sampleRate));
    }
}

bool Voice::setVoiceParam(const std::string& name, float value) {
    if (!mapUI) return false;
    
    bool isNodeParam = (name.find('/') != std::string::npos);
    
    std::string suffix = name;
    if (suffix.empty()) return false;
    if (suffix[0] != '/') {
        suffix = "/" + suffix;
    }
    
    auto& paths = mapUI->getFullpathMap();
    for (const auto& [path, zone] : paths) {
        size_t slashCount = 0;
        for (char c : path) {
            if (c == '/') slashCount++;
        }
        
        if (isNodeParam) {
            if (path.length() >= suffix.length() && 
                path.compare(path.length() - suffix.length(), suffix.length(), suffix) == 0) {
                *zone = value;
                return true;
            }
        } else {
            if (slashCount == 2 && 
                path.length() >= suffix.length() && 
                path.compare(path.length() - suffix.length(), suffix.length(), suffix) == 0) {
                *zone = value;
                return true;
            }
        }
    }
    
    return false;
}

void Voice::noteOn(std::int32_t newNoteId, int bus, int channel, int note,
                   double velocity, double tuningSemitones) {
    noteId = newNoteId;
    eventBus = bus;
    midiChannel = channel;
    midiNote = note;
    initialTuningSemitones = tuningSemitones;
    notePitchSemitones = 0.0;
    hasNotePressure = false;
    active = true;
    gateActive = true;
    silentFramesCount = 0;
    
    if (mapUI) {
        updateFrequency();
        setVoiceParam("gain", static_cast<float>(velocity));
        setVoiceParam("velocity", static_cast<float>(velocity));
        setVoiceParam("release_velocity", 0.0f);
        setVoiceParam("note_number", static_cast<float>(note));
        setVoiceParam("note_pressure", 0.0f);
        setVoiceParam("note_pitch", 0.0f);
        setVoiceParam("note_timbre", 0.0f);
        setVoiceParam("note_expression", 0.0f);
        setVoiceParam("gate", 1.0f);
    }
}

void Voice::noteOff(double releaseVelocity) {
    if (mapUI) {
        setVoiceParam("release_velocity", static_cast<float>(releaseVelocity));
        setVoiceParam("gate", 0.0f);
    }
    gateActive = false;
}

void Voice::updateFrequency() {
    if (midiNote < 0) return;
    const double semitones = static_cast<double>(midiNote - 69) + initialTuningSemitones +
        notePitchSemitones + channelPitchSemitones;
    const float frequency = static_cast<float>(440.0 * std::exp2(semitones / 12.0));
    setVoiceParam("freq", frequency);
    setVoiceParam("note_frequency", frequency);
}

void Voice::setNotePressure(double value) {
    hasNotePressure = true;
    const float v = static_cast<float>(value);
    setVoiceParam("note_pressure", v);
    setVoiceParam("aftertouch", v);
}

void Voice::setNotePitch(double semitones) {
    notePitchSemitones = semitones;
    setVoiceParam("note_pitch", static_cast<float>(semitones));
    updateFrequency();
}

void Voice::setNoteTimbre(double value) {
    setVoiceParam("note_timbre", static_cast<float>(value));
}

void Voice::setNoteExpression(double value) {
    setVoiceParam("note_expression", static_cast<float>(value));
}

void Voice::setChannelPressure(double value) {
    setVoiceParam("channel_pressure", static_cast<float>(value));
    if (!hasNotePressure) setVoiceParam("aftertouch", static_cast<float>(value));
}

void Voice::setChannelPitchBend(double normalizedValue, double rangeSemitones) {
    channelPitchSemitones = normalizedValue * rangeSemitones;
    setVoiceParam("channel_pitch_bend", static_cast<float>(normalizedValue));
    setVoiceParam("pitch_bend", static_cast<float>(normalizedValue));
    updateFrequency();
}

void Voice::setParameter(const std::string& path, float value) {
    setVoiceParam(path, value);
}

float Voice::getParameter(const std::string& path) {
    if (!mapUI) return 0.0f;
    
    bool isNodeParam = (path.find('/') != std::string::npos);
    
    std::string suffix = path;
    if (suffix.empty()) return 0.0f;
    if (suffix[0] != '/') suffix = "/" + suffix;
    
    auto& paths = mapUI->getFullpathMap();
    for (const auto& [fullpath, zone] : paths) {
        size_t slashCount = 0;
        for (char c : fullpath) {
            if (c == '/') slashCount++;
        }
        
        if (isNodeParam) {
            if (fullpath.length() >= suffix.length() && 
                fullpath.compare(fullpath.length() - suffix.length(), suffix.length(), suffix) == 0) {
                return *zone;
            }
        } else {
            if (slashCount == 2 && 
                fullpath.length() >= suffix.length() && 
                fullpath.compare(fullpath.length() - suffix.length(), suffix.length(), suffix) == 0) {
                return *zone;
            }
        }
    }
    return 0.0f;
}

void Voice::prepare(double rate) {
    sampleRate = rate;
    if (dsp) {
        dsp->init(static_cast<int>(sampleRate));
    }
}

void Voice::process(float** outputs, int numFrames, float** tempBuffers) {
    if (!dsp) return;
    
    // Clear temp buffer
    if (tempBuffers && tempBuffers[0]) memset(tempBuffers[0], 0, numFrames * sizeof(float));
    if (tempBuffers && tempBuffers[1]) memset(tempBuffers[1], 0, numFrames * sizeof(float));
    
    // Render voice into temp buffer
    dsp->compute(numFrames, nullptr, reinterpret_cast<FAUSTFLOAT**>(tempBuffers));
    
    // Sum temp buffer into outputs and measure peak amplitude
    float peak = 0.0f;
    for (int sample = 0; sample < numFrames; ++sample) {
        if (outputs) {
            if (outputs[0] && tempBuffers && tempBuffers[0]) outputs[0][sample] += tempBuffers[0][sample];
            if (outputs[1] && tempBuffers && tempBuffers[1]) outputs[1][sample] += tempBuffers[1][sample];
        }
        
        float absL = 0.0f;
        float absR = 0.0f;
        if (tempBuffers) {
            if (tempBuffers[0]) absL = std::abs(tempBuffers[0][sample]);
            if (tempBuffers[1]) absR = std::abs(tempBuffers[1][sample]);
        }
        if (absL > peak) peak = absL;
        if (absR > peak) peak = absR;
    }
    
    // Decays to silence tracking
    if (!gateActive && active) {
        if (peak < 0.0001f) { // -80dB
            silentFramesCount += numFrames;
            // Mark inactive after 0.2 seconds of silence (9600 samples at 48k)
            int thresholdSamples = static_cast<int>(0.2 * sampleRate);
            if (silentFramesCount > thresholdSamples) {
                active = false;
            }
        } else {
            silentFramesCount = 0;
        }
    }
}

} // namespace OrchFaust
