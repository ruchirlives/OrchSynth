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
};

} // namespace OrchFaust
