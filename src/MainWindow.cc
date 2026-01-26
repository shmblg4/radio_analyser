#include "MainWindow.hpp"
#include "radio_scanner.hpp"
#include "AudioProcessorThread.hpp"

#ifdef HAVE_QT_AUDIO
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#endif

#include <QMessageBox>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <limits>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      centralWidget(new QWidget(this)),
      plot(new QCustomPlot(this)),
      spectrumUpdateTimer(new QTimer(this)),
      averagePowerLevelTimer(new QTimer(this)),
      scanActiveTimer(new QTimer(this)),
      activeFreqCleanupTimer(new QTimer(this)),
      alloc_params({static_cast<uint64_t>(405.125 * 1e6),
                    static_cast<uint64_t>(4.8 * 1e6),
                    static_cast<uint32_t>(2.0 * 1e6), 0, 0, 1024}),
      fftSize(1024),
      average_power(0.0),
      threshold(0),
      listenToggleButton(nullptr),
      volumeSlider(nullptr),
      volumeLabel(nullptr),
      listeningStatusLabel(nullptr),
      listeningFrequencyLabel(nullptr),
      detectedFrequenciesGroup(nullptr),
      detectedFrequenciesList(nullptr),
      plotModeAction(nullptr),
      viewToolBar(nullptr) {

    configure();

    this->setWindowTitle("Radio Scanner");
    controls = new QGroupBox(tr("HackRF Configuration"));
    info = new QGroupBox(tr("Info"));
    setupControls();
    setupInfo();

    QVBoxLayout *controlLayout = new QVBoxLayout;
    controlLayout->addWidget(
        new QLabel(tr("Center Frequency (MHz):")));
    controlLayout->addWidget(frequencySpinBox);

    controlLayout->addWidget(
        new QLabel(tr("Sample Rate (MS/s):")));
    controlLayout->addWidget(sampleRateSpinBox);

    controlLayout->addWidget(
        new QLabel(tr("Bandwidth (MHz):")));
    controlLayout->addWidget(bandwidthSpinBox);

    QHBoxLayout *vgaLayout = new QHBoxLayout;
    vgaLayout->addWidget(vgaSlider);
    vgaLayout->addWidget(vgaLabel);
    controlLayout->addWidget(new QLabel(tr("VGA Gain:")));
    controlLayout->addLayout(vgaLayout);

    QHBoxLayout *lnaLayout = new QHBoxLayout;
    lnaLayout->addWidget(lnaSlider);
    lnaLayout->addWidget(lnaLabel);
    controlLayout->addWidget(new QLabel(tr("LNA Gain:")));
    controlLayout->addLayout(lnaLayout);

    QHBoxLayout *fftLayout = new QHBoxLayout;
    fftLayout->addWidget(fftSizeBox);
    fftLayout->addWidget(fftSizeLabel);
    controlLayout->addWidget(new QLabel(tr("FFT Size:")));
    controlLayout->addLayout(fftLayout);

    QHBoxLayout *thresholdLayout = new QHBoxLayout;
    thresholdLayout->addWidget(thresholdSlider);
    thresholdLayout->addWidget(thresholdLabel);
    controlLayout->addWidget(new QLabel(tr("Threshold:")));
    controlLayout->addLayout(thresholdLayout);

    controlLayout->addWidget(applyButton);
    controlLayout->addStretch();
    controls->setLayout(controlLayout);
    controls->setFixedWidth(300);

    QVBoxLayout *infoLayout = new QVBoxLayout;
    infoLayout->addWidget(new QLabel(tr("Average Power (dB):")));
    infoLayout->addWidget(averagePowerLabel);
    infoLayout->addStretch();
    info->setLayout(infoLayout);
    info->setFixedWidth(300);

#ifdef HAVE_QT_AUDIO
    QAudioFormat format;
    format.setSampleRate(audioSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    audioSink = new QAudioSink(format, this);
    if (audioSink) {
        audioSink->setVolume(0.5);
        audioIODevice = audioSink->start();
        if (!audioIODevice) {
            spdlog::warn("Failed to start audio output device");
        } else {
            spdlog::info("Audio output initialized: {} Hz, {} channels, volume: {}", 
                        format.sampleRate(), format.channelCount(), audioSink->volume());
        }
    } else {
        spdlog::warn("Failed to create QAudioSink");
    }
#endif

    audioTimer = new QTimer(this);
    connect(audioTimer, &QTimer::timeout, this, &MainWindow::processAudio);

#ifdef HAVE_QT_AUDIO
    audioProcessorThread = new AudioProcessorThread(this);
    connect(audioProcessorThread, &AudioProcessorThread::audioSamplesReady,
            this, &MainWindow::onAudioSamplesReady);
    audioProcessorThread->start();
#endif

    setupToolbar();

    setCentralWidget(centralWidget);
    setupLayout();
    
#ifdef HAVE_QT_AUDIO
    if (volumeSlider && audioSink) {
        int volumeValue = volumeSlider->value();
        audioSink->setVolume(volumeValue / 100.0f);
        spdlog::info("Initial volume set to: {}%", volumeValue);
    }
#endif

    setupPlot();
    resize(1000, 600);
    try {
        device = std::make_unique<HackrfDevice>();
        if (!device->configure(alloc_params)) {
            spdlog::error("Failed to configure HackRF device.");
            QMessageBox::critical(this, "Error",
                                  "Failed to configure HackRF device.");
            return;
        }
        device->startRx();
    } catch (const std::exception &e) {
        spdlog::error("Error initializing HackRF: {}", e.what());
        QMessageBox::critical(
            this, "Error",
            QString("Error initializing HackRF: %1").arg(e.what()));
        return;
    }

    connect(spectrumUpdateTimer, &QTimer::timeout, this,
            &MainWindow::updateSpectrum);
    spectrumUpdateTimer->start(50);
    connect(averagePowerLevelTimer, &QTimer::timeout, this,
            &MainWindow::updateAveragePower);
    averagePowerLevelTimer->start(100);
    connect(scanActiveTimer, &QTimer::timeout, this, &MainWindow::scanActive);
    scanActiveTimer->start(500);
    connect(activeFreqCleanupTimer, &QTimer::timeout, this, &MainWindow::activeFreqCleanup);
    activeFreqCleanupTimer->start(5000);
}

MainWindow::~MainWindow() {
    if (device) {
        device->stopRx();
    }
    spectrumUpdateTimer->stop();
    averagePowerLevelTimer->stop();
    scanActiveTimer->stop();
#ifdef HAVE_QT_AUDIO
    if (audioProcessorThread) {
        audioProcessorThread->stopProcessing();
        audioProcessorThread->wait();
    }
#endif
}

void MainWindow::updateSpectrum() {
    if (!device)
        return;

    spectrum_db = device->getMagnitudeSpectrum();

    if (spectrum_db.size() != static_cast<size_t>(fftSize)) {
        return;
    }

    static double last_sample_rate = 0.0;
    static uint64_t last_center_freq = 0;
    static int last_fft_size = 0;

    double freq_resolution_hz = alloc_params.sample_rate / fftSize;
    double start_freq_hz =
        (alloc_params.center_freq - alloc_params.sample_rate / 2.0);
    double end_freq_hz =
        (alloc_params.center_freq + alloc_params.sample_rate / 2.0);
    double freq_resolution_mhz = freq_resolution_hz / 1e6;
    double start_freq_mhz = start_freq_hz / 1e6;
    double end_freq_mhz = end_freq_hz / 1e6;

    if (alloc_params.sample_rate != last_sample_rate ||
        alloc_params.center_freq != last_center_freq ||
        fftSize != last_fft_size) {
        waterfallHistory.clear();

        x_axis_values.resize(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            x_axis_values[i] = start_freq_mhz + (i * freq_resolution_mhz);
        }

        plot->xAxis->setRange(start_freq_mhz, end_freq_mhz);

        last_sample_rate = alloc_params.sample_rate;
        last_center_freq = alloc_params.center_freq;
        last_fft_size = fftSize;
    }

    if (currentPlotMode == PlotMode::Spectrum) {
        if (plot->graphCount() < 2) {
            setupPlot();
            return;
        }

        QVector<double> x(fftSize), y(fftSize), y2(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            x[i] = x_axis_values[i];
            y[i] = spectrum_db[i];
            y2[i] = average_power + threshold; 
        }

        plot->graph(0)->setData(x, y);
        plot->graph(1)->setData(x, y2);
        plot->replot();
    } else {
        QVector<double> line(fftSize);
        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::lowest();
        for (int i = 0; i < fftSize; ++i) {
            line[i] = spectrum_db[i];
            minVal = std::min(minVal, line[i]);
            maxVal = std::max(maxVal, line[i]);
        }

        waterfallHistory.push_back(std::move(line));
        if (static_cast<int>(waterfallHistory.size()) > waterfallHistorySize) {
            waterfallHistory.pop_front();
        }

        if (!waterfallMap) {
            setupPlot();
            return;
        }

        if (waterfallMap && waterfallMap->data()) {
            QCPColorMapData *data = waterfallMap->data();
            data->setSize(fftSize, waterfallHistorySize);
            data->setKeyRange(QCPRange(start_freq_mhz, end_freq_mhz));
            data->setValueRange(QCPRange(0, waterfallHistorySize));

            const int rows = static_cast<int>(waterfallHistory.size());
            for (int j = 0; j < waterfallHistorySize; ++j) {
                int srcIndex = rows - 1 - j;
                const QVector<double> *rowPtr =
                    (srcIndex >= 0 && srcIndex < rows)
                        ? &waterfallHistory[static_cast<size_t>(srcIndex)]
                        : nullptr;
                for (int i = 0; i < fftSize; ++i) {
                    const bool hasRow = rowPtr && i < rowPtr->size();
                    const double v = hasRow
                                         ? (*rowPtr)[i]
                                         : (minVal == std::numeric_limits<
                                                             double>::max()
                                                ? -200.0
                                                : minVal);
                    data->setCell(i, j, v);
                }
            }

            if (waterfallColorScale) {
                waterfallColorScale->setDataRange(QCPRange(minVal, maxVal));
            }
            plot->replot();
        }
    }
}

void MainWindow::applyConfig() {
    if (!device) {
        QMessageBox::warning(this, "Error", "Device is not initialized.");
        return;
    }

    alloc_params.center_freq =
        static_cast<uint64_t>(frequencySpinBox->value() * 1e6);
    alloc_params.sample_rate =
        static_cast<double>(sampleRateSpinBox->value() * 1e6);
    alloc_params.bandwidth =
        static_cast<uint32_t>(bandwidthSpinBox->value() * 1e6);
    alloc_params.vga_gain = vgaSlider->value();
    alloc_params.lna_gain = lnaSlider->value();
    fftSize = fftSizeBox->currentData().toInt();
    alloc_params.fft_size = fftSize;
    fftSizeLabel->setText(QString::number(fftSize));
    windowed_samples.clear();
    windowed_samples.resize(WINDOW_SIZE_BY_FFTSIZE[fftSize]);
    waterfallHistory.clear();
    device->stopRx();
    bool success = device->configure(alloc_params);

    if (success) {
        setupPlot();
        device->startRx();
        spdlog::info("Configuration applied successfully.");
        QMessageBox::information(this, "Success",
                                 "Configuration applied successfully.");
    } else {
        spdlog::error("Failed to apply new configuration.");
        QMessageBox::critical(this, "Error",
                              "Failed to apply new configuration.");
    }
}

void MainWindow::updateAveragePower() {
    if (!device || spectrum_db.empty())
        return;

    double sum = 0.0;
    for (int i = 0; i < static_cast<int>(spectrum_db.size()); ++i) {
        sum += spectrum_db[i];
    }
    average_power = sum / spectrum_db.size();

    averagePowerLabel->setText(QString::number(average_power, 'f', 2));
}

void MainWindow::toggleDisplayMode() {
    currentPlotMode = currentPlotMode == PlotMode::Spectrum
                          ? PlotMode::Waterfall
                          : PlotMode::Spectrum;

    waterfallHistory.clear();

    updateDisplayModeControls();
    setupPlot();
}


void MainWindow::scanActive() {
    if (!device || spectrum_db.empty()) {
        return;
    }

    int total_bins = static_cast<int>(spectrum_db.size());
    int scan_window_size_bins = WINDOW_SIZE_BY_FFTSIZE[fftSize];

    if (scan_window_size_bins > total_bins) {
        spdlog::warn("Scan window size ({}) is larger than total bins ({}). "
                     "Skipping scan.",
                     scan_window_size_bins, total_bins);
        scan_current_start_index = 0;
        return;
    }

    while (scan_current_start_index + scan_window_size_bins <= total_bins) {
        int start_idx = scan_current_start_index;
        int end_idx = start_idx + scan_window_size_bins;

        double sum_in_window = 0.0;
        for (int i = start_idx; i < end_idx; ++i) {
            sum_in_window += spectrum_db[i];
        }
        double average_in_window = sum_in_window / scan_window_size_bins;

        bool activity_detected = false;

        if (average_in_window > (average_power / 2 + threshold)) {
            activity_detected = true;
        }

        if (activity_detected) {
            double detected_freq_mhz = x_axis_values[start_idx + scan_window_size_bins / 2];
            
            double existing_freq = 0.0;
            if (isNearExistingFrequency(detected_freq_mhz, existing_freq)) {
                auto it = std::find(detectedFrequencies.begin(), detectedFrequencies.end(), existing_freq);
                if (it != detectedFrequencies.end()) {
                    *it = (existing_freq + detected_freq_mhz) / 2.0;
                }
            } else {
                detectedFrequencies.push_back(detected_freq_mhz);
                
                if (detectedFrequencies.size() > MAX_DETECTED_FREQUENCIES) {
                    detectedFrequencies.erase(detectedFrequencies.begin());
                }
            }
            
            updateDetectedFrequenciesList();
        }

        scan_current_start_index += 1;
    }

    if (scan_current_start_index + scan_window_size_bins > total_bins) {
        scan_current_start_index = 0;
    }
}

bool MainWindow::isNearExistingFrequency(double newFreq, double& existingFreq) {
    for (auto& freq : detectedFrequencies) {
        if (std::abs(newFreq - freq) <= FREQUENCY_TOLERANCE_MHZ) {
            existingFreq = freq;
            return true;
        }
    }
    return false;
}

void MainWindow::updateDetectedFrequenciesList() {
    if (!detectedFrequenciesList) {
        return;
    }
    
    detectedFrequenciesList->clear();
    
    std::vector<double> sortedFreqs = detectedFrequencies;
    std::sort(sortedFreqs.begin(), sortedFreqs.end());
    
    for (const auto& freq : sortedFreqs) {
        QString freqText = QString("%1 MHz").arg(freq, 0, 'f', 3);
        QListWidgetItem* item = new QListWidgetItem(freqText, detectedFrequenciesList);
        item->setData(Qt::UserRole, freq);
    }
}

void MainWindow::onDetectedFrequencyClicked(QListWidgetItem* item) {
    if (!item || !frequencySpinBox) {
        return;
    }
    
    double freq_mhz = item->data(Qt::UserRole).toDouble();
    
    frequencySpinBox->setValue(freq_mhz);
    
    applyConfig();
}

void MainWindow::activeFreqCleanup() {
    detectedFrequenciesList->clear();
}

void MainWindow::setupVolumeSliderConnection() {
#ifdef HAVE_QT_AUDIO
    if (volumeSlider) {
        disconnect(volumeSlider, &QSlider::valueChanged, this, nullptr);
        
        connect(volumeSlider, &QSlider::valueChanged, this,
                [this](int value) {
                    if (audioSink) {
                        float volume = value / 100.0f;
                        audioSink->setVolume(volume);
                        spdlog::debug("Volume set to: {}% ({})", value, volume);
                    } else {
                        spdlog::warn("Cannot set volume: audioSink is null");
                    }
                    if (volumeLabel) {
                        volumeLabel->setText(QString("%1%").arg(value));
                    }
                });
        
        if (audioSink) {
            int currentValue = volumeSlider->value();
            audioSink->setVolume(currentValue / 100.0f);
        }
    }
#endif
}
