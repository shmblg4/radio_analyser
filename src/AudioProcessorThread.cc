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

void AudioProcessorThread::clearPendingQueue() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    pending_queue_.clear();
}

void AudioProcessorThread::processIQSamples(const std::vector<std::complex<float>> &iq_samples,
                                           double sample_rate, int audio_rate) {
    if (should_stop_ || iq_samples.empty()) {
        return;
    }

    ProcessingData data;
    data.iq_samples = iq_samples;
    data.sample_rate = sample_rate;
    data.audio_rate = audio_rate;
    data.valid = true;

    std::lock_guard<std::mutex> lock(data_mutex_);
    if (pending_queue_.size() >= kMaxPendingChunks) {
        pending_queue_.pop_front();
    }
    pending_queue_.push_back(std::move(data));
}

void AudioProcessorThread::run() {
    running_ = true;
    should_stop_ = false;

    while (!should_stop_) {
        ProcessingData data;
        bool has_data = false;

        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (!pending_queue_.empty()) {
                data = std::move(pending_queue_.front());
                pending_queue_.pop_front();
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
            msleep(2);
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
    const float limiter_threshold = 0.02f;
    std::complex<float> prev = iq_samples[0];
    float prev_mag = std::abs(prev);
    if (prev_mag > limiter_threshold) {
        prev /= prev_mag;
    } else {
        prev = std::complex<float>(0.0f, 0.0f);
    }
    for (size_t i = 1; i < iq_samples.size(); ++i) {
        std::complex<float> curr = iq_samples[i];
        float curr_mag = std::abs(curr);
        if (curr_mag > limiter_threshold) {
            curr /= curr_mag;
            std::complex<float> diff = curr * std::conj(prev);
            float angle = std::atan2(diff.imag(), diff.real());
            demod.push_back(angle * phase_scale);
            prev = curr;
        } else {
            demod.push_back(0.0f);
            prev = std::complex<float>(0.0f, 0.0f);
        }
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

    const double voice_cutoff_hz = 4000.0;
    int filter_length = static_cast<int>(sample_rate / voice_cutoff_hz);
    if (filter_length < 1) filter_length = 1;
    const int half = filter_length / 2;
    const int n = static_cast<int>(demod.size());
    std::vector<float> filtered_demod;
    filtered_demod.reserve(demod.size());

    float running_sum = 0.0f;
    int run_count = 0;
    for (int j = 0; j <= half && j < n; ++j) {
        running_sum += demod[static_cast<size_t>(j)];
        run_count++;
    }
    for (int i = 0; i < n; ++i) {
        filtered_demod.push_back(run_count > 0 ? running_sum / run_count : 0.0f);
        if (i >= half) {
            running_sum -= demod[static_cast<size_t>(i - half)];
            run_count--;
        }
        if (i + half + 1 < n) {
            running_sum += demod[static_cast<size_t>(i + half + 1)];
            run_count++;
        }
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
    const float strongScale = 0.8f;
    const float weakScaleFraction = 0.4f;
    float scale;
    if (maxVal > minPeak) {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * strongScale / maxVal;
    } else if (maxVal > 1.0f) {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * weakScaleFraction / maxVal;
    } else {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * weakScaleFraction;
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

