#ifndef RADIO_SCANNER_H
#define RADIO_SCANNER_H

#include <atomic>
#include <complex>
#include <ctime>
#include <hackrf.h>
#include <iostream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

#include "colors.hpp"

#pragma pack(push, 1)
typedef struct hackrf_alloc_params {
    uint64_t center_freq = 0;
    double sample_rate = 0;
    uint32_t bandwidth = 0;
    uint32_t vga_gain = 0;
    uint32_t lna_gain = 0;
    uint16_t fft_size = 0;
} hackrf_alloc_params;
#pragma pack(pop)

class HackrfDevice {
public:
    HackrfDevice();
    ~HackrfDevice();

    bool configure(hackrf_alloc_params alloc_params);

    bool startRx();
    void stopRx();

    std::vector<std::complex<float>> getIQSamplesForProcessing();
    std::vector<double> getMagnitudeSpectrum();
    std::vector<double> getMagnitudeSpectrumFromIQ(
        const std::vector<std::complex<float>> &iq_samples);

    const hackrf_alloc_params &getCurrentAllocParams() const {
        return __alloc_params__;
    }

private:
    hackrf_device *__dev__ = nullptr;
    hackrf_alloc_params __alloc_params__{};

    std::vector<int8_t> __samples_buffer__;
    std::mutex __samples_mutex__;
    std::atomic<bool> __running__{false};

    bool _configure_device();
    std::vector<bool> _validate_gains(int vga_gain, int lna_gain);
    static int _rx_callback(hackrf_transfer *transfer);
    int _handle_rx(hackrf_transfer *transfer);

    std::vector<std::complex<float>>
    convertRawSamples(const std::vector<int8_t> &raw_samples);
    std::vector<double> calculateMagnitudeSpectrum(
        const std::vector<std::complex<float>> &iq_samples, int fft_size);
    void removeDCOffset(std::vector<std::complex<float>> &iq_samples);

    float __dc_i_accumulator__ = 0.0f;
    float __dc_q_accumulator__ = 0.0f;
    std::mutex __dc_mutex__;
};

#endif