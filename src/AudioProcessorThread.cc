#include "AudioProcessorThread.hpp"
#include <cmath>
#include <limits>
#include <cstring>
#include <algorithm>
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

void AudioProcessorThread::resetDSPState() {
    std::lock_guard<std::mutex> lock(dsp_state_mutex_);
    dsp_state_.reset();
}

void AudioProcessorThread::clearPendingQueue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_queue_.clear();
}

void AudioProcessorThread::processIQSamples(std::vector<std::complex<float>> iq_samples,
                                           double sample_rate, int audio_rate) {
    if (should_stop_ || iq_samples.empty()) {
        return;
    }

    ProcessingData data;
    data.iq_samples = std::move(iq_samples);  // Move instead of copy
    data.sample_rate = sample_rate;
    data.audio_rate = audio_rate;
    data.valid = true;

    std::lock_guard<std::mutex> lock(queue_mutex_);
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
            std::lock_guard<std::mutex> lock(queue_mutex_);
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

    constexpr double kTargetDemodRateHz = 96000.0;
    const int channel_decim =
        std::max(1, static_cast<int>(std::floor(sample_rate / kTargetDemodRateHz)));
    const double channel_sample_rate = sample_rate / channel_decim;
    const float limiter_threshold = 0.005f;

    // FM deviation for narrowband FM (typical: 5 kHz for voice)
    const double fm_deviation_hz = 5000.0;

    // Get current DSP state with lock
    DSPState local_state;
    {
        std::lock_guard<std::mutex> lock(dsp_state_mutex_);
        local_state = dsp_state_;
    }

    // Wideband FM on IQ makes estimateResidualCarrierHz() follow modulation, not
    // LO offset — logs showed kHz-class "residual" during TX and audible whistle.
    local_state.residual_freq_estimate_hz = 0.0;

    std::vector<std::complex<float>> channelized;
    channelized.reserve(iq_samples.size() /
                        static_cast<size_t>(channel_decim) + 1U);

    const double channel_cutoff_hz = std::min(10000.0, 0.45 * channel_sample_rate);
    const double lp_alpha = 1.0 - std::exp(-2.0 * M_PI * channel_cutoff_hz / sample_rate);
    const float lp_alpha_f = static_cast<float>(lp_alpha);
    const float lp_one_minus_alpha = 1.0f - lp_alpha_f;
    std::complex<float> decim_acc(0.0f, 0.0f);
    int decim_count = 0;

    for (size_t i = 0; i < iq_samples.size(); ++i) {
        const std::complex<float> shifted = iq_samples[i];

        local_state.channel_lp_state = lp_alpha_f * shifted +
                            lp_one_minus_alpha * local_state.channel_lp_state;

        decim_acc += local_state.channel_lp_state;
        ++decim_count;
        if (decim_count >= channel_decim) {
            channelized.push_back(decim_acc / static_cast<float>(channel_decim));
            decim_acc = std::complex<float>(0.0f, 0.0f);
            decim_count = 0;
        }
    }
    if (decim_count > 0) {
        channelized.push_back(decim_acc / static_cast<float>(decim_count));
    }

    if (channelized.size() < 2) {
        return {};
    }

    std::vector<float> demod;
    demod.reserve(channelized.size());
    

    const float phase_scale = static_cast<float>(channel_sample_rate / (2.0 * M_PI * fm_deviation_hz));
    
    std::complex<float> prev = channelized[0];
    float prev_mag = std::abs(prev);
    if (prev_mag > limiter_threshold) {
        prev /= prev_mag;
    } else {
        prev = std::complex<float>(1.0f, 0.0f);
    }
    for (size_t i = 1; i < channelized.size(); ++i) {
        std::complex<float> curr = channelized[i];
        float curr_mag = std::abs(curr);
        if (curr_mag > limiter_threshold) {
            curr /= curr_mag;
            std::complex<float> diff = curr * std::conj(prev);
            float angle = std::atan2(diff.imag(), diff.real());
            demod.push_back(angle * phase_scale);
            prev = curr;
        } else {
            demod.push_back(0.0f);
        }
    }

    if (demod.empty()) {
        return {};
    }

    // DC removal filter
    {
        const float dc_alpha = 0.995f;
        const float dc_one_minus_alpha = 1.0f - dc_alpha;
        for (float &v : demod) {
            local_state.dc_accumulator = dc_alpha * local_state.dc_accumulator +
                              dc_one_minus_alpha * v;
            v = v - local_state.dc_accumulator;
        }
    }

    const double deemphasis_tau_sec = 75e-6;
    const float deemph_alpha =
        static_cast<float>(std::exp(-1.0 / (channel_sample_rate * deemphasis_tau_sec)));
    const float deemph_one_minus_alpha = 1.0f - deemph_alpha;
    for (float &v : demod) {
        local_state.deemphasis_state =
            deemph_alpha * local_state.deemphasis_state + deemph_one_minus_alpha * v;
        v = local_state.deemphasis_state;
    }

    // Two cascaded 1st-order HPFs (~380 Hz) attenuate CTCSS/DCS sub-audio (67–254 Hz)
    // much more than a single soft HPF at 300 Hz (H8).
    const double voice_low_cut_hz = 380.0;
    const double voice_high_cut_hz = 3000.0;
    const float hp_alpha = static_cast<float>(
        std::exp(-2.0 * M_PI * voice_low_cut_hz / channel_sample_rate));
    const float voice_lp_alpha = static_cast<float>(
        1.0 - std::exp(-2.0 * M_PI * voice_high_cut_hz / channel_sample_rate));
    const float voice_lp_one_minus_alpha = 1.0f - voice_lp_alpha;
    
    std::vector<float> filtered_demod;
    filtered_demod.reserve(demod.size());
    
    for (size_t i = 0; i < demod.size(); ++i) {
        const float hp1 = hp_alpha * (local_state.voice_hp_prev_output + demod[i] -
                                      local_state.voice_hp_prev_input);
        local_state.voice_hp_prev_input = demod[i];
        local_state.voice_hp_prev_output = hp1;

        const float hp2 = hp_alpha * (local_state.voice_hp2_prev_output + hp1 -
                                       local_state.voice_hp2_prev_input);
        local_state.voice_hp2_prev_input = hp1;
        local_state.voice_hp2_prev_output = hp2;

        local_state.voice_lp_state =
            voice_lp_alpha * hp2 +
            voice_lp_one_minus_alpha * local_state.voice_lp_state;
        filtered_demod.push_back(local_state.voice_lp_state);
    }

    // Save DSP state back with lock after all filters update.
    {
        std::lock_guard<std::mutex> lock(dsp_state_mutex_);
        dsp_state_ = local_state;
    }

    std::vector<float> resampled;
    if (filtered_demod.size() >= 2) {
        const double resample_step = channel_sample_rate / audio_rate;
        double pos = 0.0;
        while (pos + 1.0 < static_cast<double>(filtered_demod.size())) {
            const int i0 = static_cast<int>(pos);
            const int i1 = i0 + 1;
            const float frac = static_cast<float>(pos - i0);
            const float interp = filtered_demod[static_cast<size_t>(i0)] *
                                     (1.0f - frac) +
                                 filtered_demod[static_cast<size_t>(i1)] * frac;
            resampled.push_back(interp);
            pos += resample_step;
        }
    }

    if (resampled.empty()) {
        return {};
    }

    std::vector<int16_t> audioSamples;
    audioSamples.reserve(resampled.size());

    float maxVal = 0.0f;
    for (float v : resampled) {
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

    for (size_t i = 0; i < resampled.size(); ++i) {
        float v = resampled[i] * scale;
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
