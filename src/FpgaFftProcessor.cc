#include "FpgaFftProcessor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#ifdef HAVE_FTDI_D2XX
#include <ftd2xx.h>
#endif

namespace {
constexpr size_t kBytesPerFftBin = 2U * sizeof(int32_t);
constexpr size_t kMinUsefulRxBytes = FpgaFftProcessor::kRxFrameBytes -
                                     (kBytesPerFftBin - 1U);
constexpr size_t kMinNonzeroRxBins = 32;
constexpr unsigned kRxResponseTimeoutMs = 400;
constexpr int kMaxTxAttempts = 4;
constexpr unsigned kMaxStaleRxDiscards = 16;
constexpr double kHannCoherentGain = 0.5;
constexpr double kInputFullScale = 128.0;

double hannWindow(size_t i) {
    if (FpgaFftProcessor::kFftSize <= 1) {
        return 1.0;
    }
    constexpr double pi = 3.14159265358979323846;
    const double phase =
        2.0 * pi * static_cast<double>(i) /
        static_cast<double>(FpgaFftProcessor::kFftSize - 1);
    return 0.5 * (1.0 - std::cos(phase));
}

int32_t readLeI32(const uint8_t *p) {
    const uint32_t v = static_cast<uint32_t>(p[0]) |
                       (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) |
                       (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(v);
}

struct RxFrameStats {
    int64_t max_abs = 0;
    int64_t nonzero_bins = 0;
};

RxFrameStats measureRxFrame(const uint8_t *rx_data, size_t bytes_read) {
    RxFrameStats stats;
    if (!rx_data || bytes_read < kBytesPerFftBin) {
        return stats;
    }
    const size_t complete_bins = bytes_read / kBytesPerFftBin;
    for (size_t bin = 0; bin < complete_bins; ++bin) {
        const int32_t re = readLeI32(rx_data + bin * kBytesPerFftBin);
        const int32_t im =
            readLeI32(rx_data + bin * kBytesPerFftBin + sizeof(int32_t));
        stats.max_abs = std::max(stats.max_abs,
                                 static_cast<int64_t>(std::max(std::abs(re),
                                                               std::abs(im))));
        if (re != 0 || im != 0) {
            ++stats.nonzero_bins;
        }
    }
    return stats;
}

std::string rxBytesHex(const uint8_t *rx_data, size_t bytes_read,
                       size_t max_bytes) {
    if (!rx_data || bytes_read == 0) {
        return "";
    }
    std::ostringstream out;
    out << std::hex;
    const size_t n = std::min(bytes_read, max_bytes);
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << static_cast<unsigned>(rx_data[i]);
    }
    return out.str();
}

bool shouldLogDebugFrame() {
    static bool consumed = false;
    const char *env = std::getenv("FPGA_FFT_DEBUG_FRAME");
    if (consumed || !env || *env == '\0' || *env == '0') {
        return false;
    }
    consumed = true;
    return true;
}

int8_t clampToInt8(float value) {
    const float scaled = std::round(value * 127.0f);
    const float clamped = std::clamp(
        scaled, static_cast<float>(std::numeric_limits<int8_t>::min()),
        static_cast<float>(std::numeric_limits<int8_t>::max()));
    return static_cast<int8_t>(clamped);
}

int8_t clampInt32ToInt8(int value) {
    const int clamped = std::clamp(value,
                                   static_cast<int>(std::numeric_limits<int8_t>::min()),
                                   static_cast<int>(std::numeric_limits<int8_t>::max()));
    return static_cast<int8_t>(clamped);
}

#ifdef HAVE_FTDI_D2XX
std::string ftStatusName(FT_STATUS status) {
    switch (status) {
    case FT_OK:
        return "FT_OK";
    case FT_INVALID_HANDLE:
        return "FT_INVALID_HANDLE";
    case FT_DEVICE_NOT_FOUND:
        return "FT_DEVICE_NOT_FOUND";
    case FT_DEVICE_NOT_OPENED:
        return "FT_DEVICE_NOT_OPENED";
    case FT_IO_ERROR:
        return "FT_IO_ERROR";
    case FT_INSUFFICIENT_RESOURCES:
        return "FT_INSUFFICIENT_RESOURCES";
    case FT_INVALID_PARAMETER:
        return "FT_INVALID_PARAMETER";
    case FT_INVALID_BAUD_RATE:
        return "FT_INVALID_BAUD_RATE";
    case FT_DEVICE_NOT_OPENED_FOR_ERASE:
        return "FT_DEVICE_NOT_OPENED_FOR_ERASE";
    case FT_DEVICE_NOT_OPENED_FOR_WRITE:
        return "FT_DEVICE_NOT_OPENED_FOR_WRITE";
    case FT_FAILED_TO_WRITE_DEVICE:
        return "FT_FAILED_TO_WRITE_DEVICE";
    case FT_EEPROM_READ_FAILED:
        return "FT_EEPROM_READ_FAILED";
    case FT_EEPROM_WRITE_FAILED:
        return "FT_EEPROM_WRITE_FAILED";
    case FT_EEPROM_ERASE_FAILED:
        return "FT_EEPROM_ERASE_FAILED";
    case FT_EEPROM_NOT_PRESENT:
        return "FT_EEPROM_NOT_PRESENT";
    case FT_EEPROM_NOT_PROGRAMMED:
        return "FT_EEPROM_NOT_PROGRAMMED";
    case FT_INVALID_ARGS:
        return "FT_INVALID_ARGS";
    case FT_NOT_SUPPORTED:
        return "FT_NOT_SUPPORTED";
    case FT_OTHER_ERROR:
        return "FT_OTHER_ERROR";
    case FT_DEVICE_LIST_NOT_READY:
        return "FT_DEVICE_LIST_NOT_READY";
    default:
        return "FT_STATUS(" + std::to_string(static_cast<int>(status)) + ")";
    }
}

std::string d2xxDeviceListSummary() {
    DWORD device_count = 0;
    FT_STATUS status = FT_CreateDeviceInfoList(&device_count);
    if (status != FT_OK) {
        return "D2XX device list failed: " + ftStatusName(status);
    }
    if (device_count == 0) {
        return "D2XX sees 0 devices";
    }

    std::ostringstream out;
    out << "D2XX sees " << device_count << " device(s):";
    for (DWORD i = 0; i < device_count; ++i) {
        DWORD flags = 0;
        DWORD type = 0;
        DWORD id = 0;
        DWORD loc_id = 0;
        char serial[16] = {};
        char description[64] = {};
        FT_HANDLE handle = nullptr;
        status = FT_GetDeviceInfoDetail(i, &flags, &type, &id, &loc_id,
                                        serial, description, &handle);
        if (status == FT_OK) {
            out << " [" << i << "] serial='" << serial << "' desc='"
                << description << "' loc=0x" << std::hex << loc_id
                << std::dec << " flags=0x" << std::hex << flags << std::dec;
        } else {
            out << " [" << i << "] detail failed: " << ftStatusName(status);
        }
    }
    return out.str();
}

DWORD d2xxDeviceCount() {
    DWORD device_count = 0;
    if (FT_CreateDeviceInfoList(&device_count) != FT_OK) {
        return 0;
    }
    return device_count;
}

int preferredDeviceIndex() {
    const char *env = std::getenv("FPGA_FTDI_INDEX");
    if (!env || *env == '\0') {
        return 0;
    }
    char *end = nullptr;
    const long index = std::strtol(env, &end, 10);
    if (end == env || index < 0 || index > 255) {
        return 0;
    }
    return static_cast<int>(index);
}
#endif

}  // namespace

FpgaFftProcessor::FpgaFftProcessor(std::string serial)
    : serial_(std::move(serial)) {}

FpgaFftProcessor::~FpgaFftProcessor() {
    close();
}

bool FpgaFftProcessor::process(
    const std::vector<std::complex<float>> &iq_samples,
    std::vector<double> &spectrum_db,
    std::string &error) {
    spectrum_db.clear();
    if (iq_samples.size() < static_cast<size_t>(kFftSize)) {
        error = "FPGA FFT needs 1024 complex IQ samples";
        return false;
    }

    const std::vector<uint8_t> tx = makeTxFrame(iq_samples, false);
    return transferFrame(tx, spectrum_db, error);
}

bool FpgaFftProcessor::processFromRaw(const int8_t *raw_iq, size_t num_iq_pairs,
                                      std::vector<double> &spectrum_db,
                                      std::string &error) {
    spectrum_db.clear();
    if (!raw_iq || num_iq_pairs < static_cast<size_t>(kFftSize)) {
        error = "FPGA FFT needs 1024 raw IQ pairs";
        return false;
    }

    const std::vector<uint8_t> tx = makeTxFrameFromRaw(raw_iq, num_iq_pairs, false);
    return transferFrame(tx, spectrum_db, error);
}

bool FpgaFftProcessor::transferFrame(const std::vector<uint8_t> &tx,
                                     std::vector<double> &spectrum_db,
                                     std::string &error) {
    spectrum_db.clear();
    if (tx.size() != kTxFrameBytes) {
        error = "FPGA TX frame size mismatch";
        return false;
    }

    if (!ensureOpen(error)) {
        return false;
    }

    std::vector<uint8_t> rx(kRxFrameBytes);
    size_t bytes_read = 0;
    bool frame_valid = false;
    std::string last_attempt_error;

    for (int attempt = 0; attempt < kMaxTxAttempts && !frame_valid; ++attempt) {
        bytes_read = 0;
        last_attempt_error.clear();

#ifdef HAVE_FTDI_D2XX
        FT_Purge(static_cast<FT_HANDLE>(handle_), FT_PURGE_RX);
        unsigned stale_discarded = 0;
        if (!discardStaleRx(kMaxStaleRxDiscards, stale_discarded,
                            last_attempt_error)) {
            error = last_attempt_error;
            return false;
        }
#endif

        if (!writeAll(tx.data(), tx.size(), last_attempt_error)) {
            error = last_attempt_error;
            close();
            return false;
        }

        const auto tx_time = std::chrono::steady_clock::now();
        unsigned early_discarded = 0;
        long long post_tx_ms = 0;
        unsigned rx_queued = 0;
        if (!waitForFreshRxAfterTx(tx_time, kRxResponseTimeoutMs, rx_queued,
                                   post_tx_ms, early_discarded,
                                   last_attempt_error)) {
            continue;
        }

        bytes_read = 0;
        const bool read_complete =
            readFrame(rx.data(), rx.size(), bytes_read, last_attempt_error);
        if (!read_complete && bytes_read < kBytesPerFftBin) {
            continue;
        }

        const RxFrameStats stats = measureRxFrame(rx.data(), bytes_read);
        const int64_t rx_nonzero_bins = stats.nonzero_bins;

        if (rx_nonzero_bins < static_cast<int64_t>(kMinNonzeroRxBins)) {
            last_attempt_error = "FPGA RX frame had too few nonzero bins (" +
                                 std::to_string(rx_nonzero_bins) + ")";
            continue;
        }

        std::vector<double> trial_spectrum =
            rxFrameToSpectrumDb(rx.data(), bytes_read);
        if (isCombGarbageSpectrum(trial_spectrum)) {
            last_attempt_error =
                "FPGA RX frame looked like stale comb noise";
            continue;
        }

        spectrum_db = std::move(trial_spectrum);
        if (shouldLogDebugFrame()) {
            double min_db = 1e9;
            double max_db = -1e9;
            double sum_db = 0.0;
            int max_bin = 0;
            int floor_bins = 0;
            for (int i = 0; i < static_cast<int>(spectrum_db.size()); ++i) {
                const double v = spectrum_db[static_cast<size_t>(i)];
                min_db = std::min(min_db, v);
                sum_db += v;
                if (v > max_db) {
                    max_db = v;
                    max_bin = i;
                }
                if (v <= -239.0) {
                    ++floor_bins;
                }
            }

            std::ostringstream first_bins;
            const size_t complete_bins = bytes_read / kBytesPerFftBin;
            const size_t preview_bins = std::min<size_t>(complete_bins, 8);
            for (size_t bin = 0; bin < preview_bins; ++bin) {
                const size_t offset = bin * kBytesPerFftBin;
                const int32_t re = readLeI32(rx.data() + offset);
                const int32_t im =
                    readLeI32(rx.data() + offset + sizeof(int32_t));
                const double re_d = static_cast<double>(re);
                const double im_d = static_cast<double>(im);
                const double power = re_d * re_d + im_d * im_d;
                const double db = 10.0 * std::log10(power + 1.0);
                if (bin > 0) {
                    first_bins << " | ";
                }
                first_bins << bin << ":re=" << re << ",im=" << im
                           << ",p=" << power << ",db=" << db;
            }

            spdlog::info(
                "FPGA frame debug: tx_size={} rx_size={} complete_bins={} "
                "first16_rx='{}' first_bins='{}' zero_bins={} floor_bins={} "
                "max_bin={} max_db={} min_db={} avg_db={}",
                tx.size(), bytes_read, complete_bins,
                rxBytesHex(rx.data(), bytes_read, 16), first_bins.str(),
                FpgaFftProcessor::kFftSize - rx_nonzero_bins, floor_bins,
                max_bin, max_db, min_db,
                sum_db / static_cast<double>(std::max<size_t>(1, spectrum_db.size())));
        }
        frame_valid = true;
        warmed_up_ = true;
    }

    if (!frame_valid) {
        error = last_attempt_error.empty()
                    ? "FPGA RX frame was not valid"
                    : last_attempt_error;
        return false;
    }

    return spectrum_db.size() == static_cast<size_t>(kFftSize);
}

bool FpgaFftProcessor::isCombGarbageSpectrum(
    const std::vector<double> &spectrum_db) {
    int hot = 0;
    int max_bin = 0;
    double max_db = -1e9;
    double min_hot = 1e9;
    double sum_hot = 0.0;
    for (int i = 0; i < static_cast<int>(spectrum_db.size()); ++i) {
        const double value = spectrum_db[static_cast<size_t>(i)];
        if (value > max_db) {
            max_db = value;
            max_bin = i;
        }
        if (value > -28.0) {
            ++hot;
            min_hot = std::min(min_hot, value);
            sum_hot += value;
        }
    }
    if (hot < 80) {
        return false;
    }

    const double avg_hot = sum_hot / static_cast<double>(hot);
    const double spread = max_db - min_hot;
    const double peak_above_avg = max_db - avg_hot;
    if (spread < 8.0 && hot > 100) {
        return true;
    }
    if (max_bin == 0 && hot > 120 && max_db > -35.0 && peak_above_avg < 6.0) {
        return true;
    }
    return false;
}

bool FpgaFftProcessor::waitForFreshRxAfterTx(
    const std::chrono::steady_clock::time_point &tx_time,
    unsigned timeout_ms, unsigned &rx_queued, long long &post_tx_ms,
    unsigned &early_discarded, std::string &error) {
#ifdef HAVE_FTDI_D2XX
    early_discarded = 0;
    rx_queued = 0;
    post_tx_ms = 0;
    const auto deadline =
        tx_time + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        post_tx_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - tx_time)
                         .count();
        DWORD queued = 0;
        const FT_STATUS status =
            FT_GetQueueStatus(static_cast<FT_HANDLE>(handle_), &queued);
        if (status != FT_OK) {
            error = "FTDI queue status failed: " + ftStatusName(status);
            return false;
        }
        rx_queued = static_cast<unsigned>(queued);
        if (queued >= static_cast<DWORD>(kMinUsefulRxBytes)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    error = "FTDI RX not ready: queued " + std::to_string(rx_queued) + "/" +
            std::to_string(kRxFrameBytes) + " bytes";
    return false;
#else
    (void)tx_time;
    (void)timeout_ms;
    rx_queued = 0;
    post_tx_ms = 0;
    early_discarded = 0;
    error = "FTDI D2XX support is not compiled in";
    return false;
#endif
}

bool FpgaFftProcessor::discardStaleRx(unsigned max_frames, unsigned &discarded,
                                      std::string &error) {
#ifdef HAVE_FTDI_D2XX
    discarded = 0;
    std::vector<uint8_t> trash(kRxFrameBytes);
    for (unsigned i = 0; i < max_frames; ++i) {
        DWORD queued = 0;
        const FT_STATUS status =
            FT_GetQueueStatus(static_cast<FT_HANDLE>(handle_), &queued);
        if (status != FT_OK) {
            error = "FTDI queue status failed: " + ftStatusName(status);
            return false;
        }
        if (queued < static_cast<DWORD>(kRxFrameBytes)) {
            break;
        }

        size_t bytes_read = 0;
        if (!readFrame(trash.data(), trash.size(), bytes_read, error)) {
            return false;
        }
        ++discarded;
    }
    return true;
#else
    (void)max_frames;
    discarded = 0;
    error = "FTDI D2XX support is not compiled in";
    return false;
#endif
}

void FpgaFftProcessor::close() {
#ifdef HAVE_FTDI_D2XX
    if (handle_) {
        FT_Close(static_cast<FT_HANDLE>(handle_));
        handle_ = nullptr;
    }
#else
    handle_ = nullptr;
#endif
    warmed_up_ = false;
}

std::vector<uint8_t> FpgaFftProcessor::makeTxFrame(
    const std::vector<std::complex<float>> &iq_samples,
    bool invert_imag) {
    std::vector<uint8_t> tx(kTxFrameBytes, 0);
    const size_t start =
        iq_samples.size() > static_cast<size_t>(kFftSize)
            ? iq_samples.size() - static_cast<size_t>(kFftSize)
            : 0;
    const size_t count = std::min(iq_samples.size(), static_cast<size_t>(kFftSize));

    for (size_t i = 0; i < count; ++i) {
        const auto &s = iq_samples[start + i];
        const double window = hannWindow(i);
        const int8_t re = clampToInt8(static_cast<float>(s.real() * window));
        const int8_t im = clampToInt8(
            static_cast<float>((invert_imag ? -s.imag() : s.imag()) * window));
        tx[2 * i] = static_cast<uint8_t>(re);
        tx[2 * i + 1] = static_cast<uint8_t>(im);
    }
    return tx;
}

std::vector<uint8_t> FpgaFftProcessor::makeTxFrameFromRaw(const int8_t *raw_iq,
                                                          size_t num_iq_pairs,
                                                          bool invert_imag) {
    std::vector<uint8_t> tx(kTxFrameBytes, 0);
    if (!raw_iq || num_iq_pairs == 0) {
        return tx;
    }

    const size_t start =
        num_iq_pairs > static_cast<size_t>(kFftSize)
            ? num_iq_pairs - static_cast<size_t>(kFftSize)
            : 0;
    const size_t count =
        std::min(num_iq_pairs, static_cast<size_t>(kFftSize));

    double mean_re = 0.0;
    double mean_im = 0.0;
    for (size_t i = 0; i < count; ++i) {
        mean_re += static_cast<double>(raw_iq[2 * (start + i)]);
        mean_im += static_cast<double>(raw_iq[2 * (start + i) + 1]);
    }
    mean_re /= static_cast<double>(std::max<size_t>(1, count));
    mean_im /= static_cast<double>(std::max<size_t>(1, count));

    for (size_t i = 0; i < count; ++i) {
        const double re =
            static_cast<double>(raw_iq[2 * (start + i)]) - mean_re;
        const double im =
            static_cast<double>(raw_iq[2 * (start + i) + 1]) - mean_im;
        const double window = hannWindow(i);
        const int windowed_re =
            static_cast<int>(std::llround(re * window));
        const int windowed_im =
            static_cast<int>(std::llround(im * window));
        tx[2 * i] = static_cast<uint8_t>(clampInt32ToInt8(windowed_re));
        tx[2 * i + 1] = static_cast<uint8_t>(clampInt32ToInt8(
            invert_imag ? -windowed_im : windowed_im));
    }
    return tx;
}

std::vector<double> FpgaFftProcessor::rxFrameToSpectrumDb(const uint8_t *rx_data,
                                                          size_t rx_size) {
    if (!rx_data || rx_size < kBytesPerFftBin) {
        return {};
    }

    std::vector<double> spectrum_db(static_cast<size_t>(kFftSize), -240.0);
    const size_t complete_bins =
        std::min(static_cast<size_t>(kFftSize), rx_size / kBytesPerFftBin);
    const double full_scale =
        static_cast<double>(kFftSize) * kHannCoherentGain * kInputFullScale;
    for (size_t fpga_bin = 0; fpga_bin < complete_bins; ++fpga_bin) {
        const size_t offset = fpga_bin * kBytesPerFftBin;
        const int32_t re = readLeI32(rx_data + offset);
        const int32_t im = readLeI32(rx_data + offset + sizeof(int32_t));
        const double magnitude =
            std::sqrt(static_cast<double>(re) * static_cast<double>(re) +
                      static_cast<double>(im) * static_cast<double>(im));
        const int display_bin =
            correctedDisplayBinForFpgaBin(static_cast<int>(fpga_bin));
        spectrum_db[static_cast<size_t>(display_bin)] =
            20.0 * std::log10(magnitude / full_scale + 1e-12);
    }
    return spectrum_db;
}

int FpgaFftProcessor::correctedDisplayBinForFpgaBin(int fpga_bin) {
    const int mirrored = fpga_bin <= 0 ? 0 : kFftSize - fpga_bin;
    return (mirrored + kFftSize / 2) % kFftSize;
}

bool FpgaFftProcessor::ensureOpen(std::string &error) {
#ifdef HAVE_FTDI_D2XX
    if (handle_) {
        return true;
    }

    FT_HANDLE handle = nullptr;
    FT_STATUS status = FT_OpenEx(
        const_cast<char *>(serial_.c_str()), FT_OPEN_BY_SERIAL_NUMBER, &handle);
    if (status != FT_OK) {
        const std::string serial_error = ftStatusName(status);
        const std::string list_summary = d2xxDeviceListSummary();
        const DWORD count = d2xxDeviceCount();
        if (count == 0) {
            error = "FTDI data device serial '" + serial_ +
                    "' was not opened: " + serial_error + "; " +
                    list_summary;
            return false;
        }

        const int index = std::min(preferredDeviceIndex(),
                                   static_cast<int>(count - 1));
        status = FT_Open(index, &handle);
        if (status != FT_OK) {
            error = "FTDI data device serial '" + serial_ +
                    "' was not opened: " + serial_error +
                    "; fallback FT_Open(index=" + std::to_string(index) +
                    ") failed: " + ftStatusName(status) + "; " +
                    list_summary;
            return false;
        }
    }

    handle_ = handle;
    warmed_up_ = false;
    status = FT_ResetDevice(static_cast<FT_HANDLE>(handle_));
    if (status != FT_OK) {
        error = "FTDI reset failed: " + ftStatusName(status);
        close();
        return false;
    }
    FT_Purge(static_cast<FT_HANDLE>(handle_), FT_PURGE_RX | FT_PURGE_TX);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    status = FT_SetBitMode(static_cast<FT_HANDLE>(handle_), 0x00, 0x00);
    if (status != FT_OK) {
        error = "FTDI bitmode reset failed: " + ftStatusName(status);
        close();
        return false;
    }
    status = FT_SetBitMode(static_cast<FT_HANDLE>(handle_), 0xff, 0x40);
    if (status != FT_OK) {
        error = "FTDI Sync FIFO mode failed: " + ftStatusName(status);
        close();
        return false;
    }
    status = FT_SetFlowControl(static_cast<FT_HANDLE>(handle_), FT_FLOW_RTS_CTS,
                               0, 0);
    if (status != FT_OK) {
        error = "FTDI flow control setup failed: " + ftStatusName(status);
        close();
        return false;
    }
    FT_SetTimeouts(static_cast<FT_HANDLE>(handle_), kRxResponseTimeoutMs, 500);
    FT_SetLatencyTimer(static_cast<FT_HANDLE>(handle_), 2);
    FT_SetUSBParameters(static_cast<FT_HANDLE>(handle_), 65536, 65536);
    FT_Purge(static_cast<FT_HANDLE>(handle_), FT_PURGE_RX | FT_PURGE_TX);
    return true;
#else
    error = "FTDI D2XX support was not found at build time; install ftd2xx.h "
            "and libftd2xx.so, then reconfigure CMake";
    return false;
#endif
}

bool FpgaFftProcessor::waitForRxReady(size_t bytes_needed, unsigned timeout_ms,
                                     unsigned &rx_queued, std::string &error) {
#ifdef HAVE_FTDI_D2XX
    rx_queued = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD queued = 0;
        const FT_STATUS status =
            FT_GetQueueStatus(static_cast<FT_HANDLE>(handle_), &queued);
        if (status != FT_OK) {
            error = "FTDI queue status failed: " + ftStatusName(status);
            return false;
        }
        rx_queued = static_cast<unsigned>(queued);
        if (bytes_needed == 0) {
            if (queued == 0) {
                return true;
            }
        } else if (queued >= static_cast<DWORD>(bytes_needed)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (bytes_needed == 0) {
        error = "FTDI RX queue did not drain";
        return false;
    }
    if (bytes_needed > 1 &&
        rx_queued + 1 >= static_cast<unsigned>(bytes_needed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        DWORD queued = 0;
        if (FT_GetQueueStatus(static_cast<FT_HANDLE>(handle_), &queued) == FT_OK) {
            rx_queued = static_cast<unsigned>(queued);
        }
        return true;
    }
    error = "FTDI RX not ready: queued " + std::to_string(rx_queued) + "/" +
            std::to_string(bytes_needed) + " bytes";
    return false;
#else
    (void)bytes_needed;
    (void)timeout_ms;
    rx_queued = 0;
    error = "FTDI D2XX support is not compiled in";
    return false;
#endif
}

bool FpgaFftProcessor::writeAll(const uint8_t *data, size_t size,
                                std::string &error) {
#ifdef HAVE_FTDI_D2XX
    DWORD written = 0;
    const FT_STATUS status =
        FT_Write(static_cast<FT_HANDLE>(handle_), const_cast<uint8_t *>(data),
                 static_cast<DWORD>(size), &written);
    if (status != FT_OK) {
        error = "FTDI write failed: " + ftStatusName(status);
        return false;
    }
    if (written != size) {
        error = "FTDI partial write: " + std::to_string(written) + "/" +
                std::to_string(size) + " bytes";
        return false;
    }
    return true;
#else
    (void)data;
    (void)size;
    error = "FTDI D2XX support is not compiled in";
    return false;
#endif
}

bool FpgaFftProcessor::readFrame(uint8_t *data, size_t size,
                                 size_t &bytes_read, std::string &error) {
#ifdef HAVE_FTDI_D2XX
    bytes_read = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kRxResponseTimeoutMs);
    while (bytes_read < size) {
        if (std::chrono::steady_clock::now() >= deadline) {
            error = "FTDI partial frame timeout: " + std::to_string(bytes_read) +
                    "/" + std::to_string(size) + " bytes";
            return false;
        }
        DWORD chunk = 0;
        const DWORD want = static_cast<DWORD>(
            std::min<size_t>(size - bytes_read, 64U * 1024U));
        const FT_STATUS status =
            FT_Read(static_cast<FT_HANDLE>(handle_), data + bytes_read, want,
                    &chunk);
        if (status != FT_OK) {
            error = "FTDI read failed after " + std::to_string(bytes_read) +
                    "/" + std::to_string(size) +
                    " bytes: " + ftStatusName(status);
            return false;
        }
        if (chunk == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        bytes_read += chunk;
    }
    return true;
#else
    (void)data;
    (void)size;
    bytes_read = 0;
    error = "FTDI D2XX support is not compiled in";
    return false;
#endif
}
