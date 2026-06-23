#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include "qcustomplot.h"
#include "AnalysisParams.hpp"
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
#include <QEvent>
#include <QMouseEvent>
#include <deque>
#include <memory>
#include <vector>
#include <cmath>

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
    void applyConfig(bool show_detection_success = true);
    void updateAveragePower();
    void scanActive();
    void activeFreqCleanup();

private:
    enum class AppMode { Analysis, Detection };
    enum class PlotMode { Spectrum, Waterfall };

    QWidget *centralWidget;

    AppMode appMode_ = AppMode::Analysis;
    PlotMode currentPlotMode = PlotMode::Spectrum;

    QCustomPlot *plot;
    QPointer<QCPColorMap> waterfallMap = nullptr;
    QPointer<QCPColorScale> waterfallColorScale = nullptr;
    void configure();
    void setupPlot();
    void setupControls();
    void setupToolbar();
    void setupMenuBar();
    void setupLayout();
    void updateListeningParameterControls();
    void setupVolumeSliderConnection();
    void updateDisplayModeControls();
    void updateModeControls();
    void updateAnalysisSpanRange();
    void setAppMode(AppMode mode);
    void stopListeningInternal();
    void updateDetectedFrequenciesList();
    void updatePlotTheme(bool dark);
    bool isNearExistingFrequency(double newFreq, double& existingFreq);
    void refreshSpectrumPlot();
    double getRbwHz() const;
    double getDetectionToleranceMHz() const;
    double estimateSubBinFrequencyMHz(int bin) const;
    void logBaselineMetrics(const char *context) const;
    void updateSpectrumRefreshInterval();
    int spectrumRefreshIntervalMs() const;
    int waterfallDisplayBins() const;
    void rebuildAnalysisSweepPlan();
    bool isFpgaFftSelected() const;
    void applyFftBackendToDevice();
    void enforceFftBackendConstraints();
    void clearSpectrumHold();
    bool applyIncomingSpectrum(std::vector<double> frame);
    void setupSpectrumCursor();
    void updateSpectrumCursorStyle(bool dark);
    void hideSpectrumCursor();
    void onPlotMouseMove(QMouseEvent *event);
    void onPlotMousePress(QMouseEvent *event);
    double spectrumFrequencyMHzAt(const QPoint &pos) const;

    QPointer<QCPItemStraightLine> spectrumCursorLine_ = nullptr;
    QPointer<QCPItemText> spectrumCursorLabel_ = nullptr;

    std::unique_ptr<HackrfDevice> device;

    AnalysisSweepPlan analysisSweepPlan_;
    
    QTimer *spectrumUpdateTimer;
    QTimer *averagePowerLevelTimer;
    QTimer *scanActiveTimer;
    QTimer *activeFreqCleanupTimer;
    
    hackrf_alloc_params alloc_params;

    int fftSize;
    double average_power;
    int threshold;
    int scan_current_start_index;

    std::vector<double> x_axis_values;
    QVector<double> plot_x_cache_;
    std::vector<double> spectrum_db;
    std::vector<double> last_good_spectrum_db_;
    std::vector<double> windowed_samples;
    std::deque<QVector<double>> waterfallHistory;
    int waterfallHistorySize = 200;
    bool waterfallGridInitialized_ = false;

    std::vector<double> detectedFrequencies;
    static constexpr double MIN_FREQUENCY_TOLERANCE_MHZ = 0.010;
    static constexpr double DETECTOR_CHANNEL_WIDTH_HZ = 25000.0;
    static constexpr double MERGE_WIDTH_CHANNEL_FACTOR = 1.0;
    static constexpr size_t MAX_DETECTED_FREQUENCIES = 10;

    QGroupBox *analysisControlsGroup_ = nullptr;
    QGroupBox *detectionControlsGroup_ = nullptr;
    QWidget *controlsAndInfoWidget;
    QDoubleSpinBox *frequencySpinBox;
    QDoubleSpinBox *analysisSpanSpinBox_ = nullptr;
    QComboBox *analysisFftBox_ = nullptr;
    QComboBox *fftBackendBox_ = nullptr;
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
    QDoubleSpinBox *demodOffsetSpinBox = nullptr;
    QComboBox *demodModeCombo_ = nullptr;

    QGroupBox *detectedFrequenciesGroup;
    QListWidget *detectedFrequenciesList;
    QPushButton *clearDetectedButton;

    QGroupBox *listeningInfoGroup_ = nullptr;

    QGroupBox *logGroup = nullptr;
    QPlainTextEdit *logTextEdit = nullptr;
    static constexpr int maxLogLines = 1000;

    QAction *plotModeAction = nullptr;
    QAction *analysisModeAction_ = nullptr;
    QAction *detectionModeAction_ = nullptr;
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif
