#include "MainWindow.hpp"
#include "radio_scanner.hpp"
#include "AudioProcessorThread.hpp"

#ifdef HAVE_QT_AUDIO
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#endif

#include <cstring>
#include <cmath>
#include <algorithm>
#include <QMessageBox>
#include <spdlog/spdlog.h>

void MainWindow::stopListeningInternal() {
#ifdef HAVE_QT_AUDIO
    listeningActive = false;
    if (listenToggleButton) {
        listenToggleButton->setText(tr("Старт прослушивания"));
    }
    if (audioTimer) {
        audioTimer->stop();
    }
    if (audioSink) {
        audioSink->stop();
        audioIODevice = nullptr;
    }
    audioBuffer.clear();
    if (audioProcessorThread) {
        audioProcessorThread->resetDSPState();
        audioProcessorThread->clearPendingQueue();
    }
    updateListeningParameterControls();
    updateListeningStatus();
#endif
}

void MainWindow::toggleListening() {
    if (appMode_ != AppMode::Detection) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Прослушивание доступно только в режиме "
                                "детектирования."));
        return;
    }

    if (!device) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Устройство не инициализировано."));
        return;
    }

    if (!listeningActive) {
        listeningActive = true;
        enforceFftBackendConstraints();
        applyFftBackendToDevice();

        const double desired_center_hz =
            frequencySpinBox ? (frequencySpinBox->value() * 1e6) : 0.0;
        const double demod_if_offset_hz =
            demodOffsetSpinBox ? demodOffsetSpinBox->value() : 0.0;
        const double tuned_center_hz =
            std::max(0.0, desired_center_hz + demod_if_offset_hz);
        alloc_params.center_freq =
            static_cast<uint64_t>(std::llround(tuned_center_hz));
        alloc_params.sample_rate =
            static_cast<double>(sampleRateSpinBox->value() * 1e6);
        alloc_params.bandwidth =
            static_cast<uint32_t>(bandwidthSpinBox->value() * 1e6);
        alloc_params.vga_gain = vgaSlider->value();
        alloc_params.lna_gain = lnaSlider->value();
        fftSize = fftSizeBox->currentData().toInt();
        alloc_params.fft_size = fftSize;

        device->stopRx();
        if (!device->configure(alloc_params) || !device->startRx()) {
            QMessageBox::critical(this, tr("Ошибка"),
                                  tr("Не удалось запустить прослушивание."));
            listeningActive = false;
            return;
        }
        
        setupPlot();
        logBaselineMetrics("startListening");

        if (listenToggleButton)
            listenToggleButton->setText(tr("Стоп прослушивания"));
#ifdef HAVE_QT_AUDIO
        if (!audioTimer) {
            QMessageBox::warning(this, tr("Ошибка"),
                                tr("Аудио таймер не инициализирован."));
            listeningActive = false;
            return;
        }
        if (!audioSink) {
            QMessageBox::warning(this, tr("Ошибка"),
                                tr("Аудио устройство не инициализировано. Попробуйте перезапустить приложение."));
            listeningActive = false;
            return;
        }
        if (!audioIODevice) {
            audioIODevice = audioSink->start();
            if (!audioIODevice) {
                QMessageBox::warning(this, tr("Ошибка"),
                                    tr("Не удалось открыть аудио устройство для воспроизведения."));
                listeningActive = false;
                return;
            }
        }
        spdlog::info("Starting audio processing: target_freq={} Hz, tuned_freq={} Hz, if_offset={} Hz, sample_rate={} Hz, bandwidth={} Hz",
                     static_cast<uint64_t>(std::llround(desired_center_hz)),
                     alloc_params.center_freq,
                     demod_if_offset_hz,
                     alloc_params.sample_rate,
                     alloc_params.bandwidth);
        if (audioProcessorThread) {
            audioProcessorThread->resetDSPState();
            audioProcessorThread->clearPendingQueue();
        }
        audioTimer->start(20);
#else
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Аудио поддержка не доступна (Qt Multimedia не найден при компиляции)."));
        listeningActive = false;
        return;
#endif
    } else {
        stopListeningInternal();
    }

    updateListeningParameterControls();
    if (listeningStatusLabel || listeningFrequencyLabel) {
        updateListeningStatus();
    }
}

void MainWindow::processAudio() {
#ifdef HAVE_QT_AUDIO
    if (!device || !audioProcessorThread || !listeningActive) {
        return;
    }

    auto iq_samples = device->getIQSamplesForProcessing(false);
    if (iq_samples.size() < 2) {
        return;
    }

    static int debug_counter = 0;
    if (++debug_counter % 50 == 0) {
        spdlog::debug("Sending IQ samples to audio processor: {} samples", iq_samples.size());
    }

    const double in_sample_rate = alloc_params.sample_rate;
    const int audio_rate = audioSampleRate;

    updateSpectrumFromIQ(iq_samples);
    
    const double demod_if_offset_hz =
        demodOffsetSpinBox ? demodOffsetSpinBox->value() : 0.0;
    audioProcessorThread->processIQSamples(std::move(iq_samples), in_sample_rate,
                                           audio_rate, demod_if_offset_hz);
#endif
}

void MainWindow::onAudioSamplesReady(const std::vector<int16_t> &samples) {
#ifdef HAVE_QT_AUDIO
    if (!listeningActive || !audioIODevice || samples.empty()) {
        return;
    }

    audioBuffer.resize(static_cast<int>(samples.size() * sizeof(int16_t)));
    std::memcpy(audioBuffer.data(), samples.data(),
                static_cast<size_t>(audioBuffer.size()));

    qint64 written = audioIODevice->write(audioBuffer);
    if (written < 0) {
        spdlog::warn("Audio write failed");
    }
#endif
}

void MainWindow::updateListeningStatus() {
    if (!listeningStatusLabel) {
        return;
    }

    if (listeningActive) {
        listeningStatusLabel->setText(tr("Статус: Активно"));
        listeningStatusLabel->setStyleSheet("font-weight: bold; color: #2e7d32;");
    } else {
        listeningStatusLabel->setText(tr("Статус: Остановлено"));
        listeningStatusLabel->setStyleSheet("font-weight: bold; color: #d32f2f;");
    }

    if (listeningFrequencyLabel) {
        if (listeningActive && device) {
            const double freq_mhz =
                frequencySpinBox ? frequencySpinBox->value()
                                 : (alloc_params.center_freq / 1e6);
            listeningFrequencyLabel->setText(
                QString(tr("Частота: %1 MHz")).arg(freq_mhz, 0, 'f', 3));
        } else {
            listeningFrequencyLabel->setText(tr("Частота: -- MHz"));
        }
    }
}
