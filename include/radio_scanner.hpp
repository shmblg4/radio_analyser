#ifndef RADIO_SCANNER_H
#define RADIO_SCANNER_H

#include <atomic>
#include <complex>
#include <ctime>
#include <hackrf.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <vector>

struct hackrf_alloc_params {
    uint64_t center_freq = 0;
    double sample_rate = 0;
    uint32_t bandwidth = 0;
    uint32_t vga_gain = 0;
    uint32_t lna_gain = 0;
    int fft_size = 0;  // Changed from uint16_t to int for safety
};

class HackrfDevice {
public:
    HackrfDevice();
    ~HackrfDevice();

    bool configure(const hackrf_alloc_params& alloc_params);

    bool startRx();
    bool stopRx();  // Changed to return bool for error checking

    std::vector<std::complex<float>> getIQSamplesForProcessing(
        bool remove_dc = true);
    std::vector<double> getMagnitudeSpectrum();
    std::vector<double> getMagnitudeSpectrumFromIQ(
        const std::vector<std::complex<float>> &iq_samples);

    const hackrf_alloc_params& getCurrentAllocParams() const {
        return alloc_params_;
    }

private:
    hackrf_device* device_ = nullptr;
    hackrf_alloc_params alloc_params_{};

    std::vector<int8_t> samples_buffer_;
    mutable std::mutex samples_mutex_;
    std::atomic<bool> running_{false};

    bool configureDevice();
    bool validateGains(int vga_gain, int lna_gain) const;
    static int rxCallback(hackrf_transfer* transfer);
    int handleRx(hackrf_transfer* transfer);

    std::vector<std::complex<float>>
    convertRawSamples(const std::vector<int8_t>& raw_samples) const;
    std::vector<double> calculateMagnitudeSpectrum(
        const std::vector<std::complex<float>>& iq_samples, int fft_size) const;
    void removeDCOffset(std::vector<std::complex<float>>& iq_samples);

    float dc_i_accumulator_ = 0.0f;
    float dc_q_accumulator_ = 0.0f;
    mutable std::mutex dc_mutex_;
};

#endif