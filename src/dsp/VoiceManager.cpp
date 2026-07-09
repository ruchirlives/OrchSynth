#include "VoiceManager.h"
#include "faust/dsp/llvm-dsp.h"
#include "util/Logging.h"
#include <algorithm>

namespace OrchFaust {

VoiceManager::VoiceManager(int maxVoices)
    : maxVoices(maxVoices), sampleRate(44100.0), blockSize(512), compiled(false) {
    
    voices.reserve(maxVoices);
    for (int i = 0; i < maxVoices; ++i) {
        voices.push_back(std::make_unique<Voice>());
    }

    scratchBufferL.resize(8192, 0.0f);
    scratchBufferR.resize(8192, 0.0f);
    scratchPointers[0] = scratchBufferL.data();
    scratchPointers[1] = scratchBufferR.data();
}

VoiceManager::~VoiceManager() {
    allNotesOff();
}

void VoiceManager::prepare(double rate, int size) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    sampleRate = rate;
    blockSize = size;

    if (scratchBufferL.size() < static_cast<size_t>(size)) {
        scratchBufferL.resize(size, 0.0f);
        scratchBufferR.resize(size, 0.0f);
    }
    scratchPointers[0] = scratchBufferL.data();
    scratchPointers[1] = scratchBufferR.data();

    for (auto& voice : voices) {
        voice->prepare(rate);
    }
}

bool VoiceManager::updateFactory(llvm_dsp_factory* newFactory) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    
    if (!newFactory) {
        compiled = false;
        for (auto& voice : voices) {
            voice->clear();
        }
        return false;
    }
    
    Logger::logInfo("VoiceManager: Instantiating ", maxVoices, " voices from new factory...");
    
    for (int i = 0; i < maxVoices; ++i) {
        llvm_dsp* dspInst = newFactory->createDSPInstance();
        if (!dspInst) {
            Logger::logError("VoiceManager: Failed to create DSP instance for voice ", i);
            compiled = false;
            return false;
        }
        
        // Preserve parameters if old voice exists
        float oldParams[64];
        std::vector<std::string> paths;
        
        // Get old parameters
        if (compiled && voices[i]->isActive()) {
            // We just update the DSP, but keep the note running if possible
            // To make it simple, let's reset voice note status but keep parameter mappings
        }
        
        voices[i]->setDsp(dspInst);
    }
    
    compiled = true;
    Logger::logInfo("VoiceManager: All voices updated successfully.");
    return true;
}

void VoiceManager::noteOn(int note, float velocity) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    if (!compiled) return;

    // Find if note is already playing (re-trigger)
    for (auto& voice : voices) {
        if (voice->isActive() && voice->getNote() == note) {
            voice->noteOn(note, velocity);
            return;
        }
    }

    // Find free voice
    for (auto& voice : voices) {
        if (!voice->isActive()) {
            voice->noteOn(note, velocity);
            return;
        }
    }

    // Voice stealing: steal first active voice
    for (auto& voice : voices) {
        if (voice->isActive()) {
            voice->noteOff();
            voice->noteOn(note, velocity);
            return;
        }
    }
}

void VoiceManager::noteOff(int note) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    if (!compiled) return;

    for (auto& voice : voices) {
        if (voice->isActive() && voice->getNote() == note) {
            voice->noteOff();
        }
    }
}

void VoiceManager::allNotesOff() {
    std::lock_guard<std::mutex> lock(voiceMutex);
    if (!compiled) return;

    for (auto& voice : voices) {
        if (voice->isActive()) {
            voice->noteOff();
        }
    }
}

void VoiceManager::setParameter(const std::string& path, float value) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    for (auto& voice : voices) {
        voice->setParameter(path, value);
    }
}

float VoiceManager::getParameter(const std::string& path) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    if (!voices.empty()) {
        return voices[0]->getParameter(path);
    }
    return 0.0f;
}

void VoiceManager::process(float** outputs, int numFrames) {
    // Clear outputs
    for (int i = 0; i < 2; ++i) {
        if (outputs[i]) {
            memset(outputs[i], 0, numFrames * sizeof(float));
        }
    }

    // Resize scratch if needed
    if (scratchBufferL.size() < static_cast<size_t>(numFrames)) {
        scratchBufferL.resize(numFrames, 0.0f);
        scratchBufferR.resize(numFrames, 0.0f);
        scratchPointers[0] = scratchBufferL.data();
        scratchPointers[1] = scratchBufferR.data();
    }

    std::lock_guard<std::mutex> lock(voiceMutex);
    if (!compiled) return;

    for (auto& voice : voices) {
        if (voice->isActive()) {
            voice->process(outputs, numFrames, scratchPointers);
        }
    }
}

} // namespace OrchFaust
