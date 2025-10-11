#include <radio_scanner.hpp>

int main() {
    HackrfDevice device;
    if (!device.configure(CENTER_FREQ_MHZ, SAMPLE_RATE_MHZ, BANDWIDTH_MHZ, 46,
                          16)) {
        spdlog::error("Configuration failed");
        return -1;
    }

    if (!device.startRx()) {
        spdlog::error("RX start failed");
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    device.stopRx();

    auto samples = device.getIQSamples();
    spdlog::info("Captured {} samples", samples.size());

    return 0;
}
