#pragma once

#include "FFTConvolver.h"

#include <cstddef>
#include <string>
#include <vector>

namespace OrchFaust {

class ConvolutionProcessor {
public:
    void prepare(double sampleRate, int maximumBlockSize);
    static bool loadMonoImpulseResponse(const std::string& path, double targetSampleRate,
                                        size_t maximumSamples, bool normalize,
                                        std::vector<float>& samples, std::string& error);
    static bool analyseBodyImpulseResponse(const std::string& path, double targetSampleRate,
                                           bool normalize, std::vector<float>& frequencies,
                                           std::vector<float>& levels, std::string& error);
    bool loadImpulseResponse(const std::string& path, bool normalize, std::string& error);
    void clear();

    void setMix(float wet, float gain);
    bool isReady() const { return ready; }
    void process(float* left, float* right, int numFrames);

private:
    static constexpr size_t kPartitionSize = 1024;
    static constexpr double kMaximumIrSeconds = 20.0;

    bool resample(const std::vector<float>& source, std::uint32_t sourceRate,
                  std::vector<float>& destination, std::string& error) const;

    double sampleRate = 44100.0;
    std::vector<float> wetLeft;
    std::vector<float> wetRight;
    fftconvolver::FFTConvolver leftConvolver;
    fftconvolver::FFTConvolver rightConvolver;
    float wet = 0.35f;
    float gain = 1.0f;
    bool ready = false;
};

} // namespace OrchFaust
