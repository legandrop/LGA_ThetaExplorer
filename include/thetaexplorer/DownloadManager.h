#pragma once
#include <QObject>
#include <QStringList>

class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr);

    void  startBatch(const QStringList& fileNames);
    void  onFileCompleted(const QString& fileName);
    void  onFileError(const QString& fileName, const QString& error);
    void  reset();

    int   total()     const { return m_total; }
    int   completed() const { return m_completed; }
    bool  isActive()  const { return m_active; }

signals:
    void batchCompleted(int successCount, int errorCount);
    void progressChanged(int completed, int total);

private:
    QStringList m_pending;
    int         m_total     = 0;
    int         m_completed = 0;
    int         m_errors    = 0;
    bool        m_active    = false;
};
