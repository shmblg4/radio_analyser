#include "MainWindow.hpp"
#include "MainWindowConstants.hpp"
#include <QMessageBox>

void MainWindow::configure() {
    scan_current_start_index = 0;
    WINDOW_SIZE_BY_FFTSIZE[512] = 6;
    WINDOW_SIZE_BY_FFTSIZE[1024] = 13;
    WINDOW_SIZE_BY_FFTSIZE[2048] = 26;
    WINDOW_SIZE_BY_FFTSIZE[4096] = 51;
}

void MainWindow::setupPlot() {
    plot->addGraph();
    plot->addGraph();
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
    frequencySpinBox->setValue(alloc_params.center_freq / 1e6);
    frequencySpinBox->setSuffix(" MHz");
    frequencySpinBox->setSingleStep(0.001);
    frequencySpinBox->setDecimals(3);

    sampleRateSpinBox = new QDoubleSpinBox();
    sampleRateSpinBox->setRange(MIN_SAMPLE_RATE_MHZ, MAX_SAMPLE_RATE_MHZ);
    sampleRateSpinBox->setValue(alloc_params.sample_rate / 1e6);
    sampleRateSpinBox->setSuffix(" MS/s");
    sampleRateSpinBox->setSingleStep(0.1);
    sampleRateSpinBox->setDecimals(3);

    bandwidthSpinBox = new QDoubleSpinBox();
    bandwidthSpinBox->setRange(MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ);
    bandwidthSpinBox->setValue(alloc_params.bandwidth / 1e6);
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

void MainWindow::setupMenu() {
    QMenu *viewMenu = menuBar()->addMenu(tr("Вид"));

    spectrumOverviewAction = new QAction(tr("Обзор спектра"), this);
    listeningModeAction = new QAction(tr("Прослушивание"), this);

    spectrumOverviewAction->setCheckable(true);
    listeningModeAction->setCheckable(true);

    QActionGroup *viewGroup = new QActionGroup(this);
    viewGroup->addAction(spectrumOverviewAction);
    viewGroup->addAction(listeningModeAction);
    viewGroup->setExclusive(true);

    spectrumOverviewAction->setChecked(true);

    viewMenu->addAction(spectrumOverviewAction);
    viewMenu->addAction(listeningModeAction);

    connect(spectrumOverviewAction, &QAction::triggered, this,
            &MainWindow::setSpectrumOverviewMode);
    connect(listeningModeAction, &QAction::triggered, this,
            &MainWindow::setListeningMode);
}

void MainWindow::applyCurrentModeLayout() {
    if (!centralWidget) {
        return;
    }

    if (QLayout *oldLayout = centralWidget->layout()) {
        QLayoutItem *item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->setParent(nullptr);
            }
            delete item;
        }
        delete oldLayout;
    }

    if (currentMode == ViewMode::SpectrumOverview) {
        QVBoxLayout *controlsAndInfoLayout = new QVBoxLayout;
        controlsAndInfoLayout->addWidget(controls);
        controlsAndInfoLayout->addWidget(info);
        controlsAndInfoLayout->addStretch();

        QWidget *controlsAndInfoWidget = new QWidget(this);
        controlsAndInfoWidget->setLayout(controlsAndInfoLayout);
        controlsAndInfoWidget->setFixedWidth(300);

        if (backToOverviewButton) {
            backToOverviewButton->hide();
        }

        QHBoxLayout *mainLayout = new QHBoxLayout;
        mainLayout->addWidget(plot);
        mainLayout->addWidget(controlsAndInfoWidget);
        centralWidget->setLayout(mainLayout);
    } else {
        if (!backToOverviewButton) {
            backToOverviewButton =
                new QPushButton(tr("Вернуться к обзору"), this);
            connect(backToOverviewButton, &QPushButton::clicked, this,
                    &MainWindow::setSpectrumOverviewMode);
        }

        if (!listenToggleButton) {
            listenToggleButton =
                new QPushButton(tr("Старт прослушивания"), this);
            connect(listenToggleButton, &QPushButton::clicked, this,
                    &MainWindow::toggleListening);
        }

        if (!volumeSlider) {
            volumeSlider = new QSlider(Qt::Horizontal, this);
            volumeSlider->setRange(0, 100);
            volumeSlider->setValue(50);
            setupVolumeSliderConnection();
        }

        QVBoxLayout *listeningLayout = new QVBoxLayout;
        listeningLayout->addWidget(plot, 1);
        listeningLayout->addWidget(backToOverviewButton, 0, Qt::AlignRight);

        QHBoxLayout *listenControlsLayout = new QHBoxLayout;
        listenControlsLayout->addWidget(listenToggleButton);
        listenControlsLayout->addWidget(new QLabel(tr("Громкость:"), this));
        listenControlsLayout->addWidget(volumeSlider);
        listeningLayout->addLayout(listenControlsLayout);

        listeningLayout->addWidget(controls);

        centralWidget->setLayout(listeningLayout);
        backToOverviewButton->show();

        updateListeningParameterControls();
    }
}

void MainWindow::updateListeningParameterControls() {
    bool enabled = !listeningActive;
    if (frequencySpinBox)
        frequencySpinBox->setEnabled(enabled);
    if (sampleRateSpinBox)
        sampleRateSpinBox->setEnabled(enabled);
    if (bandwidthSpinBox)
        bandwidthSpinBox->setEnabled(enabled);
    if (applyButton)
        applyButton->setEnabled(enabled);
}

