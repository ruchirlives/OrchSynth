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
    PerformanceEvent event;
    event.type = PerformanceEventType::NoteOn;
    event.noteId = allocateNoteId();
    event.pitch = static_cast<std::int16_t>(note);
    event.velocity = velocity;
    applyPerformanceEvent(event);
}

std::int32_t VoiceManager::allocateNoteId() {
    const auto result = nextGeneratedNoteId;
    ++nextGeneratedNoteId;
    if (nextGeneratedNoteId > -1000) nextGeneratedNoteId = -10000;
    return result;
}

void VoiceManager::applyPerformanceEvent(const PerformanceEvent& event) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    if (!compiled) return;

    auto matchingVoice = [&]() -> Voice* {
        if (event.noteId != -1) {
            for (auto& voice : voices) {
                if (voice->isActive() && voice->getNoteId() == event.noteId) return voice.get();
            }
        }
        for (auto& voice : voices) {
            if (voice->isActive() && voice->getBus() == event.eventBus &&
                voice->getChannel() == event.channel && voice->getNote() == event.pitch) return voice.get();
        }
        return nullptr;
    };

    switch (event.type) {
        case PerformanceEventType::NoteOn: {
            Voice* target = nullptr;
            if (event.noteId != -1) target = matchingVoice();
            if (!target) {
                for (auto& voice : voices) {
                    if (!voice->isActive()) { target = voice.get(); break; }
                }
            }
            if (!target && !voices.empty()) {
                target = voices.front().get();
                target->noteOff();
            }
            if (target) target->noteOn(event.noteId, event.eventBus, event.channel, event.pitch,
                                       event.velocity, event.tuningSemitones);
            break;
        }
        case PerformanceEventType::NoteOff:
            if (auto* voice = matchingVoice()) voice->noteOff(event.velocity);
            break;
        case PerformanceEventType::NotePressure:
            if (auto* voice = matchingVoice()) voice->setNotePressure(event.value);
            break;
        case PerformanceEventType::NotePitch:
            if (auto* voice = matchingVoice()) voice->setNotePitch(event.value);
            break;
        case PerformanceEventType::NoteTimbre:
            if (auto* voice = matchingVoice()) voice->setNoteTimbre(event.value);
            break;
        case PerformanceEventType::NoteExpression:
            if (auto* voice = matchingVoice()) voice->setNoteExpression(event.value);
            break;
        case PerformanceEventType::ChannelPressure:
            for (auto& voice : voices) {
                if (voice->isActive() && voice->getBus() == event.eventBus && voice->getChannel() == event.channel)
                    voice->setChannelPressure(event.value);
            }
            break;
        case PerformanceEventType::ChannelPitchBend:
            for (auto& voice : voices) {
                if (voice->isActive() && voice->getBus() == event.eventBus && voice->getChannel() == event.channel)
                    voice->setChannelPitchBend(event.value);
            }
            break;
        case PerformanceEventType::ChannelController:
            for (auto& voice : voices) {
                if (voice->isActive() && voice->getBus() == event.eventBus && voice->getChannel() == event.channel)
                    voice->setParameter("cc_" + std::to_string(event.controllerId), static_cast<float>(event.value));
            }
            break;
        case PerformanceEventType::AllNotesOff:
            for (auto& voice : voices) if (voice->isActive()) voice->noteOff();
            break;
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

void VoiceManager::setGlobalControl(const std::string& name, float value) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    for (auto& voice : voices) {
        voice->setParameter(name, value);
    }
}

float VoiceManager::getParameter(const std::string& path) {
    std::lock_guard<std::mutex> lock(voiceMutex);
    for (auto& voice : voices) {
        if (voice->isActive()) return voice->getParameter(path);
    }
    if (!voices.empty()) {
        return voices[0]->getParameter(path);
    }
    return 0.0f;
}

void VoiceManager::process(float** outputs, int numFrames, int outputOffset, bool clearOutput) {
    // Clear outputs
    for (int i = 0; i < 2; ++i) {
        if (clearOutput && outputs[i]) {
            memset(outputs[i] + outputOffset, 0, numFrames * sizeof(float));
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
            float* rangedOutputs[2] = {
                outputs[0] ? outputs[0] + outputOffset : nullptr,
                outputs[1] ? outputs[1] + outputOffset : nullptr
            };
            voice->process(rangedOutputs, numFrames, scratchPointers);
        }
    }
}

} // namespace OrchFaust
