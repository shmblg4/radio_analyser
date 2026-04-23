#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <fftw3.h>
#include <iostream>
#include <vector>

namespace {

double normalizeBy128(int8_t v) { return static_cast<double>(v) / 128.0; }
double normalizeBy127(int8_t v) { return static_cast<double>(v) / 127.0; }

double estimateToneFreqHz(const std::vector<std::complex<double>> &samples,
                          double sampleRateHz) {
    const int n = static_cast<int>(samples.size());
    fftw_complex *in = static_cast<fftw_complex *>(
        fftw_malloc(sizeof(fftw_complex) * static_cast<size_t>(n)));
    fftw_complex *out = static_cast<fftw_complex *>(
        fftw_malloc(sizeof(fftw_complex) * static_cast<size_t>(n)));
    for (int i = 0; i < n; ++i) {
        in[i][0] = samples[static_cast<size_t>(i)].real();
        in[i][1] = samples[static_cast<size_t>(i)].imag();
    }

    fftw_plan p = fftw_plan_dft_1d(n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(p);

    int bestBin = 0;
    double bestMag = 0.0;
    std::vector<double> mags(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        const double re = out[i][0];
        const double im = out[i][1];
        const double mag = std::sqrt(re * re + im * im);
        mags[static_cast<size_t>(i)] = mag;
        if (mag > bestMag) {
            bestMag = mag;
            bestBin = i;
        }
    }

    double delta = 0.0;
    if (bestBin > 0 && bestBin < n - 1) {
        const double left = mags[static_cast<size_t>(bestBin - 1)];
        const double center = mags[static_cast<size_t>(bestBin)];
        const double right = mags[static_cast<size_t>(bestBin + 1)];
        const double denom = left - 2.0 * center + right;
        if (std::abs(denom) > 1e-12) {
            delta = 0.5 * (left - right) / denom;
            if (delta > 0.5) {
                delta = 0.5;
            } else if (delta < -0.5) {
                delta = -0.5;
            }
        }
    }

    const double freqHz = (bestBin + delta) * sampleRateHz / n;

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);
    return freqHz;
}

}

int main() {
    const double max128 = normalizeBy128(127);
    const double min128 = normalizeBy128(-128);
    if (!(std::abs(max128 - 0.9921875) < 1e-6 && std::abs(min128 + 1.0) < 1e-6)) {
        std::cerr << "Normalization /128 check failed\n";
        return EXIT_FAILURE;
    }

    const double max127 = normalizeBy127(127);
    if (!(std::abs(max127 - 1.0) < 1e-6)) {
        std::cerr << "Normalization /127 check failed\n";
        return EXIT_FAILURE;
    }

    const int fftSize = 4096;
    const double sampleRateHz = 2.0e6;
    const double toneHz = 5000.0;
    std::vector<std::complex<double>> tone;
    tone.reserve(static_cast<size_t>(fftSize));
    for (int n = 0; n < fftSize; ++n) {
        const double ph = 2.0 * M_PI * toneHz * n / sampleRateHz;
        tone.emplace_back(std::cos(ph), std::sin(ph));
    }

    const double estimated = estimateToneFreqHz(tone, sampleRateHz);
    if (std::abs(estimated - toneHz) > 300.0) {
        std::cerr << "Tone estimation check failed: expected " << toneHz
                  << " got " << estimated << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "DSP self-test passed\n";
    return EXIT_SUCCESS;
}
