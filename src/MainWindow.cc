#include "MainWindow.hpp"
#include "radio_scanner.hpp"

#define LNA_MIN 0
#define LNA_MAX 40
#define LNA_STEP 8

#define VGA_MIN 0
#define VGA_MAX 62
#define VGA_STEP 2

const double MIN_SAMPLE_RATE_MHZ = 2.0;
const double MAX_SAMPLE_RATE_MHZ = 20.0;
const double DEFAULT_SAMPLE_RATE_MHZ = 2.0;

const double MIN_BANDWIDTH_MHZ = 1.0;
const double MAX_BANDWIDTH_MHZ = 20.0;
const double DEFAULT_BANDWIDTH_MHZ = 2.0;

const double MIN_FREQ_MHZ = 400.0;
const double MAX_FREQ_MHZ = 450.0;

const int THRESHOLD_STEP = 1;
const int THRESHOLD_MIN = -100;
const int THRESHOLD_MAX = 50;

void MainWindow::configure() {
    scan_current_start_index = 0;
    WINDOW_SIZE_BY_FFTSIZE[512] = 6;
    WINDOW_SIZE_BY_FFTSIZE[1024] = 13;
    WINDOW_SIZE_BY_FFTSIZE[2048] = 26;
    WINDOW_SIZE_BY_FFTSIZE[4096] = 51;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), plot(new QCustomPlot(this)),
      spectrumUpdateTimer(new QTimer(this)),
      averagePowerLevelTimer(new QTimer(this)),
      scanActiveTimer(new QTimer(this)),
      alloc_params({static_cast<uint64_t>(434.125 * 1e6),
                    static_cast<uint64_t>(4.8 * 1e6),
                    static_cast<uint32_t>(2.0 * 1e6), 0, 0, 1024}),
      fftSize(1024),
      // --- КОНЕЦ ИЗМЕНЕНИЯ ---
      average_power(0.0), threshold(0) {

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

    QVBoxLayout *controlsAndInfoLayout = new QVBoxLayout;
    controlsAndInfoLayout->addWidget(controls);
    controlsAndInfoLayout->addWidget(info);
    controlsAndInfoLayout->addStretch();
    QWidget *controlsAndInfoWidget = new QWidget();
    controlsAndInfoWidget->setLayout(controlsAndInfoLayout);
    controlsAndInfoWidget->setFixedWidth(300);

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(plot);
    mainLayout->addWidget(controlsAndInfoWidget);

    QWidget *centralWidget = new QWidget();
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

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
    scanActiveTimer->start(100);
    // scanActiveTimer->stop(); // временно
}

MainWindow::~MainWindow() {
    if (device) {
        device->stopRx();
    }
    spectrumUpdateTimer->stop();
    averagePowerLevelTimer->stop();
    scanActiveTimer->stop();
}

void MainWindow::setupPlot() {
    plot->addGraph(); // Spectrum
    plot->addGraph(); // Average Power Level
    plot->graph(0)->setPen(QPen(Qt::blue));
    plot->graph(1)->setPen(QPen(Qt::red, 3, Qt::DashLine));
    plot->xAxis->setLabel("Frequency (MHz)");
    plot->yAxis->setLabel("Amplitude (dB)");

    double freq_resolution_hz = alloc_params.sample_rate / fftSize;
    double start_freq_hz =
        (alloc_params.center_freq - alloc_params.sample_rate / 2.0);
    double end_freq_hz =
        (alloc_params.center_freq + alloc_params.sample_rate / 2.0);
    double freq_resolution_mhz = freq_resolution_hz / 1e6;
    double start_freq_mhz = start_freq_hz / 1e6;
    double end_freq_mhz = end_freq_hz / 1e6;

    x_axis_values.resize(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x_axis_values[i] = start_freq_mhz + (i * freq_resolution_mhz);
    }

    QVector<double> x(fftSize), y(fftSize), y2(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x[i] = x_axis_values[i];
        y[i] = -200.0;
        y2[i] = 0.0;
    }
    plot->graph(0)->setData(x, y);
    plot->graph(1)->setData(x, y2);
    plot->yAxis->setRange(-100.0, 50.0);
    plot->xAxis->setRange(start_freq_mhz, end_freq_mhz);
    plot->replot();
}

void MainWindow::setupControls() {
    vgaLabel = new QLabel(QString::number(alloc_params.vga_gain));
    lnaLabel = new QLabel(QString::number(alloc_params.lna_gain));
    fftSizeLabel = new QLabel(QString::number(fftSize));
    thresholdLabel = new QLabel(QString::number(threshold));

    frequencySpinBox = new QDoubleSpinBox();
    frequencySpinBox->setRange(MIN_FREQ_MHZ, MAX_FREQ_MHZ);
    frequencySpinBox->setValue(alloc_params.center_freq /
                               1e6);
    frequencySpinBox->setSuffix(" MHz");
    frequencySpinBox->setSingleStep(0.001);
    frequencySpinBox->setDecimals(3);

    sampleRateSpinBox = new QDoubleSpinBox();
    sampleRateSpinBox->setRange(MIN_SAMPLE_RATE_MHZ, MAX_SAMPLE_RATE_MHZ);
    sampleRateSpinBox->setValue(alloc_params.sample_rate /
                                1e6);
    sampleRateSpinBox->setSuffix(" MS/s");
    sampleRateSpinBox->setSingleStep(0.1);
    sampleRateSpinBox->setDecimals(3);

    bandwidthSpinBox = new QDoubleSpinBox();
    bandwidthSpinBox->setRange(MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ);
    bandwidthSpinBox->setValue(alloc_params.bandwidth /
                               1e6);
    bandwidthSpinBox->setSuffix(" MHz");
    bandwidthSpinBox->setSingleStep(0.1);
    bandwidthSpinBox->setDecimals(3);

    vgaSlider = new QSlider(Qt::Horizontal);
    vgaSlider->setRange(VGA_MIN, VGA_MAX);
    vgaSlider->setValue(alloc_params.vga_gain);
    vgaSlider->setSingleStep(VGA_STEP);
    vgaSlider->setPageStep(VGA_STEP);
    connect(vgaSlider, &QSlider::sliderMoved, this, [this](int value) {
        int rounded_value = ((value + VGA_STEP / 2) / VGA_STEP) * VGA_STEP;
        rounded_value = qBound(VGA_MIN, rounded_value, VGA_MAX);
        vgaSlider->setValue(rounded_value);
    });
    connect(vgaSlider, &QSlider::valueChanged, this,
            [this](int value) { vgaLabel->setText(QString::number(value)); });

    lnaSlider = new QSlider(Qt::Horizontal);
    lnaSlider->setRange(LNA_MIN, LNA_MAX);
    lnaSlider->setValue(alloc_params.lna_gain);
    lnaSlider->setSingleStep(LNA_STEP);
    lnaSlider->setPageStep(LNA_STEP);
    connect(lnaSlider, &QSlider::sliderMoved, this, [this](int value) {
        int rounded_value = ((value + LNA_STEP / 2) / LNA_STEP) * LNA_STEP;
        rounded_value = qBound(LNA_MIN, rounded_value, LNA_MAX);
        lnaSlider->setValue(rounded_value);
    });
    connect(lnaSlider, &QSlider::valueChanged, this,
            [this](int value) { lnaLabel->setText(QString::number(value)); });

    thresholdSlider = new QSlider(Qt::Horizontal);
    thresholdSlider->setRange(THRESHOLD_MIN, THRESHOLD_MAX);
    thresholdSlider->setValue(threshold);
    thresholdSlider->setSingleStep(THRESHOLD_STEP);
    thresholdSlider->setPageStep(THRESHOLD_STEP);
    connect(thresholdSlider, &QSlider::sliderMoved, this, [this](int value) {
        int rounded_value =
            ((value + THRESHOLD_STEP / 2) / THRESHOLD_STEP) * THRESHOLD_STEP;
        rounded_value = qBound(THRESHOLD_MIN, rounded_value, THRESHOLD_MAX);
        thresholdSlider->setValue(rounded_value);
    });
    connect(thresholdSlider, &QSlider::valueChanged, this, [this](int value) {
        thresholdLabel->setText(QString::number(value));
        threshold = value;
    });

    fftSizeBox = new QComboBox();
    fftSizeBox->addItem("512", 512);
    fftSizeBox->addItem("1024", 1024);
    fftSizeBox->addItem("2048", 2048);
    fftSizeBox->addItem("4096", 4096);
    fftSizeBox->setCurrentIndex(fftSizeBox->findData(fftSize));

    applyButton = new QPushButton(tr("Apply"));
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyConfig);
}

void MainWindow::setupInfo() {
    averagePowerLabel = new QLabel(QString::number(average_power));
}

void MainWindow::updateSpectrum() {
    if (!device)
        return;

    spectrum_db = device->getMagnitudeSpectrum();

    if (spectrum_db.size() != static_cast<size_t>(fftSize)) {
        return;
    }

    QVector<double> x(fftSize), y(fftSize), y2(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x[i] = x_axis_values[i];
        y[i] = spectrum_db[i];
        y2[i] = average_power / 2 + threshold;
    }

    plot->graph(0)->setData(x, y);
    plot->graph(1)->setData(x, y2);
    plot->replot();
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
            spdlog::info("Activity detected in frequency {}",
                         x_axis_values[end_idx - start_idx / 2]);
        }

        scan_current_start_index += 1;
    }

    if (scan_current_start_index + scan_window_size_bins > total_bins) {
        scan_current_start_index = 0;
    }
}