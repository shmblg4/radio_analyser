#include "MainWindow.hpp"
#include <QApplication>
#include <QDebug>
#include <QVBoxLayout>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), plot(new QCustomPlot(this)),
      spectrumUpdateTimer(new QTimer(this)), centerFreqHz(434e6),
      sampleRateHz(2e6),
      fftSize(512) {

    setCentralWidget(plot);
    setupPlot();
    resize(800, 600);
    try {
        device = std::make_unique<HackrfDevice>();
        if (!device->configure(centerFreqHz, sampleRateHz, 2500000, 16, 16)) {
            spdlog::error("Failed to configure HackRF device.");
            return;
        }
        device->startRx();
    } catch (const std::exception &e) {
        spdlog::error("Error initializing HackRF: {}", e.what());
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

    double freq_resolution = sampleRateHz / fftSize;
    double start_freq_mhz = (centerFreqHz - sampleRateHz / 2.0) / 1e6;
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

    plot->rescaleAxes();
    plot->replot();
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
    plot->rescaleAxes(true);
    plot->replot();
}