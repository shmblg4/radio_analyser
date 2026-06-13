#include "FpgaFftProcessor.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

#ifdef HAVE_FTDI_D2XX
#include <ftd2xx.h>
#endif

namespace {
constexpr size_t kBytesPerFftBin = 2U * sizeof(int32_t);

int8_t clampToInt8(float value) {
    const float scaled = std::round(value * 127.0f);
    const float clamped = std::clamp(
        scaled, static_cast<float>(std::numeric_limits<int8_t>::min()),
        static_cast<float>(std::numeric_limits<int8_t>::max()));
    return static_cast<int8_t>(clamped);
}

int32_t readLeI32(const uint8_t *p) {
    const uint32_t v = static_cast<uint32_t>(p[0]) |
                       (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) |
                       (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(v);
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

    if (!ensureOpen(error)) {
        return false;
    }

    const std::vector<uint8_t> tx = makeTxFrame(iq_samples, false);
    if (!writeAll(tx.data(), tx.size(), error)) {
        close();
        return false;
    }

    std::vector<uint8_t> rx(kRxFrameBytes);
    size_t bytes_read = 0;
    if (!readFrame(rx.data(), rx.size(), bytes_read, error)) {
        if (bytes_read < kBytesPerFftBin) {
            close();
            return false;
        }
    }

    spectrum_db = rxFrameToSpectrumDb(rx.data(), bytes_read);
    return spectrum_db.size() == static_cast<size_t>(kFftSize);
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
        const int8_t re = clampToInt8(s.real());
        const int8_t im = clampToInt8(invert_imag ? -s.imag() : s.imag());
        tx[2 * i] = static_cast<uint8_t>(re);
        tx[2 * i + 1] = static_cast<uint8_t>(im);
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
    for (size_t fpga_bin = 0; fpga_bin < complete_bins; ++fpga_bin) {
        const size_t offset = fpga_bin * kBytesPerFftBin;
        const int32_t re = readLeI32(rx_data + offset);
        const int32_t im = readLeI32(rx_data + offset + sizeof(int32_t));
        const double re_d = static_cast<double>(re);
        const double im_d = static_cast<double>(im);
        const double power = re_d * re_d + im_d * im_d;
        const int display_bin =
            correctedDisplayBinForFpgaBin(static_cast<int>(fpga_bin));
        spectrum_db[static_cast<size_t>(display_bin)] =
            power > 0.0 ? 10.0 * std::log10(power) : -240.0;
    }
    return spectrum_db;
}

int FpgaFftProcessor::correctedDisplayBinForFpgaBin(int fpga_bin) {
    if (fpga_bin <= 0) {
        return 0;
    }
    return kFftSize - fpga_bin;
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
    status = FT_ResetDevice(static_cast<FT_HANDLE>(handle_));
    if (status != FT_OK) {
        error = "FTDI reset failed: " + ftStatusName(status);
        close();
        return false;
    }
    FT_Purge(static_cast<FT_HANDLE>(handle_), FT_PURGE_RX | FT_PURGE_TX);

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
    status = FT_SetFlowControl(static_cast<FT_HANDLE>(handle_),
                               FT_FLOW_RTS_CTS, 0, 0);
    if (status != FT_OK) {
        error = "FTDI flow control setup failed: " + ftStatusName(status);
        close();
        return false;
    }
    FT_SetTimeouts(static_cast<FT_HANDLE>(handle_), 500, 500);
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
    while (bytes_read < size) {
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
            error = "FTDI partial frame: " + std::to_string(bytes_read) + "/" +
                    std::to_string(size) + " bytes";
            return false;
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
