#include "AudioProcessorThread.hpp"
#include <cmath>
#include <limits>
#include <cstring>
#include <spdlog/spdlog.h>

AudioProcessorThread::AudioProcessorThread(QObject *parent)
    : QThread(parent) {
}

AudioProcessorThread::~AudioProcessorThread() {
    stopProcessing();
    wait();
}

void AudioProcessorThread::stopProcessing() {
    should_stop_ = true;
    running_ = false;
}

void AudioProcessorThread::resetDCAccumulator() {
    std::lock_guard<std::mutex> lock(dc_mutex_);
    dc_accumulator_ = 0.0f;
}

void AudioProcessorThread::processIQSamples(const std::vector<std::complex<float>> &iq_samples,
                                           double sample_rate, int audio_rate) {
    if (should_stop_) {
        return;
    }

    std::lock_guard<std::mutex> lock(data_mutex_);
    pending_data_.iq_samples = iq_samples;
    pending_data_.sample_rate = sample_rate;
    pending_data_.audio_rate = audio_rate;
    pending_data_.valid = true;
}

void AudioProcessorThread::run() {
    running_ = true;
    should_stop_ = false;

    while (!should_stop_) {
        ProcessingData data;
        bool has_data = false;

        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (pending_data_.valid) {
                data = pending_data_;
                pending_data_.valid = false;
                has_data = true;
            }
        }

        if (has_data && !data.iq_samples.empty()) {
            auto audio_samples = processDemodulation(data.iq_samples, 
                                                     data.sample_rate, 
                                                     data.audio_rate);
            if (!audio_samples.empty()) {
                emit audioSamplesReady(audio_samples);
            }
        } else {
            msleep(5);
        }
    }

    running_ = false;
}

std::vector<int16_t> AudioProcessorThread::processDemodulation(
    const std::vector<std::complex<float>> &iq_samples,
    double sample_rate, int audio_rate) {

    if (iq_samples.size() < 2) {
        return {};
    }

    if (sample_rate <= 0 || audio_rate <= 0) {
        return {};
    }

    int decim = static_cast<int>(sample_rate / audio_rate);
    if (decim < 1) {
        decim = 1;
    }

    std::vector<float> demod;
    demod.reserve(iq_samples.size());

    const float phase_scale = static_cast<float>(sample_rate) / (2.0f * M_PI);
    std::complex<float> prev = iq_samples[0];
    for (size_t i = 1; i < iq_samples.size(); ++i) {
        std::complex<float> curr = iq_samples[i];
        std::complex<float> diff = curr * std::conj(prev);
        float angle = std::atan2(diff.imag(), diff.real());
        demod.push_back(angle * phase_scale);
        prev = curr;
    }

    if (demod.empty()) {
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(dc_mutex_);
        const float dc_alpha = 0.995f;
        for (float &v : demod) {
            dc_accumulator_ = dc_alpha * dc_accumulator_ + v;
            v = v - dc_accumulator_;
        }
    }

    const int filter_length = static_cast<int>(sample_rate / 4000.0);
    const int actual_filter_length = (filter_length > 1) ? filter_length : 1;
    std::vector<float> filtered_demod;
    filtered_demod.reserve(demod.size());
    
    for (size_t i = 0; i < demod.size(); ++i) {
        float sum = 0.0f;
        int count = 0;
        int start = static_cast<int>(i) - actual_filter_length / 2;
        int end = static_cast<int>(i) + actual_filter_length / 2;
        start = (start < 0) ? 0 : start;
        end = (end >= static_cast<int>(demod.size())) ? static_cast<int>(demod.size() - 1) : end;
        
        for (int j = start; j <= end; ++j) {
            sum += demod[j];
            count++;
        }
        filtered_demod.push_back(sum / count);
    }

    std::vector<int16_t> audioSamples;
    audioSamples.reserve(filtered_demod.size() / decim + 1);

    float maxVal = 0.0f;
    for (float v : filtered_demod) {
        float absv = std::abs(v);
        if (absv > maxVal) {
            maxVal = absv;
        }
    }

    const float minPeak = 100.0f;
    float scale = 1.0f;
    if (maxVal > minPeak) {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * 0.8f / maxVal;
    } else {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * 0.1f / minPeak;
    }

    for (size_t i = 0; i < filtered_demod.size(); i += static_cast<size_t>(decim)) {
        float v = filtered_demod[i] * scale;
        if (v > static_cast<float>(std::numeric_limits<int16_t>::max())) {
            v = static_cast<float>(std::numeric_limits<int16_t>::max());
        }
        if (v < static_cast<float>(std::numeric_limits<int16_t>::min())) {
            v = static_cast<float>(std::numeric_limits<int16_t>::min());
        }
        audioSamples.push_back(static_cast<int16_t>(v));
    }

    return audioSamples;
}

