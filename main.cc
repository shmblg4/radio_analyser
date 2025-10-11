#include <radio_scanner.hpp>

int main() {
    HackrfDevice device;
    device.configure(CENTER_FREQ_MHZ, SAMPLE_RATE_MHZ, BANDWIDTH_MHZ, 46, 16);

    return 0;
}