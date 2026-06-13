#ifndef RADIO_SCANNER_H
#define RADIO_SCANNER_H

#include <atomic>
#include <complex>
#include <ctime>
#include <hackrf.h>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

class FpgaFftProcessor;

enum class FftBackend {
    FFTW3,
    FPGA
};

struct hackrf_alloc_params {
    uint64_t center_freq = 0;
    double sample_rate = 0;
    uint32_t bandwidth = 0;
    uint32_t vga_gain = 0;
    uint32_t lna_gain = 0;
    int fft_size = 0;
};

class HackrfDevice {
public:
    HackrfDevice();
    ~HackrfDevice();

    bool configure(const hackrf_alloc_params& alloc_params,
                   bool log_details = true);

    bool startRx();
    bool stopRx();
    bool setCenterFrequency(uint64_t center_freq_hz);
    bool waitForRawSamples(size_t raw_bytes_needed, int timeout_ms);

    std::vector<std::complex<float>> getIQSamplesForProcessing(
        bool remove_dc = true);
    std::vector<double> getMagnitudeSpectrum();
    std::vector<double> getMagnitudeSpectrumFromLatest(bool remove_dc = false);
    std::vector<double> getMagnitudeSpectrumFromIQ(
        const std::vector<std::complex<float>> &iq_samples);
    void setFftBackend(FftBackend backend);
    FftBackend getFftBackend() const;
    std::string getLastFftError() const;

    const hackrf_alloc_params& getCurrentAllocParams() const {
        return alloc_params_;
    }

private:
    hackrf_device* device_ = nullptr;
    hackrf_alloc_params alloc_params_{};

    std::vector<int8_t> samples_buffer_;
    mutable std::mutex samples_mutex_;
    std::mutex device_mutex_;
    std::atomic<bool> running_{false};

    bool configureDevice(bool log_details);
    bool validateGains(int vga_gain, int lna_gain) const;
    static int rxCallback(hackrf_transfer* transfer);
    int handleRx(hackrf_transfer* transfer);

    std::vector<std::complex<float>>
    convertRawSamples(const int8_t *raw_data, size_t num_iq_pairs) const;
    std::vector<double> calculateMagnitudeSpectrum(
        const std::vector<std::complex<float>>& iq_samples, int fft_size) const;
    std::vector<double> calculateFftwSpectrumDb(
        const std::vector<std::complex<float>>& iq_samples, int fft_size) const;
    void removeDCOffset(std::vector<std::complex<float>>& iq_samples);
    static void removeBlockDCOffset(std::vector<std::complex<float>>& iq_samples);
    void trimSamplesBufferLocked();

    size_t max_raw_buffer_bytes_ = 0;

    float dc_i_accumulator_ = 0.0f;
    float dc_q_accumulator_ = 0.0f;
    mutable std::mutex dc_mutex_;

    FftBackend fft_backend_ = FftBackend::FFTW3;
    std::unique_ptr<FpgaFftProcessor> fpga_fft_;
    mutable std::mutex fft_backend_mutex_;
    std::string last_fft_error_;
};

#endif
