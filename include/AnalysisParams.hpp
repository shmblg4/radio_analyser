#ifndef ANALYSIS_PARAMS_HPP
#define ANALYSIS_PARAMS_HPP

#include <cstdint>
#include <vector>

struct AnalysisSweepSegment {
    uint64_t center_freq_hz = 0;
    double sample_rate_hz = 0.0;
    uint32_t bandwidth_hz = 0;
};

struct AnalysisSweepPlan {
    double requested_span_mhz = 0.0;
    double center_freq_mhz = 0.0;
    int fft_size_per_segment = 0;
    int num_segments = 0;
    int total_bins = 0;
    double rbw_hz = 0.0;
    bool segments_clamped = false;
    std::vector<AnalysisSweepSegment> segments;
};

AnalysisSweepPlan computeAnalysisSweepPlan(double center_mhz, double span_mhz,
                                           int fft_size);
double maxAnalysisSpanMHz(double center_mhz);

bool isValidAnalysisSweepFftSize(int fft_size);

#endif
