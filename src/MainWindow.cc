#include "MainWindow.hpp"
#include "AnalysisParams.hpp"
#include "MainWindowConstants.hpp"
#include "AudioProcessorThread.hpp"
#include "FpgaFftProcessor.hpp"
#include "LogSink.hpp"
#include "SpectrumWorker.hpp"
#include "radio_scanner.hpp"

#include <QMetaObject>
#include <QSignalBlocker>
#include <QThread>

#ifdef HAVE_QT_AUDIO
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#endif

#include <QBrush>
#include <QColor>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QTextCursor>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace {

hackrf_alloc_params allocFromSweepPlan(const AnalysisSweepPlan &plan) {
    hackrf_alloc_params params;
    if (plan.segments.empty()) {
        return params;
    }
    const auto &seg = plan.segments.front();
    params.center_freq = seg.center_freq_hz;
    params.sample_rate = seg.sample_rate_hz;
    params.bandwidth = seg.bandwidth_hz;
    params.vga_gain = ANALYSIS_DEFAULT_VGA_GAIN;
    params.lna_gain = ANALYSIS_DEFAULT_LNA_GAIN;
    params.fft_size = plan.fft_size_per_segment;
    return params;
}

AnalysisSweepPlan defaultAnalysisSweepPlan() {
    return computeAnalysisSweepPlan(ANALYSIS_DEFAULT_FREQ_MHZ,
                                    ANALYSIS_DEFAULT_SPAN_MHZ,
                                    ANALYSIS_DEFAULT_SWEEP_FFT_SIZE);
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      centralWidget(new QWidget(this)),
      plot(new QCustomPlot(this)),
      spectrumUpdateTimer(new QTimer(this)),
      averagePowerLevelTimer(new QTimer(this)),
      scanActiveTimer(new QTimer(this)),
      activeFreqCleanupTimer(new QTimer(this)),
      analysisSweepPlan_(defaultAnalysisSweepPlan()),
      alloc_params(allocFromSweepPlan(defaultAnalysisSweepPlan())),
      fftSize(defaultAnalysisSweepPlan().total_bins),
      average_power(0.0),
      threshold(0),
      listenToggleButton(nullptr),
      volumeSlider(nullptr),
      volumeLabel(nullptr),
      listeningStatusLabel(nullptr),
      listeningFrequencyLabel(nullptr),
      detectedFrequenciesGroup(nullptr),
      detectedFrequenciesList(nullptr),
      clearDetectedButton(nullptr),
      plotModeAction(nullptr),
      viewToolBar(nullptr) {

    configure();

    this->setWindowTitle("Radio Analyser");
    info = new QGroupBox(tr("Info"));
    setupControls();
    setupInfo();
    rebuildAnalysisSweepPlan();
    alloc_params = allocFromSweepPlan(analysisSweepPlan_);
    fftSize = analysisSweepPlan_.total_bins;

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

    setupMenuBar();
    setupToolbar();

    setCentralWidget(centralWidget);
    setupLayout();

    {
        auto logSink = std::make_shared<LogSink>(this);
        spdlog::default_logger()->sinks().clear();
        spdlog::default_logger()->sinks().push_back(logSink);
    }

#ifdef HAVE_QT_AUDIO
    if (volumeSlider && audioSink) {
        int volumeValue = volumeSlider->value();
        audioSink->setVolume(volumeValue / 100.0f);
        spdlog::info("Initial volume set to: {}%", volumeValue);
    }
#endif

    setupPlot();
    updateDspMetricsInfo();
    logBaselineMetrics("startup");
    setMinimumSize(1100, 700);
    resize(1200, 800);
    try {
        device = std::make_unique<HackrfDevice>();
        if (!device->configure(alloc_params)) {
            spdlog::error("Failed to configure HackRF device.");
            QMessageBox::critical(this, "Error",
                                  "Failed to configure HackRF device.");
            return;
        }
        applyFftBackendToDevice();
        device->startRx();
    } catch (const std::exception &e) {
        spdlog::error("Error initializing HackRF: {}", e.what());
        QMessageBox::critical(
            this, "Error",
            QString("Error initializing HackRF: %1").arg(e.what()));
        return;
    }

    qRegisterMetaType<std::vector<double>>("std::vector<double>");
    spectrumWorker_ = new SpectrumWorker(this);
    spectrumWorker_->setDevice(device.get());
    spectrumWorker_->setSweepPlan(analysisSweepPlan_);
    spectrumWorker_->setSweepEnabled(true);
    spectrumThread_ = new QThread(this);
    spectrumWorker_->moveToThread(spectrumThread_);
    connect(spectrumWorker_, &SpectrumWorker::spectrumReady,
            this, &MainWindow::onSpectrumReady, Qt::QueuedConnection);
    spectrumThread_->start();

    connect(spectrumUpdateTimer, &QTimer::timeout, this,
            &MainWindow::updateSpectrum);
    spectrumUpdateTimer->start(spectrumRefreshIntervalMs());
    connect(averagePowerLevelTimer, &QTimer::timeout, this,
            &MainWindow::updateAveragePower);
    connect(scanActiveTimer, &QTimer::timeout, this, &MainWindow::scanActive);
}

MainWindow::~MainWindow() {
    spectrumUpdateTimer->stop();
    averagePowerLevelTimer->stop();
    scanActiveTimer->stop();
    if (spectrumThread_) {
        spectrumThread_->quit();
        spectrumThread_->wait();
    }
    if (device) {
        device->stopRx();
    }
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
    if (listeningActive)
        return;
    if (!spectrumWorker_)
        return;

    QMetaObject::invokeMethod(spectrumWorker_, "computeSpectrum", Qt::QueuedConnection);
}

void MainWindow::onSpectrumReady(std::vector<double> result) {
    spectrum_db = std::move(result);
    refreshSpectrumPlot();
}

void MainWindow::updateSpectrumFromIQ(const std::vector<std::complex<float>> &iq_samples) {
    if (!device || iq_samples.empty())
        return;

    spectrum_db = device->getMagnitudeSpectrumFromIQ(iq_samples);
    refreshSpectrumPlot();
}

void MainWindow::refreshSpectrumPlot() {
    if (spectrum_db.size() != static_cast<size_t>(fftSize)) {
        return;
    }

    static double last_display_span_mhz = 0.0;
    static double last_display_center_mhz = 0.0;
    static int last_fft_size = 0;

    double display_center_mhz =
        frequencySpinBox ? frequencySpinBox->value()
                         : analysisSweepPlan_.center_freq_mhz;
    double display_span_mhz = analysisSweepPlan_.requested_span_mhz;
    if (appMode_ == AppMode::Detection) {
        display_center_mhz = alloc_params.center_freq / 1e6;
        display_span_mhz = alloc_params.sample_rate / 1e6;
    }

    const double start_freq_mhz = display_center_mhz - display_span_mhz / 2.0;
    const double end_freq_mhz = display_center_mhz + display_span_mhz / 2.0;
    const double freq_resolution_mhz =
        display_span_mhz / static_cast<double>(std::max(1, fftSize));

    if (display_span_mhz != last_display_span_mhz ||
        display_center_mhz != last_display_center_mhz ||
        fftSize != last_fft_size) {
        waterfallHistory.clear();
        waterfallGridInitialized_ = false;

        x_axis_values.resize(fftSize);
        plot_x_cache_.resize(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            x_axis_values[i] = start_freq_mhz + (i * freq_resolution_mhz);
            plot_x_cache_[i] = x_axis_values[i];
        }

        plot->xAxis->setRange(start_freq_mhz, end_freq_mhz);

        last_display_span_mhz = display_span_mhz;
        last_display_center_mhz = display_center_mhz;
        last_fft_size = fftSize;
        updateDspMetricsInfo();
    }

    const int wf_bins = waterfallDisplayBins();

    if (currentPlotMode == PlotMode::Spectrum) {
        if (plot->graphCount() < 1) {
            setupPlot();
            return;
        }

        QVector<double> y(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            y[i] = spectrum_db[i];
        }

        plot->graph(0)->setData(plot_x_cache_, y, true);
        if (appMode_ == AppMode::Detection && plot->graphCount() >= 2) {
            QVector<double> y2(fftSize);
            const double threshold_line = average_power + threshold;
            y2.fill(threshold_line);
            plot->graph(1)->setData(plot_x_cache_, y2, true);
        }
        plot->replot(QCustomPlot::rpQueuedReplot);
    } else {
        if (!waterfallMap) {
            setupPlot();
            return;
        }

        if (waterfallMap && waterfallMap->data()) {
            QCPColorMapData *data = waterfallMap->data();
            if (!waterfallGridInitialized_ ||
                data->keySize() != wf_bins ||
                data->valueSize() != waterfallHistorySize) {
                data->setSize(wf_bins, waterfallHistorySize);
                data->setKeyRange(QCPRange(start_freq_mhz, end_freq_mhz));
                data->setValueRange(QCPRange(0, waterfallHistorySize));
                constexpr double emptyCellDb = -200.0;
                for (int j = 0; j < waterfallHistorySize; ++j) {
                    for (int i = 0; i < wf_bins; ++i) {
                        data->setCell(i, j, emptyCellDb);
                    }
                }
                waterfallWriteRow_ = 0;
                waterfallGridInitialized_ = true;
            }

            const double step =
                static_cast<double>(fftSize) / static_cast<double>(wf_bins);
            for (int i = 0; i < wf_bins; ++i) {
                const int src =
                    std::min(fftSize - 1, static_cast<int>(i * step));
                data->setCell(
                    i, waterfallWriteRow_,
                    spectrum_db[static_cast<size_t>(src)]);
            }
            waterfallWriteRow_ = (waterfallWriteRow_ + 1) % waterfallHistorySize;

            plot->replot(QCustomPlot::rpQueuedReplot);
        }
    }
}

void MainWindow::applyConfig() {
    if (!device) {
        QMessageBox::warning(this, "Error", "Device is not initialized.");
        return;
    }

    enforceFftBackendConstraints();
    applyFftBackendToDevice();

    if (appMode_ == AppMode::Analysis) {
        rebuildAnalysisSweepPlan();
        fftSize = analysisSweepPlan_.total_bins;
        if (spectrumWorker_) {
            spectrumWorker_->setSweepPlan(analysisSweepPlan_);
            spectrumWorker_->setSweepEnabled(true);
        }

        windowed_samples.clear();
        const double freq_res_hz = getRbwHz();
        const int bins_per_channel = std::max(
            1, static_cast<int>(std::round(DETECTOR_CHANNEL_WIDTH_HZ / freq_res_hz)));
        windowed_samples.resize(bins_per_channel);
        waterfallHistory.clear();
        waterfallGridInitialized_ = false;
        setupPlot();
        updateDspMetricsInfo();
        logBaselineMetrics("applyConfig");
        updateSpectrumRefreshInterval();
        spdlog::info("Analysis sweep plan applied ({} segments, {} bins).",
                     analysisSweepPlan_.num_segments, fftSize);
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
    if (spectrumWorker_) {
        spectrumWorker_->setSweepEnabled(false);
    }

    windowed_samples.clear();
    const double freq_res_hz = getRbwHz();
    const int bins_per_channel = std::max(
        1, static_cast<int>(std::round(DETECTOR_CHANNEL_WIDTH_HZ / freq_res_hz)));
    windowed_samples.resize(bins_per_channel);
    waterfallHistory.clear();
    waterfallGridInitialized_ = false;
    device->stopRx();
    bool success = device->configure(alloc_params);

    if (success) {
        if (appMode_ == AppMode::Detection) {
            detectedFrequencies.clear();
            updateDetectedFrequenciesList();
        }
        setupPlot();
        updateDspMetricsInfo();
        logBaselineMetrics("applyConfig");
        device->startRx();
        updateSpectrumRefreshInterval();
        spdlog::info("Configuration applied successfully.");
        if (appMode_ == AppMode::Detection) {
            QMessageBox::information(this, "Success",
                                     "Configuration applied successfully.");
        }
    } else {
        spdlog::error("Failed to apply new configuration.");
        QMessageBox::critical(this, "Error",
                              "Failed to apply new configuration.");
    }
}

void MainWindow::updateAveragePower() {
    if (!device || spectrum_db.empty())
        return;

    double linear_sum = 0.0;
    for (int i = 0; i < static_cast<int>(spectrum_db.size()); ++i) {
        linear_sum += std::pow(10.0, spectrum_db[i] / 10.0);
    }
    const double linear_avg = linear_sum / spectrum_db.size();
    average_power = 10.0 * std::log10(linear_avg + 1e-20);

    averagePowerLabel->setText(QString::number(average_power, 'f', 2));
    updateDspMetricsInfo();
}

void MainWindow::toggleDisplayMode() {
    currentPlotMode = currentPlotMode == PlotMode::Spectrum
                          ? PlotMode::Waterfall
                          : PlotMode::Spectrum;

    waterfallHistory.clear();
    waterfallGridInitialized_ = false;

    updateDisplayModeControls();
    setupPlot();
}

int MainWindow::spectrumRefreshIntervalMs() const {
    if (appMode_ == AppMode::Analysis) {
        const int segments = std::max(1, analysisSweepPlan_.num_segments);
        return std::min(500, 50 * segments);
    }

    const double sr = alloc_params.sample_rate;
    if (sr >= 15.0e6) {
        return 150;
    }
    if (sr >= 8.0e6) {
        return 100;
    }
    return 50;
}

int MainWindow::waterfallDisplayBins() const {
    if (appMode_ == AppMode::Analysis &&
        fftSize > ANALYSIS_WATERFALL_MAX_BINS) {
        return ANALYSIS_WATERFALL_MAX_BINS;
    }
    return fftSize;
}

void MainWindow::rebuildAnalysisSweepPlan() {
    enforceFftBackendConstraints();
    const int fft_per_segment =
        analysisFftBox_ ? analysisFftBox_->currentData().toInt()
                        : ANALYSIS_DEFAULT_SWEEP_FFT_SIZE;
    analysisSweepPlan_ = computeAnalysisSweepPlan(
        frequencySpinBox ? frequencySpinBox->value() : ANALYSIS_DEFAULT_FREQ_MHZ,
        analysisSpanSpinBox_ ? analysisSpanSpinBox_->value()
                             : ANALYSIS_DEFAULT_SPAN_MHZ,
        fft_per_segment);
}

bool MainWindow::isFpgaFftSelected() const {
    return fftBackendBox_ &&
           static_cast<FftBackend>(fftBackendBox_->currentData().toInt()) ==
               FftBackend::FPGA;
}

void MainWindow::applyFftBackendToDevice() {
    if (!device) {
        return;
    }
    device->setFftBackend(isFpgaFftSelected() ? FftBackend::FPGA
                                               : FftBackend::FFTW3);
}

void MainWindow::enforceFftBackendConstraints() {
    if (!isFpgaFftSelected()) {
        return;
    }

    if (analysisFftBox_ &&
        analysisFftBox_->currentData().toInt() != FpgaFftProcessor::kFftSize) {
        const QSignalBlocker blocker(analysisFftBox_);
        analysisFftBox_->setCurrentIndex(
            analysisFftBox_->findData(FpgaFftProcessor::kFftSize));
    }
    if (fftSizeBox &&
        fftSizeBox->currentData().toInt() != FpgaFftProcessor::kFftSize) {
        const QSignalBlocker blocker(fftSizeBox);
        fftSizeBox->setCurrentIndex(
            fftSizeBox->findData(FpgaFftProcessor::kFftSize));
        if (fftSizeLabel) {
            fftSizeLabel->setText(QString::number(FpgaFftProcessor::kFftSize));
        }
    }
}

void MainWindow::updateSpectrumRefreshInterval() {
    if (!spectrumUpdateTimer) {
        return;
    }
    spectrumUpdateTimer->setInterval(spectrumRefreshIntervalMs());
}

void MainWindow::applyTheme(bool dark) {
    darkTheme_ = dark;
    if (dark) {
        QPalette p;
        p.setColor(QPalette::Window, QColor(53, 53, 53));
        p.setColor(QPalette::WindowText, Qt::white);
        p.setColor(QPalette::Base, QColor(42, 42, 42));
        p.setColor(QPalette::Text, Qt::white);
        p.setColor(QPalette::Button, QColor(53, 53, 53));
        p.setColor(QPalette::ButtonText, Qt::white);
        p.setColor(QPalette::Highlight, QColor(42, 130, 218));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor(127, 127, 127));
        p.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
        p.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
        p.setColor(QPalette::ToolTipText, Qt::white);
        p.setColor(QPalette::Link, QColor(42, 130, 218));
        QApplication::setPalette(p);
        if (darkThemeAction) darkThemeAction->setChecked(true);
        if (lightThemeAction) lightThemeAction->setChecked(false);
    } else {
        QApplication::setPalette(QPalette());
        if (darkThemeAction) darkThemeAction->setChecked(false);
        if (lightThemeAction) lightThemeAction->setChecked(true);
    }
    updatePlotTheme(dark);
}

void MainWindow::setDarkTheme() {
    applyTheme(true);
}

void MainWindow::setLightTheme() {
    applyTheme(false);
}

void MainWindow::updatePlotTheme(bool dark) {
    if (!plot) return;
    if (dark) {
        plot->setBackground(QBrush(QColor(53, 53, 53)));
        plot->xAxis->setBasePen(QPen(Qt::white));
        plot->xAxis->setTickPen(QPen(Qt::white));
        plot->xAxis->setSubTickPen(QPen(Qt::white));
        plot->xAxis->setTickLabelColor(Qt::white);
        plot->xAxis->setLabelColor(Qt::white);
        plot->yAxis->setBasePen(QPen(Qt::white));
        plot->yAxis->setTickPen(QPen(Qt::white));
        plot->yAxis->setSubTickPen(QPen(Qt::white));
        plot->yAxis->setTickLabelColor(Qt::white);
        plot->yAxis->setLabelColor(Qt::white);
        if (plot->graphCount() >= 1) plot->graph(0)->setPen(QPen(QColor(100, 180, 255)));
        if (appMode_ == AppMode::Detection && plot->graphCount() >= 2) {
            plot->graph(1)->setPen(QPen(QColor(255, 100, 100), 3, Qt::DashLine));
        }
    } else {
        plot->setBackground(QBrush(Qt::white));
        plot->xAxis->setBasePen(QPen(Qt::black));
        plot->xAxis->setTickPen(QPen(Qt::black));
        plot->xAxis->setSubTickPen(QPen(Qt::black));
        plot->xAxis->setTickLabelColor(Qt::black);
        plot->xAxis->setLabelColor(Qt::black);
        plot->yAxis->setBasePen(QPen(Qt::black));
        plot->yAxis->setTickPen(QPen(Qt::black));
        plot->yAxis->setSubTickPen(QPen(Qt::black));
        plot->yAxis->setTickLabelColor(Qt::black);
        plot->yAxis->setLabelColor(Qt::black);
        if (plot->graphCount() >= 1) plot->graph(0)->setPen(QPen(Qt::blue));
        if (appMode_ == AppMode::Detection && plot->graphCount() >= 2) {
            plot->graph(1)->setPen(QPen(Qt::red, 3, Qt::DashLine));
        }
    }
    plot->replot();
}

void MainWindow::scanActive() {
    if (appMode_ != AppMode::Detection) {
        return;
    }
    if (!device || spectrum_db.empty() || x_axis_values.size() != spectrum_db.size()) {
        return;
    }

    const int total_bins = static_cast<int>(spectrum_db.size());
    const double power_threshold = average_power + threshold;

    const double freq_resolution_hz = getRbwHz();
    const double adaptive_channel_width_hz =
        std::max(0.5 * DETECTOR_CHANNEL_WIDTH_HZ, 3.0 * freq_resolution_hz);
    int smooth_half = static_cast<int>(
        std::round(0.25 * adaptive_channel_width_hz / freq_resolution_hz));
    smooth_half = std::clamp(smooth_half, 1, total_bins / 4);

    std::vector<double> smoothed(total_bins, 0.0);
    for (int i = 0; i < total_bins; ++i) {
        int lo = std::max(0, i - smooth_half);
        int hi = std::min(total_bins, i + smooth_half + 1);
        double sum = 0.0;
        for (int j = lo; j < hi; ++j) {
            sum += spectrum_db[j];
        }
        smoothed[i] = sum / (hi - lo);
    }

    struct Peak { int bin; double power; };
    std::vector<Peak> raw_peaks;
    for (int i = 1; i < total_bins - 1; ++i) {
        const double peak_power = std::max(smoothed[i], spectrum_db[static_cast<size_t>(i)]);
        if (peak_power <= power_threshold) {
            continue;
        }
        const bool smooth_local_max =
            smoothed[i] >= smoothed[i - 1] && smoothed[i] >= smoothed[i + 1];
        const bool raw_local_max =
            spectrum_db[static_cast<size_t>(i)] >= spectrum_db[static_cast<size_t>(i - 1)] &&
            spectrum_db[static_cast<size_t>(i)] >= spectrum_db[static_cast<size_t>(i + 1)];
        if (smooth_local_max || raw_local_max) {
            raw_peaks.push_back({i, peak_power});
        }
    }

    const double merge_mhz = getDetectionToleranceMHz();
    std::vector<double> new_peaks_mhz;
    for (size_t p = 0; p < raw_peaks.size(); ) {
        int best_bin = raw_peaks[p].bin;
        double best_power = raw_peaks[p].power;
        double freq_mhz = x_axis_values[best_bin];
        size_t q = p + 1;
        while (q < raw_peaks.size() &&
               std::abs(x_axis_values[raw_peaks[q].bin] - freq_mhz) <= merge_mhz) {
            if (raw_peaks[q].power > best_power) {
                best_power = raw_peaks[q].power;
                best_bin = raw_peaks[q].bin;
                freq_mhz = x_axis_values[best_bin];
            }
            ++q;
        }
        new_peaks_mhz.push_back(estimateSubBinFrequencyMHz(best_bin));
        p = q;
    }

    std::vector<double> updated_frequencies;
    updated_frequencies.reserve(new_peaks_mhz.size());
    for (double detected_freq_mhz : new_peaks_mhz) {
        double existing_freq = 0.0;
        if (isNearExistingFrequency(detected_freq_mhz, existing_freq)) {
            constexpr double kHistoryBlend = 0.8;
            updated_frequencies.push_back(
                kHistoryBlend * existing_freq + (1.0 - kHistoryBlend) * detected_freq_mhz);
        } else {
            updated_frequencies.push_back(detected_freq_mhz);
        }
    }

    std::sort(updated_frequencies.begin(), updated_frequencies.end());
    std::vector<double> deduped_frequencies;
    deduped_frequencies.reserve(updated_frequencies.size());
    for (double freq_mhz : updated_frequencies) {
        if (deduped_frequencies.empty() ||
            std::abs(freq_mhz - deduped_frequencies.back()) > merge_mhz) {
            deduped_frequencies.push_back(freq_mhz);
        } else {
            deduped_frequencies.back() =
                0.5 * (deduped_frequencies.back() + freq_mhz);
        }
    }

    if (deduped_frequencies.size() > MAX_DETECTED_FREQUENCIES) {
        deduped_frequencies.resize(MAX_DETECTED_FREQUENCIES);
    }

    detectedFrequencies = std::move(deduped_frequencies);

    updateDetectedFrequenciesList();
}

bool MainWindow::isNearExistingFrequency(double newFreq, double& existingFreq) {
    const double tolerance_mhz = getDetectionToleranceMHz();
    for (auto& freq : detectedFrequencies) {
        if (std::abs(newFreq - freq) <= tolerance_mhz) {
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
    detectedFrequencies.clear();
    updateDetectedFrequenciesList();
}

void MainWindow::appendLog(QString text) {
    if (!logTextEdit) {
        return;
    }
    logTextEdit->appendPlainText(text);
    QTextCursor c = logTextEdit->textCursor();
    c.movePosition(QTextCursor::End);
    logTextEdit->setTextCursor(c);
    QString content = logTextEdit->toPlainText();
    int lineCount = content.count('\n') + (content.isEmpty() ? 0 : 1);
    if (lineCount > maxLogLines) {
        int removeCount = lineCount - maxLogLines;
        int pos = 0;
        for (int i = 0; i < removeCount && pos < content.size(); ++i) {
            int next = content.indexOf('\n', pos);
            pos = (next >= 0) ? next + 1 : content.size();
        }
        logTextEdit->setPlainText(content.mid(pos));
    }
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

double MainWindow::getRbwHz() const {
    if (appMode_ == AppMode::Analysis && analysisSweepPlan_.total_bins > 0) {
        return analysisSweepPlan_.rbw_hz;
    }
    if (fftSize <= 0) {
        return 1.0;
    }
    return alloc_params.sample_rate / static_cast<double>(fftSize);
}

double MainWindow::getDetectionToleranceMHz() const {
    const double rbw_hz = getRbwHz();
    const double adaptive_hz = std::max(
        MIN_FREQUENCY_TOLERANCE_MHZ * 1e6,
        MERGE_WIDTH_CHANNEL_FACTOR * std::max(DETECTOR_CHANNEL_WIDTH_HZ, 6.0 * rbw_hz));
    return adaptive_hz / 1e6;
}

double MainWindow::estimateSubBinFrequencyMHz(int bin) const {
    if (bin <= 0 || bin >= fftSize - 1 ||
        spectrum_db.size() != static_cast<size_t>(fftSize) ||
        x_axis_values.size() != static_cast<size_t>(fftSize)) {
        if (bin >= 0 && bin < static_cast<int>(x_axis_values.size())) {
            return x_axis_values[static_cast<size_t>(bin)];
        }
        return alloc_params.center_freq / 1e6;
    }

    const double left = spectrum_db[static_cast<size_t>(bin - 1)];
    const double center = spectrum_db[static_cast<size_t>(bin)];
    const double right = spectrum_db[static_cast<size_t>(bin + 1)];
    const double denom = (left - 2.0 * center + right);
    double delta = 0.0;
    if (std::abs(denom) > 1e-12) {
        delta = 0.5 * (left - right) / denom;
        delta = std::clamp(delta, -0.5, 0.5);
    }

    const double rbw_mhz = getRbwHz() / 1e6;
    return x_axis_values[static_cast<size_t>(bin)] + delta * rbw_mhz;
}

void MainWindow::updateDspMetricsInfo() {
    if (!rbwLabel) {
        return;
    }

    const double rbw = getRbwHz();
    rbwLabel->setText(QString("%1").arg(rbw, 0, 'f', 1));

    if (appMode_ == AppMode::Analysis) {
        if (segmentsInfoLabel_) {
            segmentsInfoLabel_->setText(QString::number(analysisSweepPlan_.num_segments));
        }
        if (totalBinsInfoLabel_) {
            totalBinsInfoLabel_->setText(
                QString("%1 / %2")
                    .arg(analysisSweepPlan_.fft_size_per_segment)
                    .arg(analysisSweepPlan_.total_bins));
        }
        if (analysisHintLabel_ && analysisSpanSpinBox_ && frequencySpinBox) {
            const double center = frequencySpinBox->value();
            const double max_span = maxAnalysisSpanMHz(center);
            const int segments = analysisSweepPlan_.num_segments;
            const int est_ms =
                segments * (ANALYSIS_SWEEP_SETTLE_MS + 20);

            QStringList hints;
            hints << tr("Полоса собирается из %1 захватов по %2 MHz.")
                         .arg(segments)
                         .arg(ANALYSIS_SWEEP_SEGMENT_MHZ, 0, 'f', 1);
            hints << tr("Оценка времени sweep: ~%1 ms.").arg(est_ms);
            if (analysisSweepPlan_.segments_clamped) {
                hints << tr("Достигнут лимит %1 сегментов.")
                           .arg(ANALYSIS_MAX_SWEEP_SEGMENTS);
            }
            if (max_span < static_cast<double>(ANALYSIS_MAX_SWEEP_SEGMENTS) *
                               ANALYSIS_SWEEP_SEGMENT_MHZ - 1e-6) {
                hints << tr("При частоте %1 MHz макс. полоса: %2 MHz.")
                           .arg(center, 0, 'f', 3)
                           .arg(max_span, 0, 'f', 3);
            }
            if (fftSize > ANALYSIS_WATERFALL_MAX_BINS) {
                hints << tr("Waterfall: %1 bins (downsample).")
                           .arg(ANALYSIS_WATERFALL_MAX_BINS);
            }
            analysisHintLabel_->setText(hints.join('\n'));
        }
        return;
    }

    if (detectionToleranceLabel) {
        const double tol_mhz = getDetectionToleranceMHz();
        detectionToleranceLabel->setText(
            QString("%1").arg(tol_mhz * 1e3, 0, 'f', 2));
    }
}

void MainWindow::logBaselineMetrics(const char *context) const {
    const double rbw_hz = getRbwHz();
    const double tolerance_hz = getDetectionToleranceMHz() * 1e6;
    spdlog::info(
        "DSP metrics [{}]: center={} Hz, sample_rate={} Hz, fft_size={}, rbw={} Hz/bin, detection_tolerance={} Hz",
        context, alloc_params.center_freq, alloc_params.sample_rate, fftSize,
        rbw_hz, tolerance_hz);
}
