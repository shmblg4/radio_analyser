#include "AnalysisParams.hpp"
#include "FpgaFftProcessor.hpp"
#include "MainWindowConstants.hpp"

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

void writeLeI32(std::vector<uint8_t> &buf, size_t offset, int32_t value) {
    const uint32_t v = static_cast<uint32_t>(value);
    buf[offset] = static_cast<uint8_t>(v & 0xffU);
    buf[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xffU);
    buf[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xffU);
    buf[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xffU);
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

    {
        const auto plan20 =
            computeAnalysisSweepPlan(433.0, 20.0, 1024);
        if (plan20.num_segments != 4 ||
            plan20.total_bins != 4096 ||
            plan20.fft_size_per_segment != 1024 ||
            plan20.segments.size() != 4U) {
            std::cerr << "AnalysisSweepPlan 20 MHz check failed\n";
            return EXIT_FAILURE;
        }
    }

    {
        const auto plan2 = computeAnalysisSweepPlan(433.0, 2.0, 1024);
        if (plan2.num_segments != 1 ||
            std::abs(plan2.segments.front().sample_rate_hz - 2.0e6) > 1.0) {
            std::cerr << "AnalysisSweepPlan 2 MHz single segment failed\n";
            return EXIT_FAILURE;
        }
    }

    {
        const auto planWide =
            computeAnalysisSweepPlan(3000.0, 500.0, 1024);
        if (planWide.num_segments != ANALYSIS_MAX_SWEEP_SEGMENTS ||
            !planWide.segments_clamped) {
            std::cerr << "AnalysisSweepPlan segment clamp failed\n";
            return EXIT_FAILURE;
        }
    }

    if (std::abs(maxAnalysisSpanMHz(3000.0) - 320.0) > 1e-6) {
        std::cerr << "maxAnalysisSpanMHz sweep cap check failed\n";
        return EXIT_FAILURE;
    }
    if (std::abs(maxAnalysisSpanMHz(5.0) - 8.0) > 1e-6) {
        std::cerr << "maxAnalysisSpanMHz edge check failed\n";
        return EXIT_FAILURE;
    }
    if (maxAnalysisSpanMHz(1.0) < ANALYSIS_MIN_SPAN_MHZ - 1e-6) {
        std::cerr << "maxAnalysisSpanMHz minimum check failed\n";
        return EXIT_FAILURE;
    }

    {
        std::vector<std::complex<float>> iq(FpgaFftProcessor::kFftSize);
        iq[0] = {1.0f, 0.5f};
        const auto tx = FpgaFftProcessor::makeTxFrame(iq, false);
        if (tx.size() != FpgaFftProcessor::kTxFrameBytes ||
            static_cast<int8_t>(tx[0]) != 127 ||
            static_cast<int8_t>(tx[1]) != 64) {
            std::cerr << "FPGA TX frame int8 IQ conversion failed\n";
            return EXIT_FAILURE;
        }
    }

    {
        std::vector<uint8_t> rx(FpgaFftProcessor::kRxFrameBytes, 0);
        const int mirroredBin = 924;
        const size_t offset =
            static_cast<size_t>(mirroredBin) * 2U * sizeof(int32_t);
        writeLeI32(rx, offset, 100000);
        writeLeI32(rx, offset + sizeof(int32_t), 0);

        const auto spectrum =
            FpgaFftProcessor::rxFrameToSpectrumDb(rx.data(), rx.size());
        int bestBin = 0;
        double bestPower = -1e9;
        for (int i = 0; i < static_cast<int>(spectrum.size()); ++i) {
            if (spectrum[static_cast<size_t>(i)] > bestPower) {
                bestPower = spectrum[static_cast<size_t>(i)];
                bestBin = i;
            }
        }
        if (bestBin != 100) {
            std::cerr << "FPGA mirrored bin correction failed: expected 100 got "
                      << bestBin << "\n";
            return EXIT_FAILURE;
        }
    }

    {
        std::vector<uint8_t> rx(FpgaFftProcessor::kRxFrameBytes, 0);
        const int mirroredBin = 924;
        const size_t offset =
            static_cast<size_t>(mirroredBin) * 2U * sizeof(int32_t);
        writeLeI32(rx, offset, 100000);
        writeLeI32(rx, offset + sizeof(int32_t), 0);

        const auto spectrum =
            FpgaFftProcessor::rxFrameToSpectrumDb(rx.data(), rx.size() - 1);
        if (spectrum.size() !=
            static_cast<size_t>(FpgaFftProcessor::kFftSize)) {
            std::cerr << "FPGA partial RX frame parse failed\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "DSP self-test passed\n";
    return EXIT_SUCCESS;
}
