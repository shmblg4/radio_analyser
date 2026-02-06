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
#include <QToolBar>
#include <QPointer>
#include <QListWidget>
#include <QPlainTextEdit>
#include <deque>
#include <memory>
#include <vector>
#include <algorithm>

class QAudioFormat;
class QAudioSink;
class AudioProcessorThread;
class SpectrumWorker;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void applyTheme(bool dark);

public slots:
    void appendLog(QString text);

private slots:
    void updateSpectrum();
    void updateSpectrumFromIQ(const std::vector<std::complex<float>> &iq_samples);
    void onSpectrumReady(std::vector<double> spectrum_db);
    void applyConfig();
    void updateAveragePower();
    void scanActive();
    void activeFreqCleanup();

private:
    enum class PlotMode { Spectrum, Waterfall };

    QWidget *centralWidget;

    PlotMode currentPlotMode = PlotMode::Spectrum;

    QCustomPlot *plot;
    QPointer<QCPColorMap> waterfallMap = nullptr;
    QPointer<QCPColorScale> waterfallColorScale = nullptr;
    void configure();
    void setupPlot();
    void setupControls();
    void setupInfo();
    void setupToolbar();
    void setupMenuBar();
    void setupLayout();
    void updateListeningParameterControls();
    void setupVolumeSliderConnection();
    void updateDisplayModeControls();
    void updateDetectedFrequenciesList();
    void updatePlotTheme(bool dark);
    bool isNearExistingFrequency(double newFreq, double& existingFreq);
    void refreshSpectrumPlot();

    std::unique_ptr<HackrfDevice> device;
    hackrf_alloc_params alloc_params;

    QTimer *spectrumUpdateTimer;
    QTimer *averagePowerLevelTimer;
    QTimer *scanActiveTimer;
    QTimer *activeFreqCleanupTimer;

    int fftSize;
    double average_power;
    int threshold;
    int scan_current_start_index;

    std::vector<double> x_axis_values;
    std::vector<double> spectrum_db;
    std::vector<double> windowed_samples;
    std::deque<QVector<double>> waterfallHistory;
    int waterfallHistorySize = 200;

    std::vector<double> detectedFrequencies;
    static constexpr double FREQUENCY_TOLERANCE_MHZ = 0.025;  // 25 кГц — ширина канала LPD
    static constexpr size_t MAX_DETECTED_FREQUENCIES = 10;

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
    QPushButton *listenToggleButton;
    QSlider *volumeSlider;
    QLabel *volumeLabel;
    QLabel *listeningStatusLabel;
    QLabel *listeningFrequencyLabel;

    QGroupBox *detectedFrequenciesGroup;
    QListWidget *detectedFrequenciesList;

    QGroupBox *info;
    QLabel *averagePowerLabel;

    QGroupBox *logGroup = nullptr;
    QPlainTextEdit *logTextEdit = nullptr;
    static constexpr int maxLogLines = 1000;

    QAction *plotModeAction = nullptr;
    QToolBar *viewToolBar = nullptr;
    QAction *darkThemeAction = nullptr;
    QAction *lightThemeAction = nullptr;
    bool darkTheme_ = true;

    bool listeningActive = false;
    QTimer *audioTimer = nullptr;
    QAudioSink *audioSink = nullptr;
    QIODevice *audioIODevice = nullptr;
    QByteArray audioBuffer;
    int audioSampleRate = 48000;
    AudioProcessorThread *audioProcessorThread = nullptr;

    SpectrumWorker *spectrumWorker_ = nullptr;
    QThread *spectrumThread_ = nullptr;

private slots:
    void setDarkTheme();
    void setLightTheme();
    void toggleListening();
    void processAudio();
    void onAudioSamplesReady(const std::vector<int16_t> &samples);
    void toggleDisplayMode();
    void updateListeningStatus();
    void onDetectedFrequencyClicked(QListWidgetItem* item);
};

#endif