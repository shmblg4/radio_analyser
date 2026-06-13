#ifndef FPGA_FFT_PROCESSOR_HPP
#define FPGA_FFT_PROCESSOR_HPP

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
    void close();

    static std::vector<uint8_t> makeTxFrame(
        const std::vector<std::complex<float>> &iq_samples,
        bool invert_imag = false);
    static std::vector<double> rxFrameToSpectrumDb(const uint8_t *rx_data,
                                                   size_t rx_size);
    static int correctedDisplayBinForFpgaBin(int fpga_bin);

private:
    bool ensureOpen(std::string &error);
    bool writeAll(const uint8_t *data, size_t size, std::string &error);
    bool readFrame(uint8_t *data, size_t size, size_t &bytes_read,
                   std::string &error);

    std::string serial_;
    void *handle_ = nullptr;
};

#endif
