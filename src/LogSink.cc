#include "LogSink.hpp"
#include <spdlog/details/log_msg.h>

LogSink::LogSink(QObject *target) : target_(target) {}

QString LogSink::levelToString(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:
        return QStringLiteral("[trace]");
    case spdlog::level::debug:
        return QStringLiteral("[debug]");
    case spdlog::level::info:
        return QStringLiteral("[info] ");
    case spdlog::level::warn:
        return QStringLiteral("[warn] ");
    case spdlog::level::err:
        return QStringLiteral("[error]");
    case spdlog::level::critical:
        return QStringLiteral("[critical]");
    case spdlog::level::off:
        return QStringLiteral("[off]");
    default:
        return QStringLiteral("[?]");
    }
}

void LogSink::sink_it_(const spdlog::details::log_msg &msg) {
    if (!target_) {
        return;
    }
    QString levelStr = levelToString(msg.level);
    QString payload =
        QString::fromUtf8(msg.payload.data(), static_cast<int>(msg.payload.size()));
    QString line = levelStr + " " + payload;
    QMetaObject::invokeMethod(target_, "appendLog", Qt::QueuedConnection,
                              Q_ARG(QString, line));
}

void LogSink::flush_() {}
