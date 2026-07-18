#pragma once

#include <cstdint>

namespace OrchFaust {

enum class PerformanceEventType {
    NoteOn,
    NoteOff,
    NotePressure,
    NotePitch,
    NoteTimbre,
    NoteExpression,
    ChannelPressure,
    ChannelPitchBend,
    ChannelController,
    AllNotesOff
};

struct PerformanceEvent {
    PerformanceEventType type = PerformanceEventType::NoteOn;
    std::int32_t sampleOffset = 0;
    std::int32_t noteId = -1;
    std::int16_t eventBus = 0;
    std::int16_t channel = 0;
    std::int16_t pitch = 0;
    std::uint32_t controllerId = 0;
    double value = 0.0;
    double velocity = 0.0;
    double tuningSemitones = 0.0;
};

} // namespace OrchFaust
