#pragma once

#include <string>
#include <tuple>
#include <vector>

namespace OrchFaust {

class OrchEditorView {
public:
    virtual ~OrchEditorView() = default;

    virtual void updatePortLabel(int port) = 0;
    virtual void updateCurrentPatchLabel(const std::string& name) = 0;
    virtual void updateDialLayout(const std::vector<std::tuple<std::string, std::string, float>>& layout) = 0;
    virtual void updateGraphState(const std::string& graphJson) = 0;
    virtual void updateWaveform(const std::vector<float>& samples) = 0;
};

} // namespace OrchFaust
