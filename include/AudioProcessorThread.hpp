#ifndef AUDIOPROCESSORTHREAD_HPP
#define AUDIOPROCESSORTHREAD_HPP

#include <QThread>
#include <QObject>
#include <complex>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>

class AudioProcessorThread : public QThread {
    Q_OBJECT

public:
    explicit AudioProcessorThread(QObject *parent = nullptr);
    ~AudioProcessorThread();

    void processIQSamples(std::vector<std::complex<float>> iq_samples, 
                         double sample_rate, int audio_rate);
    void stopProcessing();
    void resetDSPState();
    void clearPendingQueue();

signals:
    void audioSamplesReady(const std::vector<int16_t> &samples);

protected:
    void run() override;

private:
    std::vector<int16_t> processDemodulation(const std::vector<std::complex<float>> &iq_samples,
                                            double sample_rate, int audio_rate);

    struct ProcessingData {
        std::vector<std::complex<float>> iq_samples;
        double sample_rate = 0.0;
        int audio_rate = 0;
        bool valid = false;
    };

    // DSP state structure - protected by dsp_state_mutex_
    struct DSPState {
        float dc_accumulator = 0.0f;
        float deemphasis_state = 0.0f;
        float voice_lp_state = 0.0f;
        float voice_hp_prev_input = 0.0f;
        float voice_hp_prev_output = 0.0f;
        float voice_hp2_prev_input = 0.0f;
        float voice_hp2_prev_output = 0.0f;
        std::complex<float> channel_lp_state{0.0f, 0.0f};
        double residual_freq_estimate_hz = 0.0;
        
        void reset() {
            dc_accumulator = 0.0f;
            deemphasis_state = 0.0f;
            voice_lp_state = 0.0f;
            voice_hp_prev_input = 0.0f;
            voice_hp_prev_output = 0.0f;
            voice_hp2_prev_input = 0.0f;
            voice_hp2_prev_output = 0.0f;
            channel_lp_state = std::complex<float>(0.0f, 0.0f);
            residual_freq_estimate_hz = 0.0;
        }
    };

    static constexpr size_t kMaxPendingChunks = 4;
    
    // Queue mutex - protects pending_queue_
    std::mutex queue_mutex_;
    std::deque<ProcessingData> pending_queue_;
    
    // DSP state mutex - protects all DSP state variables
    mutable std::mutex dsp_state_mutex_;
    DSPState dsp_state_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
};

#endif // AUDIOPROCESSORTHREAD_HPP

