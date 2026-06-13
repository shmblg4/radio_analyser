#include "AnalysisParams.hpp"
#include "MainWindowConstants.hpp"

#include <algorithm>
#include <cmath>

bool isValidAnalysisSweepFftSize(int fft_size) {
    return fft_size == 512 || fft_size == 1024;
}

double maxAnalysisSpanMHz(double center_mhz) {
    const double room_below_mhz = center_mhz - ANALYSIS_MIN_FREQ_MHZ;
    const double room_above_mhz = ANALYSIS_MAX_FREQ_MHZ - center_mhz;
    const double freq_limited_mhz =
        2.0 * std::min(room_below_mhz, room_above_mhz);
    const double sweep_limited_mhz =
        static_cast<double>(ANALYSIS_MAX_SWEEP_SEGMENTS) *
        ANALYSIS_SWEEP_SEGMENT_MHZ;
    const double max_span_mhz =
        std::min(freq_limited_mhz, sweep_limited_mhz);
    return std::max(ANALYSIS_MIN_SPAN_MHZ, max_span_mhz);
}

AnalysisSweepPlan computeAnalysisSweepPlan(double center_mhz, double span_mhz,
                                           int fft_size) {
    AnalysisSweepPlan plan;
    plan.center_freq_mhz = center_mhz;
    plan.requested_span_mhz = span_mhz;

    if (!isValidAnalysisSweepFftSize(fft_size)) {
        fft_size = ANALYSIS_DEFAULT_SWEEP_FFT_SIZE;
    }
    plan.fft_size_per_segment = fft_size;

    const double segment_step_hz = ANALYSIS_SWEEP_SEGMENT_MHZ * 1e6;
    int num_segments = static_cast<int>(
        std::ceil(span_mhz / ANALYSIS_SWEEP_SEGMENT_MHZ));
    num_segments = std::max(1, num_segments);
    if (num_segments > ANALYSIS_MAX_SWEEP_SEGMENTS) {
        plan.segments_clamped = true;
        num_segments = ANALYSIS_MAX_SWEEP_SEGMENTS;
    }
    plan.num_segments = num_segments;

    const double span_hz = span_mhz * 1e6;
    const double span_start_hz = center_mhz * 1e6 - span_hz / 2.0;

    plan.segments.reserve(static_cast<size_t>(num_segments));
    for (int i = 0; i < num_segments; ++i) {
        AnalysisSweepSegment segment;
        const double segment_center_hz =
            span_start_hz + (static_cast<double>(i) + 0.5) * segment_step_hz;
        segment.center_freq_hz =
            static_cast<uint64_t>(std::llround(segment_center_hz));

        double segment_sr_hz = segment_step_hz;
        if (num_segments == 1) {
            segment_sr_hz = std::max(span_hz, ANALYSIS_MIN_SAMPLE_RATE_MHZ * 1e6);
        }
        segment.sample_rate_hz = segment_sr_hz;
        segment.bandwidth_hz =
            static_cast<uint32_t>(std::llround(segment_sr_hz));
        plan.segments.push_back(segment);
    }

    plan.total_bins = num_segments * fft_size;
    plan.rbw_hz = span_hz / static_cast<double>(std::max(1, plan.total_bins));
    return plan;
}
