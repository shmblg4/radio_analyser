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

class HackrfDevice {
public:
    HackrfDevice();
    ~HackrfDevice();

    bool configure(uint64_t center_freq, double sample_rate, uint32_t bandwidth,
                   uint32_t vga_gain, uint32_t lna_gain);

    bool startRx();
    void stopRx();

    std::vector<std::complex<float>> getIQSamplesForProcessing();
    std::vector<double> getMagnitudeSpectrum(int fft_size = 512);

private:
    hackrf_device *__dev__ = nullptr;
    uint64_t __center_freq__ = 0;
    double __sample_rate__ = 0;
    uint32_t __bandwidth__ = 0;
    uint32_t __vga_gain__ = 0;
    uint32_t __lna_gain__ = 0;

    std::vector<int8_t>
        __samples_buffer__;
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
};

#endif