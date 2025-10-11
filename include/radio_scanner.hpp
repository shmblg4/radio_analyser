#ifndef RADIO_SCANNER_H
#define RADIO_SCANNER_H

#include <ctime>
#include <hackrf.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

#include "colors.hpp"

#define CENTER_FREQ_MHZ 434e6
#define SAMPLE_RATE_MHZ 2e6
#define BANDWIDTH_MHZ 2e6

class HackrfDevice {
public:
    HackrfDevice();
    ~HackrfDevice();

    bool configure(uint64_t center_freq, double sample_rate, uint32_t bandwidth,
                   uint32_t vga_gain, uint32_t lna_gain);

private:
    hackrf_device *__dev__ = nullptr;
    uint64_t __center_freq__ = 0;
    double __sample_rate__ = 0;
    uint32_t __bandwidth__ = 0;
    uint32_t __vga_gain__ = 0;
    uint32_t __lna_gain__ = 0;
    bool _configure_device();
    std::vector<bool> _validate_gains(int vga_gain, int lna_gain);
};

#endif