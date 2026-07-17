#include "ConvolutionProcessor.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace OrchFaust {

void ConvolutionProcessor::prepare(double newSampleRate, int maximumBlockSize) {
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const size_t size = static_cast<size_t>((std::max)(maximumBlockSize, 1));
    wetLeft.assign(size, 0.0f);
    wetRight.assign(size, 0.0f);
}

void ConvolutionProcessor::clear() {
    leftConvolver.reset();
    rightConvolver.reset();
    ready = false;
}

void ConvolutionProcessor::setMix(float newWet, float newGain) {
    wet = std::clamp(newWet, 0.0f, 1.0f);
    gain = std::clamp(newGain, 0.0f, 4.0f);
}

bool ConvolutionProcessor::loadMonoImpulseResponse(const std::string& path, double targetSampleRate,
                                                   size_t maximumSamples, bool normalize,
                                                   std::vector<float>& samples, std::string& error) {
    samples.clear();
    if (path.empty()) {
        error = "No impulse response file selected";
        return false;
    }
    unsigned int channels = 0;
    unsigned int sourceRate = 0;
    drwav_uint64 frameCount = 0;
    float* interleaved = drwav_open_file_and_read_pcm_frames_f32(
        path.c_str(), &channels, &sourceRate, &frameCount, nullptr);
    if (!interleaved || channels == 0 || sourceRate == 0 || frameCount == 0) {
        if (interleaved) drwav_free(interleaved, nullptr);
        error = "Could not open WAV impulse response: " + path;
        return false;
    }
    const double ratio = targetSampleRate / static_cast<double>(sourceRate);
    const size_t resampledSize = static_cast<size_t>(std::ceil(static_cast<double>(frameCount) * ratio));
    if (resampledSize == 0) {
        drwav_free(interleaved, nullptr);
        error = "Body impulse response contains no audio samples";
        return false;
    }
    std::vector<float> source(static_cast<size_t>(frameCount));
    for (size_t frame = 0; frame < source.size(); ++frame) {
        float sum = 0.0f;
        for (unsigned int channel = 0; channel < channels; ++channel) sum += interleaved[frame * channels + channel];
        source[frame] = sum / static_cast<float>(channels);
    }
    drwav_free(interleaved, nullptr);
    const size_t destinationSize = (std::min)(resampledSize, maximumSamples);
    samples.resize(destinationSize);
    for (size_t i = 0; i < destinationSize; ++i) {
        const double sourcePosition = static_cast<double>(i) / ratio;
        const size_t index = static_cast<size_t>(sourcePosition);
        const float first = source[(std::min)(index, source.size() - 1)];
        const float second = source[(std::min)(index + 1, source.size() - 1)];
        samples[i] = first + static_cast<float>((second - first) * (sourcePosition - index));
    }
    if (resampledSize > maximumSamples) {
        // A body filter is intentionally short. Fade a clipped tail to prevent a
        // discontinuity from becoming an audible click or broad-band artefact.
        const size_t fadeLength = (std::min)(destinationSize, static_cast<size_t>(128));
        for (size_t i = 0; i < fadeLength; ++i) {
            samples[destinationSize - fadeLength + i] *= static_cast<float>(fadeLength - i) / static_cast<float>(fadeLength);
        }
    }
    if (normalize) {
        float peak = 0.0f;
        for (float sample : samples) peak = (std::max)(peak, std::abs(sample));
        if (peak > 0.000001f) for (float& sample : samples) sample /= peak;
    }
    return true;
}

bool ConvolutionProcessor::analyseBodyImpulseResponse(const std::string& path, double targetSampleRate,
                                                       bool normalize, std::vector<float>& frequencies,
                                                       std::vector<float>& levels, std::string& error) {
    std::vector<float> samples;
    if (!loadMonoImpulseResponse(path, targetSampleRate, 4096, normalize, samples, error)) return false;

    constexpr size_t kBands = 4;
    constexpr size_t kAnalysisSize = 2048;
    constexpr float pi = 3.14159265358979323846f;
    frequencies.clear();
    levels.clear();
    struct Peak { float level; float frequency; };
    std::vector<Peak> peaks;
    const size_t count = (std::min)(samples.size(), kAnalysisSize);
    if (count < 16) {
        error = "Body impulse response is too short to analyse";
        return false;
    }
    for (size_t band = 0; band < 48; ++band) {
        const float position = static_cast<float>(band) / 47.0f;
        const float frequency = 90.0f * std::pow(12000.0f / 90.0f, position);
        float real = 0.0f;
        float imaginary = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            const float window = 0.5f - 0.5f * std::cos((2.0f * pi * static_cast<float>(i)) / static_cast<float>(count - 1));
            const float phase = 2.0f * pi * frequency * static_cast<float>(i) / static_cast<float>(targetSampleRate);
            real += samples[i] * window * std::cos(phase);
            imaginary -= samples[i] * window * std::sin(phase);
        }
        peaks.push_back({std::sqrt(real * real + imaginary * imaginary), frequency});
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& left, const Peak& right) { return left.level > right.level; });
    const float strongest = peaks.empty() ? 0.0f : peaks.front().level;
    for (const Peak& peak : peaks) {
        bool separated = true;
        for (float selected : frequencies) {
            if (std::abs(std::log2(peak.frequency / selected)) < 0.45f) { separated = false; break; }
        }
        if (separated) {
            frequencies.push_back(peak.frequency);
            levels.push_back(strongest > 0.000001f ? peak.level / strongest : 0.0f);
            if (frequencies.size() == kBands) break;
        }
    }
    if (frequencies.empty()) {
        error = "Could not extract resonances from body impulse response";
        return false;
    }
    while (frequencies.size() < kBands) {
        frequencies.push_back(frequencies.back());
        levels.push_back(0.0f);
    }
    return true;
}

bool ConvolutionProcessor::resample(const std::vector<float>& source, std::uint32_t sourceRate,
                                    std::vector<float>& destination, std::string& error) const {
    if (source.empty() || sourceRate == 0) {
        error = "Impulse response contains no audio samples";
        return false;
    }

    const double ratio = sampleRate / static_cast<double>(sourceRate);
    const size_t destinationSize = static_cast<size_t>(std::ceil(source.size() * ratio));
    const size_t maximumSamples = static_cast<size_t>(sampleRate * kMaximumIrSeconds);
    if (destinationSize == 0 || destinationSize > maximumSamples) {
        error = "Impulse response exceeds the 20 second limit at the current sample rate";
        return false;
    }

    destination.resize(destinationSize);
    for (size_t i = 0; i < destinationSize; ++i) {
        const double sourcePosition = static_cast<double>(i) / ratio;
        const size_t index = static_cast<size_t>(sourcePosition);
        const double fraction = sourcePosition - static_cast<double>(index);
        const float first = source[(std::min)(index, source.size() - 1)];
        const float second = source[(std::min)(index + 1, source.size() - 1)];
        destination[i] = first + static_cast<float>((second - first) * fraction);
    }
    return true;
}

bool ConvolutionProcessor::loadImpulseResponse(const std::string& path, bool normalize, std::string& error) {
    clear();
    if (path.empty()) {
        error = "No impulse response file selected";
        return false;
    }

    unsigned int channels = 0;
    unsigned int sourceRate = 0;
    drwav_uint64 frameCount = 0;
    float* interleaved = drwav_open_file_and_read_pcm_frames_f32(
        path.c_str(), &channels, &sourceRate, &frameCount, nullptr);
    if (!interleaved) {
        error = "Could not open WAV impulse response: " + path;
        return false;
    }

    const bool valid = channels > 0 && frameCount > 0 &&
        frameCount <= (std::numeric_limits<size_t>::max)() / channels;
    if (!valid) {
        drwav_free(interleaved, nullptr);
        error = "Impulse response has invalid WAV format";
        return false;
    }

    std::vector<float> sourceLeft(static_cast<size_t>(frameCount));
    std::vector<float> sourceRight(static_cast<size_t>(frameCount));
    for (size_t frame = 0; frame < static_cast<size_t>(frameCount); ++frame) {
        sourceLeft[frame] = interleaved[frame * channels];
        sourceRight[frame] = channels > 1 ? interleaved[frame * channels + 1] : sourceLeft[frame];
    }
    drwav_free(interleaved, nullptr);

    std::vector<float> irLeft;
    std::vector<float> irRight;
    if (!resample(sourceLeft, sourceRate, irLeft, error) || !resample(sourceRight, sourceRate, irRight, error)) {
        return false;
    }

    if (normalize) {
        // Peak normalisation makes a long, quiet room IR disproportionately loud
        // once all of its samples are summed by the convolver. Unit energy keeps
        // the return at a predictable level regardless of IR duration.
        double energy = 0.0;
        for (float sample : irLeft) energy += static_cast<double>(sample) * sample;
        for (float sample : irRight) energy += static_cast<double>(sample) * sample;
        if (energy > 0.000000000001) {
            const float multiplier = static_cast<float>(1.0 / std::sqrt(energy));
            for (float& sample : irLeft) sample *= multiplier;
            for (float& sample : irRight) sample *= multiplier;
        }
    }

    if (!leftConvolver.init(kPartitionSize, irLeft.data(), irLeft.size()) ||
        !rightConvolver.init(kPartitionSize, irRight.data(), irRight.size())) {
        clear();
        error = "Could not initialize convolution engine";
        return false;
    }

    ready = true;
    return true;
}

void ConvolutionProcessor::process(float* left, float* right, int numFrames) {
    if (!ready || !left || !right || numFrames <= 0 || wetLeft.empty() || wetRight.empty()) return;

    size_t offset = 0;
    const size_t frameCount = static_cast<size_t>(numFrames);
    while (offset < frameCount) {
        const size_t count = (std::min)(frameCount - offset, wetLeft.size());
        leftConvolver.process(left + offset, wetLeft.data(), count);
        rightConvolver.process(right + offset, wetRight.data(), count);
        const float dryMix = std::cos(wet * 1.57079632679f);
        const float wetMix = std::sin(wet * 1.57079632679f) * gain;
        for (size_t i = 0; i < count; ++i) {
            left[offset + i] = dryMix * left[offset + i] + wetMix * wetLeft[i];
            right[offset + i] = dryMix * right[offset + i] + wetMix * wetRight[i];
        }
        offset += count;
    }
}

} // namespace OrchFaust
