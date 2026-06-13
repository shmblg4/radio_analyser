#ifndef SPECTRUMWORKER_HPP
#define SPECTRUMWORKER_HPP

#include "AnalysisParams.hpp"
#include "radio_scanner.hpp"
#include <QObject>
#include <vector>

class SpectrumWorker : public QObject {
    Q_OBJECT

public:
    explicit SpectrumWorker(QObject *parent = nullptr);
    void setDevice(HackrfDevice *dev);
    void setSweepPlan(const AnalysisSweepPlan &plan);
    void setSweepEnabled(bool enabled);

public slots:
    void computeSpectrum();

signals:
    void spectrumReady(std::vector<double> spectrum_db);

private:
    bool ensureSweepBaseReady();
    bool captureSegmentSpectrum(std::vector<double> &segment_spectrum);

    HackrfDevice *device_ = nullptr;
    AnalysisSweepPlan sweep_plan_;
    hackrf_alloc_params sweep_rf_base_params_{};
    bool sweep_enabled_ = true;
    bool sweep_rf_base_ready_ = false;
};

#endif
