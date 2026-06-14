#include "FpgaFftProcessor.hpp"

#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
    int tone_bin = 100;
    if (argc > 1) {
        tone_bin = std::atoi(argv[1]);
    }
    if (tone_bin <= 0 || tone_bin >= FpgaFftProcessor::kFftSize) {
        std::cerr << "tone_bin must be in 1..1023\n";
        return EXIT_FAILURE;
    }

    std::vector<std::complex<float>> iq(FpgaFftProcessor::kFftSize);
    for (int n = 0; n < FpgaFftProcessor::kFftSize; ++n) {
        const double phase =
            2.0 * M_PI * static_cast<double>(tone_bin) *
            static_cast<double>(n) /
            static_cast<double>(FpgaFftProcessor::kFftSize);
        iq[static_cast<size_t>(n)] = {
            static_cast<float>(0.75 * std::cos(phase)),
            static_cast<float>(0.75 * std::sin(phase))};
    }

    FpgaFftProcessor fpga;
    std::vector<double> spectrum_db;
    std::string error;
    if (!fpga.process(iq, spectrum_db, error)) {
        std::cerr << "FPGA FFT smoke failed: " << error << "\n";
        return EXIT_FAILURE;
    }

    int best_bin = 0;
    double best_db = -1e9;
    for (int i = 0; i < static_cast<int>(spectrum_db.size()); ++i) {
        if (spectrum_db[static_cast<size_t>(i)] > best_db) {
            best_db = spectrum_db[static_cast<size_t>(i)];
            best_bin = i;
        }
    }

    std::cout << "tone_bin=" << tone_bin << " peak_bin=" << best_bin
              << " peak_db=" << best_db << "\n";
    const int expected_display_bin =
        (FpgaFftProcessor::kFftSize / 2 + tone_bin) %
        FpgaFftProcessor::kFftSize;
    if (best_bin != expected_display_bin) {
        std::cerr << "expected_display_bin=" << expected_display_bin << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
