#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include "qcustomplot.h"
#include "radio_scanner.hpp"
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QTimer>
#include <QGroupBox>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <memory>
#include <vector>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateSpectrum();
    void applyConfig();
    void updateAveragePower();

private:
    QWidget *centralWidget;

    QCustomPlot *plot;
    void setupPlot();
    void setupControls();
    void setupInfo();

    std::unique_ptr<HackrfDevice> device;
    QTimer *spectrumUpdateTimer;
    QTimer *averagePowerLevelTimer;
    hackrf_alloc_params alloc_params;
    int fftSize;
    double average_power;
    int threshold;
    std::vector<double> x_axis_values;
    std::vector<double> spectrum_db;

    QGroupBox *controls;
    QSpinBox *frequencySpinBox;
    QSpinBox *sampleRateSpinBox;
    QSpinBox *bandwidthSpinBox;
    QSlider *vgaSlider;
    QLabel *vgaLabel;
    QSlider *lnaSlider;
    QLabel *lnaLabel;
    QComboBox *fftSizeBox;
    QLabel *fftSizeLabel;
    QSlider *thresholdSlider;
    QLabel *thresholdLabel;
    QPushButton *applyButton;

    QGroupBox *info;
    QLabel *averagePowerLabel;
};

#endif