#include "SpectrumWorker.hpp"
#include "MainWindowConstants.hpp"

#include <QThread>
#include <algorithm>
#include <atomic>
#include <spdlog/spdlog.h>

namespace {
std::atomic<bool> g_spectrum_worker_busy{false};

bool sameSweepRfBase(const hackrf_alloc_params &a,
                     const hackrf_alloc_params &b) {
    return a.sample_rate == b.sample_rate && a.bandwidth == b.bandwidth &&
           a.fft_size == b.fft_size && a.lna_gain == b.lna_gain &&
           a.vga_gain == b.vga_gain;
}

void suppressSegmentDcPeak(std::vector<double> &segment_db, int fft_size) {
    if (segment_db.size() != static_cast<size_t>(fft_size) || fft_size < 5) {
        return;
    }

    const int dc = fft_size / 2;
    const double left = segment_db[static_cast<size_t>(dc - 2)];
    const double right = segment_db[static_cast<size_t>(dc + 2)];
    segment_db[static_cast<size_t>(dc - 1)] = left + (right - left) * 0.25;
    segment_db[static_cast<size_t>(dc)] = left + (right - left) * 0.5;
    segment_db[static_cast<size_t>(dc + 1)] = left + (right - left) * 0.75;
}
}  // namespace

SpectrumWorker::SpectrumWorker(QObject *parent)
    : QObject(parent) {
}

void SpectrumWorker::setDevice(HackrfDevice *dev) {
    device_ = dev;
}

void SpectrumWorker::setSweepPlan(const AnalysisSweepPlan &plan) {
    sweep_plan_ = plan;
    sweep_rf_base_ready_ = false;
}

void SpectrumWorker::setSweepEnabled(bool enabled) {
    sweep_enabled_ = enabled;
}

bool SpectrumWorker::ensureSweepBaseReady() {
    if (!device_ || sweep_plan_.segments.empty()) {
        return false;
    }

    const auto &seg0 = sweep_plan_.segments.front();
    hackrf_alloc_params params;
    params.center_freq = seg0.center_freq_hz;
    params.sample_rate = seg0.sample_rate_hz;
    params.bandwidth = seg0.bandwidth_hz;
    params.fft_size = sweep_plan_.fft_size_per_segment;
    params.lna_gain = ANALYSIS_DEFAULT_LNA_GAIN;
    params.vga_gain = ANALYSIS_DEFAULT_VGA_GAIN;

    if (sweep_rf_base_ready_ &&
        sameSweepRfBase(sweep_rf_base_params_, params)) {
        return true;
    }

    device_->stopRx();
    if (!device_->configure(params, false)) {
        sweep_rf_base_ready_ = false;
        return false;
    }
    if (!device_->startRx()) {
        sweep_rf_base_ready_ = false;
        return false;
    }

    sweep_rf_base_params_ = params;
    sweep_rf_base_ready_ = true;
    return true;
}

bool SpectrumWorker::captureSegmentSpectrum(
    std::vector<double> &segment_spectrum) {
    if (!device_) {
        return false;
    }

    const int fft_size = sweep_plan_.fft_size_per_segment;
    const size_t raw_needed = static_cast<size_t>(fft_size) * 2;

    if (!device_->waitForRawSamples(raw_needed,
                                    ANALYSIS_SWEEP_SETTLE_MS + 40)) {
        return false;
    }
    if (device_->getFftBackend() != FftBackend::FPGA) {
        (void)device_->getMagnitudeSpectrumFromLatest(true);

        if (!device_->waitForRawSamples(raw_needed, 40)) {
            return false;
        }
    }
    segment_spectrum = device_->getMagnitudeSpectrumFromLatest(true);
    if (segment_spectrum.size() != static_cast<size_t>(fft_size)) {
        return false;
    }
    suppressSegmentDcPeak(segment_spectrum, fft_size);
    return true;
}

void SpectrumWorker::computeSpectrum() {
    if (!device_) {
        return;
    }
    bool expected = false;
    if (!g_spectrum_worker_busy.compare_exchange_strong(expected, true)) {
        return;
    }

    std::vector<double> result;
    if (sweep_enabled_ && !sweep_plan_.segments.empty()) {
        if (!ensureSweepBaseReady()) {
            g_spectrum_worker_busy.store(false);
            return;
        }

        result.reserve(static_cast<size_t>(sweep_plan_.total_bins));

        for (size_t i = 0; i < sweep_plan_.segments.size(); ++i) {
            const auto &segment = sweep_plan_.segments[i];
            if (!device_->setCenterFrequency(segment.center_freq_hz)) {
                spdlog::warn("Sweep segment {} frequency retune failed", i);
                result.clear();
                break;
            }

            std::vector<double> segment_spectrum;
            if (!captureSegmentSpectrum(segment_spectrum)) {
                spdlog::warn("Sweep segment {} capture failed", i);
                result.clear();
                break;
            }

            result.insert(result.end(), segment_spectrum.begin(),
                          segment_spectrum.end());
        }

        if (result.size() !=
            static_cast<size_t>(sweep_plan_.total_bins)) {
            result.clear();
        }
    } else {
        auto iq_samples = device_->getIQSamplesForProcessing(false);
        if (!iq_samples.empty()) {
            result = device_->getMagnitudeSpectrumFromIQ(iq_samples);
        }
    }

    g_spectrum_worker_busy.store(false);

    if (!result.empty()) {
        emit spectrumReady(std::move(result));
    }
}
