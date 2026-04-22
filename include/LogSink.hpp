#ifndef LOG_SINK_H
#define LOG_SINK_H

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <mutex>
#include <spdlog/sinks/base_sink.h>

class LogSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit LogSink(QObject *target);

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override;
    void flush_() override;

private:
    QObject *target_;
    static QString levelToString(spdlog::level::level_enum level);
};

#endif
