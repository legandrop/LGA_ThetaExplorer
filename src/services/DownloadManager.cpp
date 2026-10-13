#include "thetaexplorer/DownloadManager.h"
#include "thetaexplorer/Logger.h"

DownloadManager::DownloadManager(QObject* parent)
    : QObject(parent)
{}

void DownloadManager::startBatch(const QStringList& fileNames)
{
    m_pending   = fileNames;
    m_total     = fileNames.size();
    m_completed = 0;
    m_errors    = 0;
    m_active    = true;
    LOGD("download") << "startBatch:" << m_total << "files";
    emit progressChanged(0, m_total);
}

void DownloadManager::onFileCompleted(const QString& fileName)
{
    m_completed++;
    m_pending.removeOne(fileName);
    LOGD("download") << "completed" << m_completed << "/" << m_total;
    emit progressChanged(m_completed, m_total);
    if (m_completed + m_errors >= m_total) {
        m_active = false;
        emit batchCompleted(m_completed, m_errors);
    }
}

void DownloadManager::onFileError(const QString& fileName, const QString& error)
{
    m_errors++;
    m_pending.removeOne(fileName);
    LOGW("download") << "error for" << fileName << ":" << error;
    emit progressChanged(m_completed, m_total);
    if (m_completed + m_errors >= m_total) {
        m_active = false;
        emit batchCompleted(m_completed, m_errors);
    }
}

void DownloadManager::reset()
{
    m_pending.clear();
    m_total     = 0;
    m_completed = 0;
    m_errors    = 0;
    m_active    = false;
}
