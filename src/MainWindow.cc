#include "MainWindow.hpp"
#include "radio_scanner.hpp"

#define LNA_MIN 0
#define LNA_MAX 40
#define LNA_STEP 8

#define VGA_MIN 0
#define VGA_MAX 62
#define VGA_STEP 2

const double MIN_SAMPLE_RATE = 2e6;
const double MAX_SAMPLE_RATE = 20e6;
const double DEFAULT_SAMPLE_RATE = 2e6;

const double MIN_BANDWIDTH = 1e6;
const double MAX_BANDWIDTH = 20e6;
const double DEFAULT_BANDWIDTH = 2e6;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), plot(new QCustomPlot(this)),
      spectrumUpdateTimer(new QTimer(this)),
      alloc_params({434000000, 2400000, 2000000, 16, 16}), fftSize(1024) {

    this->setWindowTitle("Radio Scanner");
    controls = new QGroupBox(tr("HackRF Configuration"));
    setupControls();

    QVBoxLayout *controlLayout = new QVBoxLayout;
    controlLayout->addWidget(new QLabel(tr("Center Frequency (Hz):")));
    controlLayout->addWidget(frequencySpinBox);

    controlLayout->addWidget(new QLabel(tr("Sample Rate (Hz):")));
    controlLayout->addWidget(sampleRateSpinBox);

    controlLayout->addWidget(new QLabel(tr("Bandwidth (Hz):")));
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

    controlLayout->addWidget(applyButton);
    controlLayout->addStretch();
    controls->setLayout(controlLayout);
    controls->setFixedWidth(300);

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(plot);
    mainLayout->addWidget(controls);

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
}

MainWindow::~MainWindow() {
    if (device) {
        device->stopRx();
    }
    spectrumUpdateTimer->stop();
}

void MainWindow::setupPlot() {
    plot->addGraph();
    plot->graph(0)->setPen(QPen(Qt::blue));
    plot->xAxis->setLabel("Frequency (MHz)");
    plot->yAxis->setLabel("Amplitude (dB)");

    double freq_resolution = alloc_params.sample_rate / fftSize;
    double start_freq_mhz =
        (alloc_params.center_freq - alloc_params.sample_rate / 2.0) / 1e6;
    double end_freq_mhz =
        (alloc_params.center_freq + alloc_params.sample_rate / 2.0) / 1e6;
    x_axis_values.resize(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x_axis_values[i] = start_freq_mhz + (i * freq_resolution) / 1e6;
    }

    QVector<double> x(fftSize), y(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x[i] = x_axis_values[i];
        y[i] = -200.0;
    }
    plot->graph(0)->setData(x, y);
    plot->yAxis->setRange(-100.0, 50.0);
    plot->xAxis->setRange(start_freq_mhz, end_freq_mhz);
    plot->replot();
}

void MainWindow::setupControls() {
    vgaLabel = new QLabel(QString::number(alloc_params.vga_gain));
    lnaLabel = new QLabel(QString::number(alloc_params.lna_gain));
    fftSizeLabel = new QLabel(QString::number(fftSize));

    frequencySpinBox = new QSpinBox();
    frequencySpinBox->setRange(400000000, 450000000);
    frequencySpinBox->setValue(alloc_params.center_freq);

    sampleRateSpinBox = new QSpinBox();
    sampleRateSpinBox->setRange(MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
    sampleRateSpinBox->setValue(alloc_params.sample_rate);

    bandwidthSpinBox = new QSpinBox();
    bandwidthSpinBox->setRange(MIN_BANDWIDTH, MAX_BANDWIDTH);
    bandwidthSpinBox->setValue(alloc_params.bandwidth);
    bandwidthSpinBox->setSuffix(" Hz");

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

    fftSizeBox = new QComboBox();
    fftSizeBox->addItem("512", 512);
    fftSizeBox->addItem("1024", 1024);
    fftSizeBox->addItem("2048", 2048);
    fftSizeBox->addItem("4096", 4096);
    fftSizeBox->setCurrentIndex(fftSizeBox->findText(
        QString::number(fftSize)));

    applyButton = new QPushButton(tr("Apply"));
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyConfig);
}

void MainWindow::updateSpectrum() {
    if (!device)
        return;

    std::vector<double> spectrum_db = device->getMagnitudeSpectrum(fftSize);

    if (spectrum_db.size() != static_cast<size_t>(fftSize)) {
        return;
    }

    QVector<double> x(fftSize), y(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x[i] = x_axis_values[i];
        y[i] = spectrum_db[i];
    }

    plot->graph(0)->setData(x, y);
    plot->replot();
}

void MainWindow::applyConfig() {
    if (!device) {
        QMessageBox::warning(this, "Error", "Device is not initialized.");
        return;
    }

    alloc_params.center_freq = frequencySpinBox->value();
    alloc_params.sample_rate = sampleRateSpinBox->value();
    alloc_params.bandwidth = bandwidthSpinBox->value();
    alloc_params.vga_gain = vgaSlider->value();
    alloc_params.lna_gain = lnaSlider->value();
    fftSize = fftSizeBox->currentData().toInt();
    fftSizeLabel->setText(QString::number(fftSize));
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