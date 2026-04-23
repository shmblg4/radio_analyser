#include "SpectrumWorker.hpp"

SpectrumWorker::SpectrumWorker(QObject *parent)
    : QObject(parent) {
}

void SpectrumWorker::setDevice(HackrfDevice *dev) {
    device_ = dev;
}

void SpectrumWorker::computeSpectrum() {
    if (!device_) {
        return;
    }
    auto iq_samples = device_->getIQSamplesForProcessing(false);
    if (iq_samples.empty()) {
        return;
    }
    std::vector<double> result = device_->getMagnitudeSpectrumFromIQ(iq_samples);
    if (!result.empty()) {
        emit spectrumReady(std::move(result));
    }
}
