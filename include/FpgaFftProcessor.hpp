#ifndef FPGA_FFT_PROCESSOR_HPP
#define FPGA_FFT_PROCESSOR_HPP

#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class FpgaFftProcessor {
public:
    static constexpr int kFftSize = 1024;
    static constexpr size_t kTxFrameBytes = kFftSize * 2;
    static constexpr size_t kRxFrameBytes = kFftSize * 2 * sizeof(int32_t);
    static constexpr const char *kDefaultSerial = "FT74ISENA";

    explicit FpgaFftProcessor(std::string serial = kDefaultSerial);
    ~FpgaFftProcessor();

    FpgaFftProcessor(const FpgaFftProcessor &) = delete;
    FpgaFftProcessor &operator=(const FpgaFftProcessor &) = delete;

    bool process(const std::vector<std::complex<float>> &iq_samples,
                 std::vector<double> &spectrum_db,
                 std::string &error);
    bool processFromRaw(const int8_t *raw_iq, size_t num_iq_pairs,
                        std::vector<double> &spectrum_db,
                        std::string &error);
    void close();

    static std::vector<uint8_t> makeTxFrame(
        const std::vector<std::complex<float>> &iq_samples,
        bool invert_imag = false);
    static std::vector<uint8_t> makeTxFrameFromRaw(const int8_t *raw_iq,
                                                   size_t num_iq_pairs,
                                                   bool invert_imag = false);
    static std::vector<double> rxFrameToSpectrumDb(const uint8_t *rx_data,
                                                   size_t rx_size);
    static int correctedDisplayBinForFpgaBin(int fpga_bin);
    static bool isCombGarbageSpectrum(const std::vector<double> &spectrum_db);
    static bool isSaturatedSpectrum(const std::vector<double> &spectrum_db);

private:
    bool ensureOpen(std::string &error);
    bool transferFrame(const std::vector<uint8_t> &tx,
                       std::vector<double> &spectrum_db, std::string &error);
    bool discardStaleRx(unsigned max_frames, unsigned &discarded,
                        std::string &error);
    bool waitForFreshRxAfterTx(
        const std::chrono::steady_clock::time_point &tx_time,
        unsigned timeout_ms, unsigned &rx_queued, long long &post_tx_ms,
        unsigned &early_discarded, std::string &error);
    bool waitForRxReady(size_t bytes_needed, unsigned timeout_ms,
                        unsigned &rx_queued, std::string &error);
    bool writeAll(const uint8_t *data, size_t size, std::string &error);
    bool readFrame(uint8_t *data, size_t size, size_t &bytes_read,
                   std::string &error);

    std::string serial_;
    void *handle_ = nullptr;
    bool warmed_up_ = false;
};

#endif
