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

    void processIQSamples(const std::vector<std::complex<float>> &iq_samples, 
                         double sample_rate, int audio_rate);
    void stopProcessing();
    void resetDCAccumulator();
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
        double sample_rate;
        int audio_rate;
        bool valid = false;
    };

    static constexpr size_t kMaxPendingChunks = 4;
    std::mutex data_mutex_;
    std::deque<ProcessingData> pending_queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    
    float dc_accumulator_ = 0.0f;
    std::mutex dc_mutex_;
};

#endif // AUDIOPROCESSORTHREAD_HPP

