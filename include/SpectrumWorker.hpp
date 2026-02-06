#ifndef SPECTRUMWORKER_HPP
#define SPECTRUMWORKER_HPP

#include "radio_scanner.hpp"
#include <QObject>
#include <vector>

class SpectrumWorker : public QObject {
    Q_OBJECT

public:
    explicit SpectrumWorker(QObject *parent = nullptr);
    void setDevice(HackrfDevice *dev);

public slots:
    void computeSpectrum();

signals:
    void spectrumReady(std::vector<double> spectrum_db);

private:
    HackrfDevice *device_ = nullptr;
};

#endif
