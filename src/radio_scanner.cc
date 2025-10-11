#include "radio_scanner.hpp"

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
    hackrf_close(__dev__);
    hackrf_exit();
    spdlog::info("Device closed");
}

bool HackrfDevice::configure(uint64_t center_freq, double sample_rate,
                             uint32_t bandwidth, uint32_t vga_gain,
                             uint32_t lna_gain) {
    __center_freq__ = center_freq;
    __sample_rate__ = sample_rate;
    __bandwidth__ = bandwidth;
    __vga_gain__ = vga_gain;
    __lna_gain__ = lna_gain;
    return _configure_device();
}

bool HackrfDevice::_configure_device() {
    auto result = hackrf_set_freq(__dev__, __center_freq__);
    if (result != HACKRF_SUCCESS) {
        return false;
    }
    result = hackrf_set_vga_gain(__dev__, __vga_gain__);
    if (result != HACKRF_SUCCESS) {
        return false;
    }
    result = hackrf_set_lna_gain(__dev__, __lna_gain__);
    if (result != HACKRF_SUCCESS) {
        return false;
    }
    result = hackrf_set_sample_rate(__dev__, __sample_rate__);
    if (result != HACKRF_SUCCESS) {
        return false;
    }
    result = hackrf_set_baseband_filter_bandwidth(__dev__, __bandwidth__);
    if (result != HACKRF_SUCCESS) {
        return false;
    }
    spdlog::info("{}############ Device configured ############{}", colors::GREEN, colors::RESET);
    spdlog::info("Center frequency: {}", __center_freq__);
    spdlog::info("Sample rate: {}", __sample_rate__);
    spdlog::info("Bandwidth: {}", __bandwidth__);
    spdlog::info("VGA gain: {}", __vga_gain__);
    spdlog::info("LNA gain: {}", __lna_gain__);
    spdlog::info("{}###########################################{}", colors::GREEN, colors::RESET);
    return true;
}

std::vector<bool> HackrfDevice::_validate_gains(int vga_gain, int lna_gain) {
    std::vector<bool> isvalid = {true, true};
    if (vga_gain < 0 || vga_gain > 62 || vga_gain % 2 != 0) {
        isvalid[0] = false;
    }
    if (lna_gain < 0 || lna_gain > 62 || lna_gain % 2 != 0) {
        isvalid[1] = false;
    }
    return isvalid;
}