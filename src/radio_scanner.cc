#include "radio_scanner.hpp"
#include "colors.hpp"
#include "FpgaFftProcessor.hpp"
#include <cmath>
#include <fftw3.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <unordered_map>

class FFTWBuffer {
public:
    explicit FFTWBuffer(int size) : size_(size), data_(nullptr) {
        if (size <= 0) {
            throw std::invalid_argument("FFTWBuffer: size must be positive");
        }
        data_ = static_cast<fftw_complex*>(
            fftw_malloc(sizeof(fftw_complex) * static_cast<size_t>(size)));
        if (!data_) {
            throw std::bad_alloc();
        }
    }
    
    ~FFTWBuffer() {
        if (data_) {
            fftw_free(data_);
        }
    }
    
    FFTWBuffer(const FFTWBuffer&) = delete;
    FFTWBuffer& operator=(const FFTWBuffer&) = delete;
    
    FFTWBuffer(FFTWBuffer&& other) noexcept : size_(other.size_), data_(other.data_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }
    
    FFTWBuffer& operator=(FFTWBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) {
                fftw_free(data_);
            }
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    fftw_complex* get() { return data_; }
    const fftw_complex* get() const { return data_; }
    int size() const { return size_; }
    
    fftw_complex& operator[](int i) { return data_[i]; }
    const fftw_complex& operator[](int i) const { return data_[i]; }
    
private:
    int size_;
    fftw_complex* data_;
};

class FFTWPlan {
public:
    FFTWPlan() : plan_(nullptr) {}
    
    FFTWPlan(int n, fftw_complex* in, fftw_complex* out, int sign, unsigned flags) {
        plan_ = fftw_plan_dft_1d(n, in, out, sign, flags);
        if (!plan_) {
            throw std::runtime_error("Failed to create FFTW plan");
        }
    }
    
    ~FFTWPlan() {
        if (plan_) {
            fftw_destroy_plan(plan_);
        }
    }
    
    FFTWPlan(const FFTWPlan&) = delete;
    FFTWPlan& operator=(const FFTWPlan&) = delete;
    
    FFTWPlan(FFTWPlan&& other) noexcept : plan_(other.plan_) {
        other.plan_ = nullptr;
    }
    
    FFTWPlan& operator=(FFTWPlan&& other) noexcept {
        if (this != &other) {
            if (plan_) {
                fftw_destroy_plan(plan_);
            }
            plan_ = other.plan_;
            other.plan_ = nullptr;
        }
        return *this;
    }
    
    void execute() {
        if (plan_) {
            fftw_execute(plan_);
        }
    }
    
    bool valid() const { return plan_ != nullptr; }
    
private:
    fftw_plan plan_;
};

class FFTWCache {
public:
    static FFTWCache& instance() {
        static FFTWCache cache;
        return cache;
    }
    
    struct FFTResources {
        fftw_complex* in;
        fftw_complex* out;
        fftw_plan plan;
    };
    
    FFTResources getResources(int fft_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_.find(fft_size);
        if (it != cache_.end()) {
            return {it->second.in, it->second.out, it->second.plan};
        }
        
        CachedEntry entry;
        entry.in = static_cast<fftw_complex*>(
            fftw_malloc(sizeof(fftw_complex) * static_cast<size_t>(fft_size)));
        entry.out = static_cast<fftw_complex*>(
            fftw_malloc(sizeof(fftw_complex) * static_cast<size_t>(fft_size)));
        
        if (!entry.in || !entry.out) {
            if (entry.in) fftw_free(entry.in);
            if (entry.out) fftw_free(entry.out);
            throw std::bad_alloc();
        }
        
        entry.plan = fftw_plan_dft_1d(fft_size, entry.in, entry.out, 
                                       FFTW_FORWARD, FFTW_ESTIMATE);
        if (!entry.plan) {
            fftw_free(entry.in);
            fftw_free(entry.out);
            throw std::runtime_error("Failed to create FFTW plan");
        }
        
        FFTResources result = {entry.in, entry.out, entry.plan};
        cache_[fft_size] = std::move(entry);
        return result;
    }
    
    ~FFTWCache() {
        for (auto& pair : cache_) {
            if (pair.second.plan) fftw_destroy_plan(pair.second.plan);
            if (pair.second.in) fftw_free(pair.second.in);
            if (pair.second.out) fftw_free(pair.second.out);
        }
    }
    
private:
    FFTWCache() = default;
    FFTWCache(const FFTWCache&) = delete;
    FFTWCache& operator=(const FFTWCache&) = delete;
    
    struct CachedEntry {
        fftw_complex* in = nullptr;
        fftw_complex* out = nullptr;
        fftw_plan plan = nullptr;
        
        CachedEntry() = default;
        CachedEntry(CachedEntry&& other) noexcept 
            : in(other.in), out(other.out), plan(other.plan) {
            other.in = nullptr;
            other.out = nullptr;
            other.plan = nullptr;
        }
        CachedEntry& operator=(CachedEntry&& other) noexcept {
            if (this != &other) {
                in = other.in;
                out = other.out;
                plan = other.plan;
                other.in = nullptr;
                other.out = nullptr;
                other.plan = nullptr;
            }
            return *this;
        }
    };
    
    std::mutex mutex_;
    std::unordered_map<int, CachedEntry> cache_;
};

std::vector<double>
performFFTAndGetMagnitude(const std::complex<float> *input, size_t input_count,
                          int fft_size) {
    if (fft_size <= 0) {
        return {};
    }

    if (input_count == 0) {
        return std::vector<double>(static_cast<size_t>(fft_size), 0.0);
    }

    const size_t use_count =
        std::min(input_count, static_cast<size_t>(fft_size));
    const size_t start =
        (input_count > static_cast<size_t>(fft_size))
            ? input_count - static_cast<size_t>(fft_size)
            : 0;

    auto resources = FFTWCache::instance().getResources(fft_size);

    const int sample_count = fft_size;
    if (sample_count > 1) {
        const double pi2 = 2.0 * M_PI;
        const double denom = static_cast<double>(sample_count - 1);
        for (int i = 0; i < sample_count; ++i) {
            const float window =
                static_cast<float>(0.5 * (1.0 - std::cos(pi2 * i / denom)));
            if (static_cast<size_t>(i) < use_count) {
                const auto &s = input[start + static_cast<size_t>(i)];
                resources.in[i][0] = static_cast<double>(s.real()) * window;
                resources.in[i][1] = static_cast<double>(s.imag()) * window;
            } else {
                resources.in[i][0] = 0.0;
                resources.in[i][1] = 0.0;
            }
        }
    } else if (use_count == 1) {
        resources.in[0][0] = static_cast<double>(input[start].real());
        resources.in[0][1] = static_cast<double>(input[start].imag());
    }

    fftw_execute(resources.plan);

    std::vector<double> magnitudes(static_cast<size_t>(fft_size));
    for (int i = 0; i < fft_size; ++i) {
        const double re = resources.out[i][0];
        const double im = resources.out[i][1];
        magnitudes[static_cast<size_t>(i)] = std::sqrt(re * re + im * im);
    }

    const int half_size = fft_size / 2;
    for (int i = 0; i < half_size; ++i) {
        std::swap(magnitudes[static_cast<size_t>(i)],
                  magnitudes[static_cast<size_t>(i + half_size)]);
    }

    return magnitudes;
}

std::vector<double>
performFFTAndGetMagnitude(const std::vector<std::complex<float>> &input,
                          int fft_size) {
    if (input.empty()) {
        return performFFTAndGetMagnitude(
            static_cast<const std::complex<float> *>(nullptr), 0, fft_size);
    }
    return performFFTAndGetMagnitude(input.data(), input.size(), fft_size);
}

HackrfDevice::HackrfDevice() {
    auto result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        throw std::runtime_error(
            std::string("HackRF init failed: ") + 
            hackrf_error_name(static_cast<hackrf_error>(result)));
    }

    result = hackrf_open(&device_);
    if (result != HACKRF_SUCCESS) {
        hackrf_exit();
        throw std::runtime_error(
            std::string("HackRF open failed: ") + 
            hackrf_error_name(static_cast<hackrf_error>(result)));
    }
    spdlog::info("HackRF device opened successfully");
}

HackrfDevice::~HackrfDevice() {
    if (running_.load()) {
        stopRx();
    }
    if (device_) {
        hackrf_close(device_);
    }
    hackrf_exit();
    spdlog::info("HackRF device closed");
}

bool HackrfDevice::configure(const hackrf_alloc_params& alloc_params,
                             bool log_details) {
    std::lock_guard<std::mutex> lock(device_mutex_);
    alloc_params_ = alloc_params;
    return configureDevice(log_details);
}

bool HackrfDevice::configureDevice(bool log_details) {
    if (!device_) {
        spdlog::error("Cannot configure: device is null");
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(dc_mutex_);
        dc_i_accumulator_ = 0.0f;
        dc_q_accumulator_ = 0.0f;
    }
    
    auto result = hackrf_set_freq(device_, alloc_params_.center_freq);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set frequency to {} Hz: {}",
                      alloc_params_.center_freq,
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }
    
    if (!validateGains(static_cast<int>(alloc_params_.vga_gain), 
                       static_cast<int>(alloc_params_.lna_gain))) {
        spdlog::error("Invalid gain values: VGA={}, LNA={}", 
                      alloc_params_.vga_gain, alloc_params_.lna_gain);
        return false;
    }
    
    result = hackrf_set_vga_gain(device_, alloc_params_.vga_gain);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set VGA gain to {}: {}",
                      alloc_params_.vga_gain,
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }
    
    result = hackrf_set_lna_gain(device_, alloc_params_.lna_gain);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set LNA gain to {}: {}",
                      alloc_params_.lna_gain,
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }
    
    result = hackrf_set_sample_rate(device_, alloc_params_.sample_rate);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set sample rate to {} Hz: {}",
                      alloc_params_.sample_rate,
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }
    
    uint32_t bw_hz = hackrf_compute_baseband_filter_bw(alloc_params_.bandwidth);
    result = hackrf_set_baseband_filter_bandwidth(device_, bw_hz);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set bandwidth to {} Hz: {}",
                      alloc_params_.bandwidth,
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }
    
    if (log_details) {
        spdlog::info("{}############ Device configured ############{}",
                     colors::GREEN, colors::RESET);
        spdlog::info("Center frequency: {} Hz", alloc_params_.center_freq);
        spdlog::info("Sample rate: {} Hz", alloc_params_.sample_rate);
        spdlog::info("Bandwidth: {} Hz", alloc_params_.bandwidth);
        spdlog::info("VGA gain: {}", alloc_params_.vga_gain);
        spdlog::info("LNA gain: {}", alloc_params_.lna_gain);
        spdlog::info("{}###########################################{}",
                     colors::GREEN, colors::RESET);
    }

    const int fft = std::max(1, alloc_params_.fft_size);
    const size_t fft_cap =
        static_cast<size_t>(fft) * 2 * sizeof(int8_t) * 4;
    // Keep at least ~100 ms of IQ for FM demodulation in detection mode.
    const size_t audio_cap = static_cast<size_t>(
        std::max(1.0, alloc_params_.sample_rate * 0.1)) *
        2 * sizeof(int8_t);
    max_raw_buffer_bytes_ = std::max(fft_cap, audio_cap);
    {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        trimSamplesBufferLocked();
    }
    return true;
}

bool HackrfDevice::validateGains(int vga_gain, int lna_gain) const {
    if (vga_gain < 0 || vga_gain > 62 || vga_gain % 2 != 0) {
        return false;
    }
    if (lna_gain < 0 || lna_gain > 40 || lna_gain % 8 != 0) {
        return false;
    }
    return true;
}

int HackrfDevice::rxCallback(hackrf_transfer* transfer) {
    HackrfDevice* instance = static_cast<HackrfDevice*>(transfer->rx_ctx);
    return instance->handleRx(transfer);
}

int HackrfDevice::handleRx(hackrf_transfer* transfer) {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    const int8_t* raw_data = reinterpret_cast<const int8_t*>(transfer->buffer);
    samples_buffer_.insert(samples_buffer_.end(), raw_data,
                           raw_data + transfer->valid_length);
    trimSamplesBufferLocked();
    return 0;
}

void HackrfDevice::trimSamplesBufferLocked() {
    if (max_raw_buffer_bytes_ == 0 ||
        samples_buffer_.size() <= max_raw_buffer_bytes_) {
        return;
    }
    samples_buffer_.erase(
        samples_buffer_.begin(),
        samples_buffer_.end() - static_cast<std::ptrdiff_t>(max_raw_buffer_bytes_));
}

bool HackrfDevice::setCenterFrequency(uint64_t center_freq_hz) {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (!device_) {
        spdlog::error("Cannot set frequency: device is null");
        return false;
    }

    const auto result = hackrf_set_freq(device_, center_freq_hz);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to set frequency to {} Hz: {}",
                      center_freq_hz,
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }

    alloc_params_.center_freq = center_freq_hz;
    {
        std::lock_guard<std::mutex> dc_lock(dc_mutex_);
        dc_i_accumulator_ = 0.0f;
        dc_q_accumulator_ = 0.0f;
    }
    {
        std::lock_guard<std::mutex> samples_lock(samples_mutex_);
        samples_buffer_.clear();
    }
    return true;
}

bool HackrfDevice::waitForRawSamples(size_t raw_bytes_needed, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(samples_mutex_);
            if (samples_buffer_.size() >= raw_bytes_needed) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

bool HackrfDevice::startRx() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (running_.load()) {
        spdlog::warn("RX already running");
        return false;
    }
    
    if (!device_) {
        spdlog::error("Cannot start RX: device is null");
        return false;
    }
    
    int result = hackrf_start_rx(device_, rxCallback, this);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to start RX: {}",
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        return false;
    }
    running_.store(true);
    spdlog::info("RX started");
    return true;
}

bool HackrfDevice::stopRx() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (!running_.load()) {
        spdlog::warn("RX already stopped");
        return true;
    }
    
    if (!device_) {
        spdlog::error("Cannot stop RX: device is null");
        return false;
    }
    
    int result = hackrf_stop_rx(device_);
    if (result != HACKRF_SUCCESS) {
        spdlog::error("Failed to stop RX: {}",
                      hackrf_error_name(static_cast<hackrf_error>(result)));
        running_.store(false);
        return false;
    }
    
    running_.store(false);
    spdlog::info("RX stopped");
    return true;
}

std::vector<std::complex<float>> HackrfDevice::getIQSamplesForProcessing(
    bool remove_dc) {
    std::vector<int8_t> local_buffer;
    {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        local_buffer = std::move(samples_buffer_);
        samples_buffer_.clear();
    }

    if (local_buffer.size() % 2 != 0) {
        local_buffer.pop_back();
    }
    if (local_buffer.empty()) {
        return {};
    }

    std::vector<std::complex<float>> iq_samples = convertRawSamples(
        local_buffer.data(), local_buffer.size() / 2);
    
    if (remove_dc && !iq_samples.empty()) {
        removeDCOffset(iq_samples);
    }
    return iq_samples;
}

std::vector<double> HackrfDevice::getMagnitudeSpectrumFromLatest(bool remove_dc) {
    const int fft_size = alloc_params_.fft_size;
    if (fft_size <= 0) {
        return {};
    }

    const size_t raw_needed = static_cast<size_t>(fft_size) * 2;
    std::vector<int8_t> tail;
    {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        if (samples_buffer_.size() < raw_needed) {
            return std::vector<double>(static_cast<size_t>(fft_size), -200.0);
        }
        tail.assign(samples_buffer_.end() - static_cast<std::ptrdiff_t>(raw_needed),
                    samples_buffer_.end());
        samples_buffer_.clear();
    }

    auto iq_samples = convertRawSamples(tail.data(), static_cast<size_t>(fft_size));
    if (remove_dc && !iq_samples.empty()) {
        removeBlockDCOffset(iq_samples);
    }
    return getMagnitudeSpectrumFromIQ(iq_samples);
}

std::vector<double> HackrfDevice::getMagnitudeSpectrum() {
    auto iq_samples = getIQSamplesForProcessing();
    return getMagnitudeSpectrumFromIQ(iq_samples);
}

std::vector<double> HackrfDevice::getMagnitudeSpectrumFromIQ(
    const std::vector<std::complex<float>>& iq_samples) {
    const int fft_size = alloc_params_.fft_size;
    
    if (fft_size <= 0) {
        return {};
    }
    
    if (iq_samples.empty()) {
        return std::vector<double>(static_cast<size_t>(fft_size), -200.0);
    }

    {
        std::lock_guard<std::mutex> lock(fft_backend_mutex_);
        if (fft_backend_ == FftBackend::FPGA) {
            if (fft_size == FpgaFftProcessor::kFftSize) {
                if (!fpga_fft_) {
                    fpga_fft_ = std::make_unique<FpgaFftProcessor>();
                }

                std::vector<double> fpga_spectrum_db;
                std::string error;
                if (fpga_fft_->process(iq_samples, fpga_spectrum_db, error)) {
                    last_fft_error_.clear();
                    return fpga_spectrum_db;
                }

                if (error != last_fft_error_) {
                    spdlog::warn("FPGA FFT failed, falling back to FFTW3: {}",
                                 error);
                    last_fft_error_ = error;
                }
            } else {
                const std::string error =
                    "FPGA FFT supports only 1024 bins, current fft_size=" +
                    std::to_string(fft_size);
                if (error != last_fft_error_) {
                    spdlog::warn("FPGA FFT unavailable, falling back to FFTW3: {}",
                                 error);
                    last_fft_error_ = error;
                }
            }
        }
    }

    return calculateFftwSpectrumDb(iq_samples, fft_size);
}

void HackrfDevice::setFftBackend(FftBackend backend) {
    std::lock_guard<std::mutex> lock(fft_backend_mutex_);
    if (fft_backend_ == backend) {
        return;
    }
    fft_backend_ = backend;
    last_fft_error_.clear();
}

FftBackend HackrfDevice::getFftBackend() const {
    std::lock_guard<std::mutex> lock(fft_backend_mutex_);
    return fft_backend_;
}

std::string HackrfDevice::getLastFftError() const {
    std::lock_guard<std::mutex> lock(fft_backend_mutex_);
    return last_fft_error_;
}

std::vector<double> HackrfDevice::calculateFftwSpectrumDb(
    const std::vector<std::complex<float>>& iq_samples, int fft_size) const {
    std::vector<double> magnitudes = calculateMagnitudeSpectrum(iq_samples, fft_size);
    
    if (magnitudes.empty()) {
        return std::vector<double>(static_cast<size_t>(fft_size), -200.0);
    }

    const double fft_norm = static_cast<double>(std::max(1, fft_size));
    const double hann_coherent_gain = 0.5;
    const double full_scale = fft_norm * hann_coherent_gain;
    
    std::vector<double> spectrum_db(static_cast<size_t>(fft_size));
    for (int i = 0; i < fft_size; ++i) {
        double magnitude = magnitudes[static_cast<size_t>(i)] / full_scale;
        spectrum_db[static_cast<size_t>(i)] = 20.0 * std::log10(magnitude + 1e-12);
    }
    return spectrum_db;
}

std::vector<std::complex<float>>
HackrfDevice::convertRawSamples(const int8_t *raw_data,
                                size_t num_iq_pairs) const {
    std::vector<std::complex<float>> iq_samples;
    iq_samples.reserve(num_iq_pairs);

    for (size_t i = 0; i < num_iq_pairs; ++i) {
        const float i_val = static_cast<float>(raw_data[2 * i]) / 128.0f;
        const float q_val = static_cast<float>(raw_data[2 * i + 1]) / 128.0f;
        iq_samples.emplace_back(i_val, q_val);
    }
    return iq_samples;
}

std::vector<double> HackrfDevice::calculateMagnitudeSpectrum(
    const std::vector<std::complex<float>>& iq_samples, int fft_size) const {
    return performFFTAndGetMagnitude(iq_samples, fft_size);
}

void HackrfDevice::removeBlockDCOffset(
    std::vector<std::complex<float>> &iq_samples) {
    if (iq_samples.empty()) {
        return;
    }

    std::complex<float> mean(0.0f, 0.0f);
    for (const auto &sample : iq_samples) {
        mean += sample;
    }
    mean /= static_cast<float>(iq_samples.size());

    for (auto &sample : iq_samples) {
        sample -= mean;
    }
}

void HackrfDevice::removeDCOffset(std::vector<std::complex<float>>& iq_samples) {
    if (iq_samples.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(dc_mutex_);
    
    const float dc_alpha = 0.995f;
    const float dc_one_minus_alpha = 1.0f - dc_alpha;
    
    for (auto& s : iq_samples) {
        float i_val = s.real();
        float q_val = s.imag();
        
        dc_i_accumulator_ = dc_alpha * dc_i_accumulator_ + dc_one_minus_alpha * i_val;
        dc_q_accumulator_ = dc_alpha * dc_q_accumulator_ + dc_one_minus_alpha * q_val;
        
        s = std::complex<float>(i_val - dc_i_accumulator_,
                                q_val - dc_q_accumulator_);
    }
}
