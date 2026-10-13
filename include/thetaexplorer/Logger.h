#pragma once

#include <QFile>
#include <QDebug>
#include <QMutex>
#include <QSet>
#include <QString>

namespace thetaexplorer {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Fatal = 4,
};

class Logger
{
public:
    static Logger& instance();

    void initialize();
    void shutdown();

    bool shouldLog(LogLevel level, const char* category) const;
    QString logFilePath() const { return m_logFilePath; }
    QString configurationSummary() const;

    static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

private:
    Logger() = default;

    void loadConfiguration();
    void openLogFile();
    void rotateIfNeeded();
    static QString levelName(QtMsgType type);

    mutable QMutex m_mutex;
    QFile m_logFile;
    QString m_logFilePath;
    QSet<QString> m_enabledCategories;
    LogLevel m_minLevel = LogLevel::Debug;
    bool m_initialized = false;
    bool m_logAllCategories = true;
    qint64 m_maxLogBytes = 5 * 1024 * 1024;
};

} // namespace thetaexplorer

#define LOGD(category) \
    if (!thetaexplorer::Logger::instance().shouldLog(thetaexplorer::LogLevel::Debug, category)) ; \
    else QDebug(QtDebugMsg).noquote().nospace() << "[" << category << "] "

#define LOGI(category) \
    if (!thetaexplorer::Logger::instance().shouldLog(thetaexplorer::LogLevel::Info, category)) ; \
    else QDebug(QtInfoMsg).noquote().nospace() << "[" << category << "] "

#define LOGW(category) \
    if (!thetaexplorer::Logger::instance().shouldLog(thetaexplorer::LogLevel::Warning, category)) ; \
    else QDebug(QtWarningMsg).noquote().nospace() << "[" << category << "] "

#define LOGE(category) \
    if (!thetaexplorer::Logger::instance().shouldLog(thetaexplorer::LogLevel::Error, category)) ; \
    else QDebug(QtCriticalMsg).noquote().nospace() << "[" << category << "] "
