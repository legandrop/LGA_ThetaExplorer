#include "thetaexplorer/Logger.h"
#include "thetaexplorer/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QStringList>
#include <cstdio>

namespace thetaexplorer {

namespace {

LogLevel parseLogLevel(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == "debug") return LogLevel::Debug;
    if (normalized == "info") return LogLevel::Info;
    if (normalized == "warning" || normalized == "warn") return LogLevel::Warning;
    if (normalized == "error") return LogLevel::Error;
    if (normalized == "fatal") return LogLevel::Fatal;
    return LogLevel::Debug;
}

QStringList parseCategories(const QString& value)
{
    QString normalized = value;
    normalized.replace(';', ',');
    const QStringList rawParts = normalized.split(',', Qt::SkipEmptyParts);

    QStringList categories;
    for (const QString& part : rawParts) {
        const QString trimmed = part.trimmed().toLower();
        if (!trimmed.isEmpty()) {
            categories.append(trimmed);
        }
    }
    categories.removeDuplicates();
    return categories;
}

LogLevel qtTypeToLevel(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg: return LogLevel::Debug;
        case QtInfoMsg: return LogLevel::Info;
        case QtWarningMsg: return LogLevel::Warning;
        case QtCriticalMsg: return LogLevel::Error;
        case QtFatalMsg: return LogLevel::Fatal;
    }
    return LogLevel::Debug;
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    loadConfiguration();
    openLogFile();
    qInstallMessageHandler(&Logger::messageHandler);
    m_initialized = true;
}

void Logger::shutdown()
{
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.flush();
        m_logFile.close();
    }
    m_initialized = false;
}

bool Logger::shouldLog(LogLevel level, const char* category) const
{
    QMutexLocker locker(&m_mutex);

    if (static_cast<int>(level) < static_cast<int>(m_minLevel)) {
        return false;
    }

    if (m_logAllCategories) {
        return true;
    }

    const QString categoryName = QString::fromUtf8(category).trimmed().toLower();
    return m_enabledCategories.contains(categoryName);
}

QString Logger::configurationSummary() const
{
    QMutexLocker locker(&m_mutex);
    QString categories = m_logAllCategories
        ? QStringLiteral("all")
        : QStringList(m_enabledCategories.begin(), m_enabledCategories.end()).join(',');

    QString level;
    switch (m_minLevel) {
        case LogLevel::Debug: level = "debug"; break;
        case LogLevel::Info: level = "info"; break;
        case LogLevel::Warning: level = "warning"; break;
        case LogLevel::Error: level = "error"; break;
        case LogLevel::Fatal: level = "fatal"; break;
    }

    return QString("level=%1 categories=%2 file=%3 maxBytes=%4")
        .arg(level, categories, m_logFilePath, QString::number(m_maxLogBytes));
}

void Logger::loadConfiguration()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    m_logFilePath = env.value("THETA_LOG_FILE", AppPaths::defaultLogFile());
    m_minLevel = parseLogLevel(env.value("THETA_LOG_LEVEL", "debug"));

    const QString categoriesEnv = env.value("THETA_LOG_CATEGORIES", "all");
    const QStringList categories = parseCategories(categoriesEnv);
    m_logAllCategories = categories.isEmpty() || categories.contains("all");
    m_enabledCategories = QSet<QString>(categories.begin(), categories.end());

    bool ok = false;
    const qint64 maxLogBytes = env.value("THETA_LOG_MAX_BYTES").toLongLong(&ok);
    if (ok && maxLogBytes > 0) {
        m_maxLogBytes = maxLogBytes;
    }
}

void Logger::openLogFile()
{
    const QFileInfo logInfo(m_logFilePath);
    QDir dir = logInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    rotateIfNeeded();

    m_logFile.setFileName(m_logFilePath);
    m_logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
}

void Logger::rotateIfNeeded()
{
    const QFileInfo currentInfo(m_logFilePath);
    if (!currentInfo.exists() || currentInfo.size() < m_maxLogBytes) {
        return;
    }

    const QString rotatedPath = m_logFilePath + ".1";
    QFile::remove(rotatedPath);
    QFile::rename(m_logFilePath, rotatedPath);
}

QString Logger::levelName(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg: return "DBG";
        case QtInfoMsg: return "INF";
        case QtWarningMsg: return "WRN";
        case QtCriticalMsg: return "ERR";
        case QtFatalMsg: return "FAT";
    }
    return "UNK";
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Logger& logger = Logger::instance();
    {
        QMutexLocker locker(&logger.m_mutex);
        if (!logger.m_initialized) {
            return;
        }

        logger.rotateIfNeeded();
        if (!logger.m_logFile.isOpen()) {
            logger.openLogFile();
        }

        const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        QString line = QString("[%1] [%2] %3").arg(timestamp, levelName(type), msg);
        if (ctx.file && *ctx.file) {
            const QFileInfo fileInfo(QString::fromUtf8(ctx.file));
            line += QString(" (%1:%2)").arg(fileInfo.fileName()).arg(ctx.line);
        }
        line += '\n';

        if (logger.m_logFile.isOpen()) {
            logger.m_logFile.write(line.toUtf8());
            logger.m_logFile.flush();
        }

        std::fprintf(stderr, "%s", line.toUtf8().constData());
        std::fflush(stderr);
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace thetaexplorer
