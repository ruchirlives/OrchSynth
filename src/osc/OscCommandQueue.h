#pragma once

#include <string>
#include <variant>
#include "util/LockFreeQueue.h"

namespace OrchFaust {

enum class CommandType {
    LoadGraph,
    Compile,
    SetParam,
    NoteOn,
    NoteOff,
    AllNotesOff,
    Status,
    RequestGraph
};

struct OscCommand {
    CommandType type;
    std::string stringArg1; // e.g. json graph for LoadGraph, nodeId for SetParam
    std::string stringArg2; // e.g. param for SetParam
    float floatArg1 = 0.0f; // e.g. value for SetParam, pitch for NoteOn/Off, velocity for NoteOn
    float floatArg2 = 0.0f; // e.g. velocity for NoteOn
};

using OscCommandQueue = LockFreeSPSCQueue<OscCommand, 2048>;

} // namespace OrchFaust
