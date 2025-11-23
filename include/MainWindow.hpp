#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include "qcustomplot.h"
#include "radio_scanner.hpp"
#include <QApplication>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <map>
#include <memory>
#include <vector>
#include <QDoubleSpinBox>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateSpectrum();
    void applyConfig();
    void updateAveragePower();
    void scanActive();

private:
    QWidget *centralWidget;

    QCustomPlot *plot;
    void configure();
    void setupPlot();
    void setupControls();
    void setupInfo();

    std::unique_ptr<HackrfDevice> device;
    hackrf_alloc_params alloc_params;

    QTimer *spectrumUpdateTimer;
    QTimer *averagePowerLevelTimer;
    QTimer *scanActiveTimer;

    int fftSize;
    double average_power;
    int threshold;
    int scan_current_start_index;

    std::vector<double> x_axis_values;
    std::vector<double> spectrum_db;
    std::vector<double> windowed_samples;

    QGroupBox *controls;
    // --- ИЗМЕНЕНО: QSpinBox -> QDoubleSpinBox ---
    QDoubleSpinBox *frequencySpinBox; // Изменено
    QDoubleSpinBox *sampleRateSpinBox; // Изменено
    QDoubleSpinBox *bandwidthSpinBox; // Изменено
    // --- КОНЕЦ ИЗМЕНЕНИЯ ---
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

    std::map<int, int> WINDOW_SIZE_BY_FFTSIZE;
};

#endif