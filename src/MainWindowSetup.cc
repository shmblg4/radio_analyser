#include "MainWindow.hpp"
#include "AnalysisParams.hpp"
#include "MainWindowConstants.hpp"
#include <QFontDatabase>
#include <QMessageBox>
#include <QSignalBlocker>
#include <cmath>
#include <algorithm>

void MainWindow::configure() {
    scan_current_start_index = 0;
}

void MainWindow::setupPlot() {
    plot->clearPlottables();
    plot->clearGraphs();
    plot->clearItems();

    if (waterfallColorScale) {
        if (waterfallMap) {
            waterfallMap->setColorScale(nullptr);
        }
        plot->plotLayout()->remove(waterfallColorScale);
        plot->plotLayout()->simplify();
        delete waterfallColorScale;
        waterfallColorScale = nullptr;
    }
    waterfallMap = nullptr;

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

    x_axis_values.resize(fftSize);
    plot_x_cache_.resize(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        x_axis_values[i] = start_freq_mhz + (i * freq_resolution_mhz);
        plot_x_cache_[i] = x_axis_values[i];
    }

    const int wf_bins = waterfallDisplayBins();

    if (currentPlotMode == PlotMode::Spectrum) {
        plot->addGraph();
        plot->graph(0)->setPen(QPen(Qt::blue));
        plot->xAxis->setLabel("Frequency (MHz)");
        plot->yAxis->setLabel("Amplitude (dBFS)");

        QVector<double> x(fftSize), y(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            x[i] = x_axis_values[i];
            y[i] = -200.0;
        }
        plot->graph(0)->setData(x, y);
        plot->yAxis->setRange(-100.0, 50.0);
        plot->xAxis->setRange(start_freq_mhz, end_freq_mhz);
    } else {
        waterfallMap = new QCPColorMap(plot->xAxis, plot->yAxis);
        waterfallColorScale = new QCPColorScale(plot);
        plot->plotLayout()->addElement(0, 1, waterfallColorScale);
        waterfallColorScale->setType(QCPAxis::atRight);
        waterfallMap->setColorScale(waterfallColorScale);
        waterfallMap->setInterpolate(false);
        waterfallMap->setGradient(QCPColorGradient::gpThermal);
        waterfallColorScale->setDataRange(QCPRange(-100.0, 50.0));

        QCPColorMapData *data = waterfallMap->data();
        data->setSize(wf_bins, waterfallHistorySize);
        data->setKeyRange(QCPRange(start_freq_mhz, end_freq_mhz));
        data->setValueRange(QCPRange(0, waterfallHistorySize));
        for (int j = 0; j < waterfallHistorySize; ++j) {
            for (int i = 0; i < wf_bins; ++i) {
                data->setCell(i, j, -200.0);
            }
        }

        plot->xAxis->setLabel("Frequency (MHz)");
        plot->yAxis->setLabel("Frame");
        plot->xAxis->setRange(start_freq_mhz, end_freq_mhz);
        plot->yAxis->setRange(0, waterfallHistorySize);
    }

    updatePlotTheme(darkTheme_);
    setupSpectrumCursor();
    plot->replot();
}

void MainWindow::setupControls() {
    vgaLabel = new QLabel(QString::number(alloc_params.vga_gain));
    lnaLabel = new QLabel(QString::number(alloc_params.lna_gain));
    fftSizeLabel = new QLabel(QString::number(fftSize));
    thresholdLabel = new QLabel(QString::number(threshold));

    frequencySpinBox = new QDoubleSpinBox();
    frequencySpinBox->setRange(ANALYSIS_MIN_FREQ_MHZ, ANALYSIS_MAX_FREQ_MHZ);
    frequencySpinBox->setValue(ANALYSIS_DEFAULT_FREQ_MHZ);
    frequencySpinBox->setSuffix(" MHz");
    frequencySpinBox->setSingleStep(0.001);
    frequencySpinBox->setDecimals(3);

    analysisSpanSpinBox_ = new QDoubleSpinBox();
    analysisSpanSpinBox_->setRange(ANALYSIS_MIN_SPAN_MHZ,
                                   maxAnalysisSpanMHz(ANALYSIS_DEFAULT_FREQ_MHZ));
    analysisSpanSpinBox_->setValue(ANALYSIS_DEFAULT_SPAN_MHZ);
    analysisSpanSpinBox_->setSuffix(" MHz");
    analysisSpanSpinBox_->setSingleStep(0.1);
    analysisSpanSpinBox_->setDecimals(3);
    connect(frequencySpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { updateAnalysisSpanRange(); });

    analysisFftBox_ = new QComboBox();
    analysisFftBox_->addItem("512", 512);
    analysisFftBox_->addItem("1024", 1024);
    analysisFftBox_->setCurrentIndex(
        analysisFftBox_->findData(ANALYSIS_DEFAULT_SWEEP_FFT_SIZE));

    fftBackendBox_ = new QComboBox();
    fftBackendBox_->addItem("FFTW3", static_cast<int>(FftBackend::FFTW3));
    fftBackendBox_->addItem("FPGA FFT", static_cast<int>(FftBackend::FPGA));
    fftBackendBox_->setCurrentIndex(0);
    connect(fftBackendBox_,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        enforceFftBackendConstraints();
        applyFftBackendToDevice();
    });

    sampleRateSpinBox = new QDoubleSpinBox();
    sampleRateSpinBox->setRange(MIN_SAMPLE_RATE_MHZ, MAX_SAMPLE_RATE_MHZ);
    sampleRateSpinBox->setValue(DETECTION_DEFAULT_SAMPLE_RATE_MHZ);
    sampleRateSpinBox->setSuffix(" MS/s");
    sampleRateSpinBox->setSingleStep(0.1);
    sampleRateSpinBox->setDecimals(3);

    bandwidthSpinBox = new QDoubleSpinBox();
    bandwidthSpinBox->setRange(MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ);
    bandwidthSpinBox->setValue(DETECTION_DEFAULT_BANDWIDTH_MHZ);
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
    fftSizeBox->setCurrentIndex(fftSizeBox->findData(DETECTION_DEFAULT_FFT_SIZE));

    applyButton = new QPushButton(tr("Apply"));
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyConfig);

    analysisControlsGroup_ = new QGroupBox(tr("Анализ"));
    QVBoxLayout *analysisLayout = new QVBoxLayout;
    analysisLayout->addWidget(new QLabel(tr("Ширина полосы:")));
    analysisLayout->addWidget(analysisSpanSpinBox_);
    analysisLayout->addWidget(new QLabel(tr("FFT на сегмент:")));
    analysisLayout->addWidget(analysisFftBox_);
    analysisLayout->addWidget(applyButton);
    analysisLayout->addStretch();
    analysisControlsGroup_->setLayout(analysisLayout);

    detectionControlsGroup_ = new QGroupBox(tr("Детектирование"));
    QVBoxLayout *detectionLayout = new QVBoxLayout;
    detectionLayout->addWidget(new QLabel(tr("Sample Rate (MS/s):")));
    detectionLayout->addWidget(sampleRateSpinBox);
    detectionLayout->addWidget(new QLabel(tr("Bandwidth (MHz):")));
    detectionLayout->addWidget(bandwidthSpinBox);

    QHBoxLayout *vgaLayout = new QHBoxLayout;
    vgaLayout->addWidget(vgaSlider);
    vgaLayout->addWidget(vgaLabel);
    detectionLayout->addWidget(new QLabel(tr("VGA Gain:")));
    detectionLayout->addLayout(vgaLayout);

    QHBoxLayout *lnaLayout = new QHBoxLayout;
    lnaLayout->addWidget(lnaSlider);
    lnaLayout->addWidget(lnaLabel);
    detectionLayout->addWidget(new QLabel(tr("LNA Gain:")));
    detectionLayout->addLayout(lnaLayout);

    QHBoxLayout *fftLayout = new QHBoxLayout;
    fftLayout->addWidget(fftSizeBox);
    fftLayout->addWidget(fftSizeLabel);
    detectionLayout->addWidget(new QLabel(tr("FFT Size:")));
    detectionLayout->addLayout(fftLayout);

    QHBoxLayout *thresholdLayout = new QHBoxLayout;
    thresholdLayout->addWidget(thresholdSlider);
    thresholdLayout->addWidget(thresholdLabel);
    detectionLayout->addWidget(new QLabel(tr("Threshold:")));
    detectionLayout->addLayout(thresholdLayout);

    QPushButton *detectionApplyButton = new QPushButton(tr("Apply"));
    connect(detectionApplyButton, &QPushButton::clicked, this, &MainWindow::applyConfig);
    detectionLayout->addWidget(detectionApplyButton);
    detectionLayout->addStretch();
    detectionControlsGroup_->setLayout(detectionLayout);
}

void MainWindow::setupMenuBar() {
    QMenu *viewMenu = menuBar()->addMenu(tr("Вид"));
    darkThemeAction = viewMenu->addAction(tr("Тёмная тема"));
    darkThemeAction->setCheckable(true);
    darkThemeAction->setChecked(true);
    connect(darkThemeAction, &QAction::triggered, this, &MainWindow::setDarkTheme);
    lightThemeAction = viewMenu->addAction(tr("Светлая тема"));
    lightThemeAction->setCheckable(true);
    lightThemeAction->setChecked(false);
    connect(lightThemeAction, &QAction::triggered, this, &MainWindow::setLightTheme);
}

void MainWindow::setupToolbar() {
    if (!viewToolBar) {
        viewToolBar = addToolBar(tr("График"));
        viewToolBar->setMovable(false);
    }

    if (!analysisModeAction_) {
        auto *modeGroup = new QActionGroup(this);
        modeGroup->setExclusive(true);

        analysisModeAction_ = viewToolBar->addAction(tr("Анализ"));
        analysisModeAction_->setCheckable(true);
        analysisModeAction_->setChecked(true);
        modeGroup->addAction(analysisModeAction_);
        connect(analysisModeAction_, &QAction::triggered, this, [this]() {
            setAppMode(AppMode::Analysis);
        });

        detectionModeAction_ = viewToolBar->addAction(tr("Детектирование"));
        detectionModeAction_->setCheckable(true);
        modeGroup->addAction(detectionModeAction_);
        connect(detectionModeAction_, &QAction::triggered, this, [this]() {
            setAppMode(AppMode::Detection);
        });

        viewToolBar->addSeparator();
    }

    if (!plotModeAction) {
        plotModeAction = viewToolBar->addAction(tr("Режим: Спектр"));
        plotModeAction->setCheckable(true);
        connect(plotModeAction, &QAction::triggered, this,
                &MainWindow::toggleDisplayMode);
    }

    updateDisplayModeControls();
}

void MainWindow::setupLayout() {
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

    if (!volumeLabel) {
        volumeLabel = new QLabel("50%", this);
        volumeLabel->setMinimumWidth(50);
        volumeLabel->setAlignment(Qt::AlignCenter);
    }

    if (!listeningStatusLabel) {
        listeningStatusLabel = new QLabel(tr("Статус: Остановлено"), this);
        listeningStatusLabel->setStyleSheet("font-weight: bold; color: #d32f2f;");
    }

    if (!listeningFrequencyLabel) {
        listeningFrequencyLabel = new QLabel(tr("Частота: -- MHz"), this);
    }

    if (!listeningInfoGroup_) {
        listeningInfoGroup_ = new QGroupBox(tr("Прослушивание"), this);
        QVBoxLayout *listeningInfoLayout = new QVBoxLayout;

        listeningInfoLayout->addWidget(listeningStatusLabel);
        listeningInfoLayout->addWidget(listeningFrequencyLabel);
        listeningInfoLayout->addWidget(listenToggleButton);
        listeningInfoLayout->addWidget(new QLabel(tr("Громкость:"), this));
        QHBoxLayout *volumeLayout = new QHBoxLayout;
        volumeLayout->addWidget(volumeSlider);
        volumeLayout->addWidget(volumeLabel);
        listeningInfoLayout->addLayout(volumeLayout);

        if (!demodOffsetSpinBox) {
            demodOffsetSpinBox = new QDoubleSpinBox(this);
            demodOffsetSpinBox->setRange(-25000.0, 25000.0);
            demodOffsetSpinBox->setValue(12000.0);
            demodOffsetSpinBox->setSingleStep(100.0);
            demodOffsetSpinBox->setDecimals(0);
            demodOffsetSpinBox->setSuffix(QStringLiteral(" Hz"));
            connect(demodOffsetSpinBox,
                    static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                    this, [this](double value_hz) {
                if (!listeningActive || !device || !frequencySpinBox) {
                    return;
                }

                const double desired_center_hz = frequencySpinBox->value() * 1e6;
                const double tuned_center_hz =
                    std::max(0.0, desired_center_hz + value_hz);
                const uint64_t new_center_freq =
                    static_cast<uint64_t>(std::llround(tuned_center_hz));
                if (new_center_freq == alloc_params.center_freq) {
                    return;
                }

                alloc_params.center_freq = new_center_freq;
                device->stopRx();
                if (!device->configure(alloc_params) || !device->startRx()) {
                    listeningActive = false;
                    if (listenToggleButton) {
                        listenToggleButton->setText(tr("Старт прослушивания"));
                    }
                    if (audioTimer) {
                        audioTimer->stop();
                    }
                    updateListeningParameterControls();
                    updateListeningStatus();
                    QMessageBox::critical(this, tr("Ошибка"),
                                          tr("Не удалось применить offset tuning."));
                    return;
                }

                setupPlot();
                updateListeningStatus();
            });
        }
        listeningInfoLayout->addWidget(
            new QLabel(tr("Уход от нуля (IF offset):"), this));
        listeningInfoLayout->addWidget(demodOffsetSpinBox);
        listeningInfoLayout->addStretch();
        listeningInfoGroup_->setLayout(listeningInfoLayout);
    }

    if (!detectedFrequenciesGroup) {
        detectedFrequenciesGroup = new QGroupBox(tr("Задетектированные частоты"), this);
        QVBoxLayout *detectedFreqLayout = new QVBoxLayout;
        
        if (!detectedFrequenciesList) {
            detectedFrequenciesList = new QListWidget(this);
            detectedFrequenciesList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            connect(detectedFrequenciesList, &QListWidget::itemClicked, this,
                    &MainWindow::onDetectedFrequencyClicked);
        }
        
        detectedFreqLayout->addWidget(detectedFrequenciesList);
        if (!clearDetectedButton) {
            clearDetectedButton = new QPushButton(tr("Очистить список"), this);
            connect(clearDetectedButton, &QPushButton::clicked, this, &MainWindow::activeFreqCleanup);
        }
        detectedFreqLayout->addWidget(clearDetectedButton);
        detectedFrequenciesGroup->setLayout(detectedFreqLayout);
    }

    if (!logGroup) {
        logGroup = new QGroupBox(tr("Лог"), this);
        logTextEdit = new QPlainTextEdit(this);
        logTextEdit->setReadOnly(true);
        logTextEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        logTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        QVBoxLayout *logLayout = new QVBoxLayout;
        logLayout->addWidget(logTextEdit);
        logGroup->setLayout(logLayout);
    }

    QVBoxLayout *configColumnLayout = new QVBoxLayout;
    configColumnLayout->addWidget(new QLabel(tr("Центральная частота:"), this));
    configColumnLayout->addWidget(frequencySpinBox);
    configColumnLayout->addWidget(new QLabel(tr("FFT backend:"), this));
    configColumnLayout->addWidget(fftBackendBox_);
    configColumnLayout->addWidget(analysisControlsGroup_);
    configColumnLayout->addWidget(detectionControlsGroup_);
    configColumnLayout->addWidget(listeningInfoGroup_);
    configColumnLayout->addStretch();

    QWidget *configColumnWidget = new QWidget(this);
    configColumnWidget->setLayout(configColumnLayout);
    configColumnWidget->setFixedWidth(300);

    QHBoxLayout *topRowLayout = new QHBoxLayout;
    topRowLayout->addWidget(plot, 1);
    topRowLayout->addWidget(configColumnWidget);

    QWidget *topRowWidget = new QWidget(this);
    topRowWidget->setLayout(topRowLayout);

    QHBoxLayout *bottomRowLayout = new QHBoxLayout;
    bottomRowLayout->addWidget(detectedFrequenciesGroup, 1);
    bottomRowLayout->addWidget(logGroup, 1);

    QWidget *bottomRowWidget = new QWidget(this);
    bottomRowWidget->setLayout(bottomRowLayout);
    bottomRowWidget->setMaximumHeight(220);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(topRowWidget, 1);
    mainLayout->addWidget(bottomRowWidget);

    centralWidget->setLayout(mainLayout);

    updateModeControls();
    updateListeningParameterControls();
    updateListeningStatus();
}

void MainWindow::setAppMode(AppMode mode) {
    if (appMode_ == mode) {
        return;
    }

    if (listeningActive) {
        stopListeningInternal();
    }

    appMode_ = mode;

    if (mode == AppMode::Detection) {
        frequencySpinBox->setValue(DETECTION_DEFAULT_FREQ_MHZ);
    } else {
        frequencySpinBox->setValue(ANALYSIS_DEFAULT_FREQ_MHZ);
    }

    if (analysisModeAction_) {
        const QSignalBlocker blocker(analysisModeAction_);
        analysisModeAction_->setChecked(mode == AppMode::Analysis);
    }
    if (detectionModeAction_) {
        const QSignalBlocker blocker(detectionModeAction_);
        detectionModeAction_->setChecked(mode == AppMode::Detection);
    }

    updateModeControls();

    if (scanActiveTimer) {
        if (mode == AppMode::Detection) {
            scanActiveTimer->start(500);
        } else {
            scanActiveTimer->stop();
            detectedFrequencies.clear();
            updateDetectedFrequenciesList();
        }
    }
    if (averagePowerLevelTimer) {
        if (mode == AppMode::Detection) {
            averagePowerLevelTimer->start(100);
        } else {
            averagePowerLevelTimer->stop();
        }
    }

    applyConfig();
    setupPlot();
}

void MainWindow::updateModeControls() {
    const bool analysis = appMode_ == AppMode::Analysis;

    if (analysisControlsGroup_) {
        analysisControlsGroup_->setVisible(analysis);
    }
    if (detectionControlsGroup_) {
        detectionControlsGroup_->setVisible(!analysis);
    }
    if (listeningInfoGroup_) {
        listeningInfoGroup_->setVisible(!analysis);
    }
    if (detectedFrequenciesGroup) {
        detectedFrequenciesGroup->setVisible(!analysis);
    }

    if (frequencySpinBox) {
        if (analysis) {
            frequencySpinBox->setRange(ANALYSIS_MIN_FREQ_MHZ, ANALYSIS_MAX_FREQ_MHZ);
        } else {
            frequencySpinBox->setRange(DETECTION_MIN_FREQ_MHZ, DETECTION_MAX_FREQ_MHZ);
        }
        frequencySpinBox->setDecimals(3);
        frequencySpinBox->setSingleStep(0.001);
    }

    if (analysis) {
        updateAnalysisSpanRange();
    }

    enforceFftBackendConstraints();
    updateListeningParameterControls();
}

void MainWindow::updateAnalysisSpanRange() {
    if (!analysisSpanSpinBox_ || !frequencySpinBox ||
        appMode_ != AppMode::Analysis) {
        return;
    }

    const double max_span = maxAnalysisSpanMHz(frequencySpinBox->value());
    analysisSpanSpinBox_->setMaximum(max_span);
    if (analysisSpanSpinBox_->value() > max_span) {
        analysisSpanSpinBox_->setValue(max_span);
    }
    if (analysisSpanSpinBox_->value() < ANALYSIS_MIN_SPAN_MHZ) {
        analysisSpanSpinBox_->setValue(ANALYSIS_MIN_SPAN_MHZ);
    }
}

void MainWindow::updateDisplayModeControls() {
    const QString text = currentPlotMode == PlotMode::Spectrum
                             ? tr("Режим: Спектр")
                             : tr("Режим: Waterfall");

    if (plotModeAction) {
        const QSignalBlocker blocker(plotModeAction);
        plotModeAction->setText(text);
        plotModeAction->setChecked(currentPlotMode == PlotMode::Waterfall);
    }
}

void MainWindow::updateListeningParameterControls() {
    bool enabled = !listeningActive;
    if (frequencySpinBox) {
        frequencySpinBox->setEnabled(enabled);
    }
    if (analysisSpanSpinBox_) {
        analysisSpanSpinBox_->setEnabled(enabled);
    }
    if (analysisFftBox_) {
        analysisFftBox_->setEnabled(enabled && !isFpgaFftSelected());
    }
    if (sampleRateSpinBox) {
        sampleRateSpinBox->setEnabled(enabled);
    }
    if (bandwidthSpinBox) {
        bandwidthSpinBox->setEnabled(enabled);
    }
    if (applyButton) {
        applyButton->setEnabled(enabled);
    }
    if (fftSizeBox) {
        fftSizeBox->setEnabled(enabled && !isFpgaFftSelected());
    }
    if (fftBackendBox_) {
        fftBackendBox_->setEnabled(enabled);
    }
}
