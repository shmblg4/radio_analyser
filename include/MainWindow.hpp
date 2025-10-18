#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include "qcustomplot.h"
#include "radio_scanner.hpp"
#include <QMainWindow>
#include <QTimer>
#include <memory>
#include <vector>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateSpectrum();

private:
    QCustomPlot *plot;
    void setupPlot();

    std::unique_ptr<HackrfDevice> device;
    QTimer *spectrumUpdateTimer;
    double centerFreqHz;
    double sampleRateHz;
    int fftSize;
    std::vector<double> x_axis_values;
};

#endif