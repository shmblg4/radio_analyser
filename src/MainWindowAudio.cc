#include "MainWindow.hpp"
#include "radio_scanner.hpp"

#ifdef HAVE_QT_AUDIO
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#endif

#include <complex>
#include <cmath>
#include <cstring>
#include <limits>
#include <QMessageBox>
#include <spdlog/spdlog.h>

void MainWindow::toggleListening() {
    if (!device) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Устройство не инициализировано."));
        return;
    }

    if (!listeningActive) {
        listeningActive = true;

        alloc_params.center_freq =
            static_cast<uint64_t>(frequencySpinBox->value() * 1e6);
        alloc_params.sample_rate = 192000.0;
        alloc_params.bandwidth = static_cast<uint32_t>(25e3);

        device->stopRx();
        if (!device->configure(alloc_params) || !device->startRx()) {
            QMessageBox::critical(this, tr("Ошибка"),
                                  tr("Не удалось запустить прослушивание."));
            listeningActive = false;
            return;
        }
        
        setupPlot();

        if (listenToggleButton)
            listenToggleButton->setText(tr("Стоп прослушивания"));
#ifdef HAVE_QT_AUDIO
        if (!audioTimer) {
            QMessageBox::warning(this, tr("Ошибка"),
                                tr("Аудио таймер не инициализирован."));
            listeningActive = false;
            return;
        }
        if (!audioIODevice) {
            QMessageBox::warning(this, tr("Ошибка"),
                                tr("Аудио устройство не инициализировано. Попробуйте перезапустить приложение."));
            listeningActive = false;
            return;
        }
        spdlog::info("Starting audio processing: freq={} Hz, sample_rate={} Hz, bandwidth={} Hz",
                    alloc_params.center_freq, alloc_params.sample_rate, alloc_params.bandwidth);
        audioTimer->start(20);
#else
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Аудио поддержка не доступна (Qt Multimedia не найден при компиляции)."));
        listeningActive = false;
        return;
#endif
    } else {
        listeningActive = false;
        if (listenToggleButton)
            listenToggleButton->setText(tr("Старт прослушивания"));
        if (audioTimer)
            audioTimer->stop();
#ifdef HAVE_QT_AUDIO
        if (audioSink) {
            audioSink->stop();
            audioIODevice = audioSink->start();
        }
        audioBuffer.clear();
#endif
    }

    updateListeningParameterControls();
}

void MainWindow::processAudio() {
#ifdef HAVE_QT_AUDIO
    if (!device || !audioIODevice || !listeningActive) {
        return;
    }

    auto iq_samples = device->getIQSamplesForProcessing();
    if (iq_samples.size() < 2) {
        return;
    }

    static int debug_counter = 0;
    if (++debug_counter % 50 == 0) {
        spdlog::debug("Processing audio: {} IQ samples", iq_samples.size());
    }

    const double in_sample_rate = alloc_params.sample_rate;
    const int audio_rate = audioSampleRate;
    if (in_sample_rate <= 0 || audio_rate <= 0) {
        return;
    }

    int decim = static_cast<int>(in_sample_rate / audio_rate);
    if (decim < 1)
        decim = 1;

    std::vector<float> demod;
    demod.reserve(iq_samples.size());

    const float phase_scale = static_cast<float>(in_sample_rate) / (2.0f * M_PI);
    std::complex<float> prev = iq_samples[0];
    for (size_t i = 1; i < iq_samples.size(); ++i) {
        std::complex<float> curr = iq_samples[i];
        std::complex<float> diff = curr * std::conj(prev);
        float angle = std::atan2(diff.imag(), diff.real());
        demod.push_back(angle * phase_scale);
        prev = curr;
    }

    if (demod.empty()) {
        return;
    }

    static float dc_accumulator = 0.0f;
    if (!listeningActive) {
        dc_accumulator = 0.0f;
    }
    const float dc_alpha = 0.995f;
    for (float &v : demod) {
        dc_accumulator = dc_alpha * dc_accumulator + v;
        v = v - dc_accumulator;
    }

    const int filter_length = static_cast<int>(in_sample_rate / 4000.0);
    const int actual_filter_length = (filter_length > 1) ? filter_length : 1;
    std::vector<float> filtered_demod;
    filtered_demod.reserve(demod.size());
    
    for (size_t i = 0; i < demod.size(); ++i) {
        float sum = 0.0f;
        int count = 0;
        int start = static_cast<int>(i) - actual_filter_length / 2;
        int end = static_cast<int>(i) + actual_filter_length / 2;
        start = (start < 0) ? 0 : start;
        end = (end >= static_cast<int>(demod.size())) ? static_cast<int>(demod.size() - 1) : end;
        
        for (int j = start; j <= end; ++j) {
            sum += demod[j];
            count++;
        }
        filtered_demod.push_back(sum / count);
    }

    std::vector<int16_t> audioSamples;
    audioSamples.reserve(filtered_demod.size() / decim + 1);

    float maxVal = 0.0f;
    for (float v : filtered_demod) {
        float absv = std::abs(v);
        if (absv > maxVal)
            maxVal = absv;
    }

    const float minPeak = 100.0f;
    float scale = 1.0f;
    if (maxVal > minPeak) {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * 0.8f / maxVal;
    } else {
        scale = static_cast<float>(std::numeric_limits<int16_t>::max()) * 0.1f / minPeak;
    }

    for (size_t i = 0; i < filtered_demod.size(); i += static_cast<size_t>(decim)) {
        float v = filtered_demod[i] * scale;
        if (v > static_cast<float>(std::numeric_limits<int16_t>::max()))
            v = static_cast<float>(std::numeric_limits<int16_t>::max());
        if (v < static_cast<float>(std::numeric_limits<int16_t>::min()))
            v = static_cast<float>(std::numeric_limits<int16_t>::min());
        audioSamples.push_back(static_cast<int16_t>(v));
    }

    if (audioSamples.empty()) {
        return;
    }

    audioBuffer.resize(static_cast<int>(audioSamples.size() * sizeof(int16_t)));
    std::memcpy(audioBuffer.data(), audioSamples.data(),
                static_cast<size_t>(audioBuffer.size()));

    qint64 written = audioIODevice->write(audioBuffer);
    if (written < 0) {
        spdlog::warn("Audio write failed");
    }
#endif
}

