#include "radio_scanner.hpp"
#include <cmath>
#include <fftw3.h>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

std::vector<double>
performFFTAndGetMagnitude(const std::vector<std::complex<float>> &input,
                          int fft_size) {
    if (input.empty()) {
        return std::vector<double>(fft_size, 0.0);
    }

    std::vector<std::complex<float>> samples = input;
    if (static_cast<int>(samples.size()) > fft_size) {
        samples.resize(fft_size);
    } else if (static_cast<int>(samples.size()) < fft_size) {
        samples.resize(fft_size, std::complex<float>(0.0f, 0.0f));
    }

    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        float window =
            0.5f * (1.0f - std::cos(2.0f * M_PI * i / (samples.size() - 1)));
        samples[i] *= window;
    }

    fftw_complex *in, *out;
    fftw_plan p;

    in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fft_size);
    out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fft_size);

    for (int i = 0; i < fft_size; ++i) {
        in[i][0] = samples[i].real();
        in[i][1] = samples[i].imag();
    }
    
    p = fftw_plan_dft_1d(fft_size, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    fftw_execute(p);

    std::vector<double> magnitudes(fft_size);
    for (int i = 0; i < fft_size; ++i) {
        magnitudes[i] =
            std::sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
    }

    int half_size = fft_size / 2;
    for (int i = 0; i < half_size; ++i) {
        std::swap(magnitudes[i], magnitudes[i + half_size]);
    }

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);

    return magnitudes;
}

HackrfDevice::HackrfDevice() {
    auto result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        throw std::runtime_error("Init error");
    }

    result = hackrf_open(&__dev__);
    if (result != HACKRF_SUCCESS) {
        throw std::runtime_error("Device open error");
    }
    spdlog::info("Device opened");
}

HackrfDevice::~HackrfDevice() {
    if (__running__.load()) {
        stopRx();
    }
    hackrf_close(__dev__);
    hackrf_exit();
    spdlog::info("Device closed");
}

bool HackrfDevice::configure(hackrf_alloc_params alloc_params) {
   __alloc_params__ = alloc_params;
    return _configure_device();
}

bool HackrfDevice::_configure_device() {
    {
        std::lock_guard<std::mutex> lock(__dc_mutex__);
        __dc_i_accumulator__ = 0.0f;
        __dc_q_accumulator__ = 0.0f;
    }
    auto result = hackrf_set_freq(__dev__, __alloc_params__.center_freq);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set frequency: {}",
                      hackrf_error_name((hackrf_error)result));
        return false;
    }
    result = hackrf_set_vga_gain(__dev__, __alloc_params__.vga_gain);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set VGA gain: {}",
                      hackrf_error_name((hackrf_error)result));
        return false;
    }
    result = hackrf_set_lna_gain(__dev__, __alloc_params__.lna_gain);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set LNA gain: {}",
                      hackrf_error_name((hackrf_error)result));
        return false;
    }
    result = hackrf_set_sample_rate(__dev__, __alloc_params__.sample_rate);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set sample rate: {}",
                      hackrf_error_name((hackrf_error)result));
        return false;
    }
    uint32_t bw_hz = hackrf_compute_baseband_filter_bw(__alloc_params__.bandwidth);
    result = hackrf_set_baseband_filter_bandwidth(__dev__, bw_hz);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set bandwidth: {}",
                      hackrf_error_name((hackrf_error)result));
        return false;
    }
    spdlog::info("{}############ Device configured ############{}",
                 colors::GREEN, colors::RESET);
    spdlog::info("Center frequency: {} Hz", __alloc_params__.center_freq);
    spdlog::info("Sample rate: {} Hz", __alloc_params__.sample_rate);
    spdlog::info("Bandwidth: {} Hz", __alloc_params__.bandwidth);
    spdlog::info("VGA gain: {}", __alloc_params__.vga_gain);
    spdlog::info("LNA gain: {}", __alloc_params__.lna_gain);
    spdlog::info("{}###########################################{}",
                 colors::GREEN, colors::RESET);
    return true;
}

std::vector<bool> HackrfDevice::_validate_gains(int vga_gain, int lna_gain) {
    std::vector<bool> isvalid = {true, true};
    if (vga_gain < 0 || vga_gain > 62 || vga_gain % 2 != 0) {
        isvalid[0] = false;
    }
    if (lna_gain < 0 || lna_gain > 40 || lna_gain % 2 != 0) {
        isvalid[1] = false;
    }
    return isvalid;
}

int HackrfDevice::_rx_callback(hackrf_transfer *transfer) {
    HackrfDevice *instance = reinterpret_cast<HackrfDevice *>(transfer->rx_ctx);
    return instance->_handle_rx(transfer);
}

int HackrfDevice::_handle_rx(hackrf_transfer *transfer) {
    std::lock_guard<std::mutex> lock(__samples_mutex__);
    const int8_t *raw_data = reinterpret_cast<const int8_t *>(transfer->buffer);
    __samples_buffer__.insert(__samples_buffer__.end(), raw_data,
                              raw_data + transfer->valid_length);
    return 0;
}

bool HackrfDevice::startRx() {
    if (__running__.load()) {
        spdlog::warn("RX already running");
        return false;
    }
    int result = hackrf_start_rx(__dev__, _rx_callback, this);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to start RX: {}",
                      hackrf_error_name((hackrf_error)result));
        return false;
    }
    __running__.store(true);
    spdlog::info("RX started");
    return true;
}

void HackrfDevice::stopRx() {
    if (!__running__.load()) {
        spdlog::warn("RX already stopped");
        return;
    }
    hackrf_stop_rx(__dev__);
    __running__.store(false);
    spdlog::info("RX stopped");
}

std::vector<std::complex<float>> HackrfDevice::getIQSamplesForProcessing() {
    std::lock_guard<std::mutex> lock(__samples_mutex__);
    std::vector<std::complex<float>> iq_samples =
        convertRawSamples(__samples_buffer__);
    __samples_buffer__.clear();
    removeDCOffset(iq_samples);
    return iq_samples;
}

std::vector<double> HackrfDevice::getMagnitudeSpectrum() {
    auto iq_samples = getIQSamplesForProcessing();
    return getMagnitudeSpectrumFromIQ(iq_samples);
}

std::vector<double> HackrfDevice::getMagnitudeSpectrumFromIQ(
    const std::vector<std::complex<float>> &iq_samples) {
    if (iq_samples.empty()) {
        return std::vector<double>(__alloc_params__.fft_size, -200.0);
    }
    std::vector<double> magnitudes =
        calculateMagnitudeSpectrum(iq_samples, __alloc_params__.fft_size);

    std::vector<double> spectrum_db(__alloc_params__.fft_size);
    for (int i = 0; i < __alloc_params__.fft_size; ++i) {
        double magnitude = magnitudes[i];
        spectrum_db[i] = 20.0 * std::log10(magnitude + 1e-10);
    }
    return spectrum_db;
}

std::vector<std::complex<float>>
HackrfDevice::convertRawSamples(const std::vector<int8_t> &raw_samples) {
    std::vector<std::complex<float>> iq_samples;
    if (raw_samples.size() % 2 != 0) {
        spdlog::warn("Raw samples size is odd, dropping last sample.");
    }
    size_t num_samples = raw_samples.size() / 2;
    iq_samples.reserve(num_samples);

    for (size_t i = 0; i < num_samples; ++i) {
        float i_val = static_cast<float>(raw_samples[2 * i]) /
                      128.0f;
        float q_val = static_cast<float>(raw_samples[2 * i + 1]) /
                      128.0f;
        iq_samples.emplace_back(i_val, q_val);
    }
    return iq_samples;
}

std::vector<double> HackrfDevice::calculateMagnitudeSpectrum(
    const std::vector<std::complex<float>> &iq_samples, int fft_size) {
    return performFFTAndGetMagnitude(iq_samples, fft_size);
}

void HackrfDevice::removeDCOffset(std::vector<std::complex<float>> &iq_samples) {
    if (iq_samples.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(__dc_mutex__);
    const float dc_alpha = 0.995f;
    const float dc_one_minus_alpha = 1.0f - dc_alpha;
    for (auto &s : iq_samples) {
        float i_val = s.real();
        float q_val = s.imag();
        __dc_i_accumulator__ =
            dc_alpha * __dc_i_accumulator__ + dc_one_minus_alpha * i_val;
        __dc_q_accumulator__ =
            dc_alpha * __dc_q_accumulator__ + dc_one_minus_alpha * q_val;
        s = std::complex<float>(i_val - __dc_i_accumulator__,
                                q_val - __dc_q_accumulator__);
    }
}