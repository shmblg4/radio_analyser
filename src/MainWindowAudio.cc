#include "MainWindow.hpp"
#include "radio_scanner.hpp"
#include "AudioProcessorThread.hpp"

#ifdef HAVE_QT_AUDIO
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#endif

#include <cstring>
#include <cmath>
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
        alloc_params.sample_rate = 2000000.0;
        alloc_params.bandwidth = 1750000u;
        alloc_params.vga_gain = vgaSlider->value();
        alloc_params.lna_gain = lnaSlider->value();

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
        spdlog::info("Starting audio processing: freq={} Hz, sample_rate={} Hz, bandwidth={} Hz",
                    alloc_params.center_freq, alloc_params.sample_rate, alloc_params.bandwidth);
        if (audioProcessorThread) {
            audioProcessorThread->resetDCAccumulator();
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
        listeningActive = false;
        if (listenToggleButton)
            listenToggleButton->setText(tr("Старт прослушивания"));
        if (audioTimer)
            audioTimer->stop();
#ifdef HAVE_QT_AUDIO
        if (audioSink) {
            audioSink->stop();
            audioIODevice = nullptr;
        }
        audioBuffer.clear();
        if (audioProcessorThread) {
            audioProcessorThread->resetDCAccumulator();
            audioProcessorThread->clearPendingQueue();
        }
#endif
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

    auto iq_samples = device->getIQSamplesForProcessing();
    if (iq_samples.size() < 2) {
        return;
    }

    static int debug_counter = 0;
    if (++debug_counter % 50 == 0) {
        spdlog::debug("Sending IQ samples to audio processor: {} samples", iq_samples.size());
    }

    const double in_sample_rate = alloc_params.sample_rate;
    const int audio_rate = audioSampleRate;

    audioProcessorThread->processIQSamples(iq_samples, in_sample_rate, audio_rate);
    updateSpectrumFromIQ(iq_samples);
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
            double freq_mhz = alloc_params.center_freq / 1e6;
            listeningFrequencyLabel->setText(
                QString(tr("Частота: %1 MHz")).arg(freq_mhz, 0, 'f', 3));
        } else {
            listeningFrequencyLabel->setText(tr("Частота: -- MHz"));
        }
    }
}
