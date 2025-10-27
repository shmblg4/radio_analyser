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

private:
    QWidget *centralWidget;

    QCustomPlot *plot;
    void setupPlot();
    void setupControls();

    std::unique_ptr<HackrfDevice> device;
    QTimer *spectrumUpdateTimer;
    hackrf_alloc_params alloc_params;
    int fftSize;
    std::vector<double> x_axis_values;

    QGroupBox *controls;
    QSpinBox *frequencySpinBox;
    QSpinBox *sampleRateSpinBox;
    QSpinBox *bandwidthSpinBox;
    QSlider *vgaSlider;
    QLabel *vgaLabel;
    QSlider *lnaSlider;
    QLabel *lnaLabel;
    QPushButton *applyButton;
};

#endif