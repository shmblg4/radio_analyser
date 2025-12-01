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
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QIODevice>
#include <QByteArray>
#include <map>
#include <memory>
#include <vector>

class QAudioFormat;
class QAudioSink;

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
    enum class ViewMode {
        SpectrumOverview,
        Listening
    };

    QWidget *centralWidget;

    ViewMode currentMode;

    QCustomPlot *plot;
    void configure();
    void setupPlot();
    void setupControls();
    void setupInfo();
    void setupMenu();
    void applyCurrentModeLayout();
    void updateListeningParameterControls();
    void setupVolumeSliderConnection();

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
    QWidget *controlsAndInfoWidget;
    QDoubleSpinBox *frequencySpinBox;
    QDoubleSpinBox *sampleRateSpinBox;
    QDoubleSpinBox *bandwidthSpinBox;
    QSlider *vgaSlider;
    QLabel *vgaLabel;
    QSlider *lnaSlider;
    QLabel *lnaLabel;
    QComboBox *fftSizeBox;
    QLabel *fftSizeLabel;
    QSlider *thresholdSlider;
    QLabel *thresholdLabel;
    QPushButton *applyButton;
    QPushButton *backToOverviewButton;
    QPushButton *listenToggleButton;
    QSlider *volumeSlider;

    QGroupBox *info;
    QLabel *averagePowerLabel;

    std::map<int, int> WINDOW_SIZE_BY_FFTSIZE;

    QAction *spectrumOverviewAction;
    QAction *listeningModeAction;

    bool listeningActive = false;
    QTimer *audioTimer = nullptr;
    QAudioSink *audioSink = nullptr;
    QIODevice *audioIODevice = nullptr;
    QByteArray audioBuffer;
    int audioSampleRate = 48000;

private slots:
    void setSpectrumOverviewMode();
    void setListeningMode();
    void toggleListening();
    void processAudio();
};

#endif